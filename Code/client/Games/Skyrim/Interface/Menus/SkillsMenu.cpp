// The game calls it statsmenu.
#include <Interface/Menus/SkillsMenu.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>

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
static void* s_pendingTreeModel = nullptr;
static NiUpdateData s_pendingTreeData{};

static bool IsMainThread()
{
    const auto* pOwner = reinterpret_cast<const uint8_t* (*)()>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kMainThreadOwner)();
    return GetCurrentThreadId() == *reinterpret_cast<const DWORD*>(pOwner + 0x108);
}

static void HookTreeModelUpdate(void* apModel, NiUpdateData* apData)
{
    static const BSFixedString s_statsName("StatsMenu");
    if (IsMainThread() || !IsMenuOpenUnpaused(s_statsName))
    {
        RealTreeModelUpdate(apModel, apData);
        return;
    }

    std::scoped_lock lock(s_treeUpdateLock);
    s_pendingTreeModel = apModel;
    s_pendingTreeData = *apData;
}

// Called from the start of Main::Update.
void RunDeferredSkillsTreeUpdate()
{
    std::scoped_lock lock(s_treeUpdateLock);
    if (!s_pendingTreeModel)
        return;

    RealTreeModelUpdate(s_pendingTreeModel, &s_pendingTreeData);
    s_pendingTreeModel = nullptr;
}

static uint32_t HookStatsProcessMessage(IMenu* apThis, UIMessage& aMessage)
{
    const uint32_t result = TiltedPhoques::ThisCall(RealStatsProcessMessage, apThis, aMessage);

    if (aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide)
    {
        std::scoped_lock lock(s_treeUpdateLock);
        s_pendingTreeModel = nullptr;
    }

    if (!apThis->PausesGame())
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
        s_checksReady = allFound;
        if (allFound)
            spdlog::info("Skills menu: checks found; it pauses as in the game until a server unpauses it");

        RealStatsProcessMessage = reinterpret_cast<TStatsProcessMessage*>(pProcessMessage);
        TP_HOOK(&RealStatsProcessMessage, HookStatsProcessMessage);
        TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + kTreeModelUpdateCall, RealTreeModelUpdate, &HookTreeModelUpdate);
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
