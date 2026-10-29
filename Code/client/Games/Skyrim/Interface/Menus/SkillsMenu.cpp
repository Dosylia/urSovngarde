// The game calls it statsmenu.
#include <Interface/Menus/SkillsMenu.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>
#include <PatchCheck.h>

// VR draws the skill tree as a 3D model in the world scene (StatsMenu+0xC0, a BSFadeNode that fades out between 500
// and 1000 units). Tree view moves it about 3000 units away. Paused, the fade update never runs; unpaused the model
// is faded to 0 and never drawn, so the tree renders black. Keep it fully faded in while the menu does not pause.
constexpr size_t kStatsModel = 0xC0;
constexpr size_t kFadeNodeCurrentFade = 0x158;
constexpr size_t kAvObjectFlags = 0x10C;
constexpr uint32_t kIgnoreFade = 0x8000;

TP_THIS_FUNCTION(TStatsProcessMessage, uint32_t, IMenu, UIMessage&);
static TStatsProcessMessage* RealStatsProcessMessage;

// The main thread check, skipped below while the menu is unpaused, lets the menu's update run from the UI job, a worker thread while unpaused.
// That update also moves the tree model (NiAVObject::Update on StatsMenu+0xC0) while the main thread draws it, so
// stars and names blink. Off the main thread the update is kept and run at the start of the next main loop
// (Main::Update, see SkyrimVM64.cpp), before anything draws.
constexpr uintptr_t kTreeModelUpdateCall = 0x8ED62B;
constexpr uintptr_t kMainThreadOwner = 0x137B630;

struct NiUpdateData
{
    float time;
    uint32_t flags;
};

static void (*RealTreeModelUpdate)(void*, NiUpdateData*);
static std::mutex s_treeUpdateLock;
// Between the menu's show and hide, changed and read under the lock: once the hide begins, no worker keeps the model,
// which the hide may free.
static bool s_statsShown = false;
static void* s_pendingTreeModel = nullptr;
static NiUpdateData s_pendingTreeData{};

static bool IsMainThread()
{
    const auto* pOwner = reinterpret_cast<const uint8_t* (*)()>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kMainThreadOwner)();
    return GetCurrentThreadId() == *reinterpret_cast<const DWORD*>(pOwner + 0x108);
}

static void HookTreeModelUpdate(void* apModel, NiUpdateData* apData)
{
    if (IsMainThread())
    {
        RealTreeModelUpdate(apModel, apData);
        return;
    }

    // Off the main thread only because the main-thread check is skipped; the game itself never updates the model here.
    // After the hide began it is dropped, not run: the model may be freed under it.
    std::scoped_lock lock(s_treeUpdateLock);
    if (!s_statsShown)
        return;
    s_pendingTreeModel = apModel;
    s_pendingTreeData = *apData;
}

// Called from the start of Main::Update.
void RunDeferredSkillsTreeUpdate()
{
    std::scoped_lock lock(s_treeUpdateLock);
    if (!s_pendingTreeModel || !s_statsShown)
        return;

    RealTreeModelUpdate(s_pendingTreeModel, &s_pendingTreeData);
    s_pendingTreeModel = nullptr;
}

// Unpaused, the hide runs on a worker and frees the tree model while the main thread draws its passes, which drew a freed one (a crash in the draw).
// So an unpaused hide takes a reference of ours on the model, and only the main thread lets it go, at the start of a frame, when nothing draws.
struct ClosedTree
{
    NiRefObject* pModel;
    uint32_t FramesAlone; // frame starts with ours the last reference
    uint32_t FramesHeld;
};
static std::vector<ClosedTree> s_closedTrees; // under s_treeUpdateLock

static void HoldClosedTree(uint8_t* apModel)
{
    auto* pModel = reinterpret_cast<NiRefObject*>(apModel);
    if (std::any_of(s_closedTrees.begin(), s_closedTrees.end(), [pModel](const ClosedTree& acTree) { return acTree.pModel == pModel; }))
        return;
    pModel->IncRef();
    s_closedTrees.push_back({pModel, 0, 0});
    spdlog::info("Skills menu: its tree model {} held at the unpaused close, to be let go on the main thread", fmt::ptr(pModel));
}

// Called from the start of Main::Update.
void ReleaseClosedSkillsTrees()
{
    // Let go outside the lock: the model's destruction must not run under a lock the menu's hooks take.
    std::vector<NiRefObject*> released;
    {
        std::scoped_lock lock(s_treeUpdateLock);
        for (auto it = s_closedTrees.begin(); it != s_closedTrees.end();)
        {
            // Ours the last for two frame starts: the frame that drew it is over, so it goes here.
            // Still kept by the game after about ten seconds, or the menu shown again: only ours is dropped.
            ClosedTree& tree = *it;
            ++tree.FramesHeld;
            tree.FramesAlone = tree.pModel->refCount == 1 ? tree.FramesAlone + 1 : 0;
            const bool cAlone = tree.FramesAlone >= 2;
            if (!cAlone && !(tree.pModel->refCount > 1 && (s_statsShown || tree.FramesHeld > 900)))
            {
                ++it;
                continue;
            }
            spdlog::info("Skills menu: its tree model {} let go on the main thread, {} frames after the close{}", fmt::ptr(tree.pModel), tree.FramesHeld,
                         cAlone ? "" : ", still kept by the game");
            released.push_back(tree.pModel);
            it = s_closedTrees.erase(it);
        }
    }
    for (NiRefObject* pModel : released)
        pModel->DecRef();
}

static uint32_t HookStatsProcessMessage(IMenu* apThis, UIMessage& aMessage)
{
    const bool cHiding = aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide;

    // Before the game handles it: a hide may free the model, and a worker must not keep it from here on.
    if (aMessage.eType == UIMessage::kShow || cHiding)
    {
        // Which thread shows and takes down the menu against the one that draws it (UnpausedMenusOnMainLoop).
        const uint32_t cThread = GetCurrentThreadId();
        const uint32_t cMain = GameMainThreadId();
        spdlog::info("Skills menu: {} handled on thread {} ({}), the menu {}", cHiding ? "hide" : "show", cThread,
                     cThread == cMain ? "the main thread" : fmt::format("not the main thread {}", cMain), apThis->PausesGame() ? "pausing" : "unpaused");
        std::scoped_lock lock(s_treeUpdateLock);
        s_statsShown = !cHiding;
        s_pendingTreeModel = nullptr;
        if (cHiding && !apThis->PausesGame())
            if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kStatsModel))
                HoldClosedTree(pModel);
    }

    // Cleared on the way out, as MapMenu.cpp does: the model is not left exempt from the fade that removes it.
    // Before the game handles the hide, which may free the model.
    if (cHiding)
    {
        if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kStatsModel))
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) &= ~kIgnoreFade;
    }

    const uint32_t result = TiltedPhoques::ThisCall(RealStatsProcessMessage, apThis, aMessage);

    // Set while open unpaused.
    if (!cHiding && !apThis->PausesGame())
    {
        if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kStatsModel))
        {
            *reinterpret_cast<float*>(pModel + kFadeNodeCurrentFade) = 1.f;
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) |= kIgnoreFade;
        }
    }

    return result;
}

// The unpaused menu needs three of the game's checks skipped: the main-thread check, the update check and the
// controls guard. They are skipped only while the server unpauses the menu (Gameplay:bUnpausedMenus); elsewhere the
// menu runs exactly as in the game. Skipped for good, a paused menu's messages ran off the main thread too, as the
// Sleep/Wait menu's did (7a4b2002). Each site's first two bytes are swapped in one aligned write, so a thread running
// it sees either form whole: a jne (0F 85 rel32) becomes a short jump over its other four bytes, a je short two nops.
struct SwitchedCheck
{
    bool InCanProcess; // else in ProcessMessage
    size_t Offset;
    uint8_t Expected[6];
    size_t Size;
    uint16_t Skipped;
    const char* What;
    uint8_t* pSite;
    uint16_t Kept;
};

// SE 51638 (StatsMenu::ProcessMessage) is VR 0x8ec3e0; the menu event handler's CanProcess is VR 0x8ed870.
constexpr uintptr_t kStatsCanProcess = 0x8ED870;
static SwitchedCheck s_checks[]{
    // Fix for menu not appearing: jnz past the update when off the main thread.
    {false, 0x91C, {0x0F, 0x85, 0x38, 0x09, 0x00, 0x00}, 6, 0x04EB, "main thread check"},
    // Keep the menu updated.
    {false, 0x109C, {0x74, 0x5F}, 2, 0x9090, "update check"},
    // Fix for controls not working.
    {true, 0x98, {0x0F, 0x85, 0x79, 0x01, 0x00, 0x00}, 6, 0x04EB, "controls guard"},
};
static bool s_checksReady = false;
// Read by the menu queue on another thread.
static std::atomic<bool> s_checksSkipped{false};

static void SetChecksSkipped(bool aSkipped) noexcept
{
    for (SwitchedCheck& check : s_checks)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(check.pSite, sizeof(uint16_t), PAGE_EXECUTE_READWRITE, &oldProtect))
            continue;
        InterlockedExchange16(reinterpret_cast<volatile SHORT*>(check.pSite), static_cast<SHORT>(aSkipped ? check.Skipped : check.Kept));
        VirtualProtect(check.pSite, sizeof(uint16_t), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), check.pSite, sizeof(uint16_t));
    }
    s_checksSkipped = aSkipped;
    spdlog::info("Skills menu: {}", aSkipped ? "this server unpauses it, so its main-thread, update and controls checks are skipped" : "it pauses as in the game");
}

bool SkillsMenuCanRunUnpaused() noexcept
{
    return s_checksReady && s_checksSkipped;
}

// Called from the start of Main::Update. Changed only while no skills menu exists, so its code is not rewritten under a message.
void UpdateSkillsMenuChecks()
{
    if (!s_checksReady || UnpausedMenusOnline() == s_checksSkipped.load())
        return;

    static const BSFixedString s_statsName("StatsMenu");
    UI* pUI = UI::Get();
    if (pUI && !pUI->FindMenuByName(s_statsName))
        SetChecksSkipped(UnpausedMenusOnline());
}
#endif

static TiltedPhoques::Initializer s_skillsMenuInit(
    []()
    {
        // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/StatsMenuEx.cpp
        // Hoooks from souls RE
#ifdef SKYRIMVR
        // VR offsets differ from AE's; each site is checked before it is patched.
        VersionDbPtr<uint8_t> ProcessMessage(51638);
        uint8_t* pProcessMessage = ProcessMessage.Get();
        uint8_t* pCanProcess = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + kStatsCanProcess;

        // No freeze frame background, which VR never draws: or dword ptr [rsi+0x1C], 0x20. Harmless paused, so for good.
        constexpr uint8_t cFreezeFrameWrite[]{0x83, 0x4E, 0x1C, 0x20};
        if (std::memcmp(pProcessMessage + 0xBB6, cFreezeFrameWrite, sizeof(cFreezeFrameWrite)) == 0)
            TiltedPhoques::Nop(pProcessMessage + 0xBB6, sizeof(cFreezeFrameWrite));
        else
            spdlog::error("Skills menu: {} is not the freeze frame flag write, leaving it alone; the menu will be black while unpaused", fmt::ptr(pProcessMessage + 0xBB6));

        // The switched checks: all three or none, and the menu pauses as in the game when any is not where it was read.
        bool allFound = true;
        for (SwitchedCheck& check : s_checks)
        {
            check.pSite = (check.InCanProcess ? pCanProcess : pProcessMessage) + check.Offset;
            if (std::memcmp(check.pSite, check.Expected, check.Size) != 0)
            {
                spdlog::error("Skills menu: {} at {} is not the expected instruction; the menu keeps pausing", check.What, fmt::ptr(check.pSite));
                allFound = false;
            }
            check.Kept = *reinterpret_cast<const uint16_t*>(check.pSite);
        }

        // Without the deferred tree update an unpaused tree would be updated on a worker while drawn, so it is needed too.
        auto* pTreeUpdateCall = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + kTreeModelUpdateCall;
        if (PatchCheck::IsCall(pTreeUpdateCall))
            TiltedPhoques::SwapCall(pTreeUpdateCall, RealTreeModelUpdate, &HookTreeModelUpdate);
        else
        {
            spdlog::error("Skills menu: {} is not the tree model's update call ({}); the menu keeps pausing", fmt::ptr(pTreeUpdateCall), PatchCheck::Bytes(pTreeUpdateCall, 5));
            allFound = false;
        }

        s_checksReady = allFound;
        if (allFound)
            spdlog::info("Skills menu: checks found; it pauses as in the game until a server unpauses it");

        RealStatsProcessMessage = reinterpret_cast<TStatsProcessMessage*>(pProcessMessage);
        TP_HOOK(&RealStatsProcessMessage, HookStatsProcessMessage);
#else
        // Fix for menu not appearing
        VersionDbPtr<uint8_t> ProcessMessage(52510);
        TiltedPhoques::Nop(ProcessMessage.Get() + 0x84E, 6);
        // Prevent setting kFreezeFrameBackground flag
        TiltedPhoques::Nop(ProcessMessage.Get() + 0xA10, 4);
        // Keep the menu updated
        TiltedPhoques::Nop(ProcessMessage.Get() + 0x1040, 2);

        // Fix for controls not working
        VersionDbPtr<uint8_t> controlPatch(52518);
        TiltedPhoques::Nop(controlPatch.Get() + 0x46, 4);
        TiltedPhoques::Nop(controlPatch.Get() + 0x4A, 2);
#endif
    });
