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

// The main thread check nopped below lets the menu's update run from the UI job, a worker thread while unpaused.
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

// Nops a patch site only if it holds the instruction it was read from (SkyrimVR 1.4.15, TiltedEvolutionVR 5ab99be).
static bool NopChecked(uint8_t* apSite, std::initializer_list<uint8_t> aExpected, const char* acpWhat)
{
    if (std::memcmp(apSite, aExpected.begin(), aExpected.size()) != 0)
    {
        spdlog::error("Skills menu: {} at {} is not the expected instruction, leaving it alone; the menu may misbehave while unpaused", acpWhat, fmt::ptr(apSite));
        return false;
    }
    TiltedPhoques::Nop(apSite, aExpected.size());
    return true;
}
#endif

static TiltedPhoques::Initializer s_skillsMenuInit(
    []()
    {
        // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/StatsMenuEx.cpp
        // Hoooks from souls RE
#ifdef SKYRIMVR
        // SE 51638 (StatsMenu::ProcessMessage) is VR 0x8ec3e0; the menu event handler's CanProcess is VR 0x8ed870.
        // VR offsets differ from AE's; each site is checked before it is patched.
        VersionDbPtr<uint8_t> ProcessMessage(51638);
        uint8_t* pProcessMessage = ProcessMessage.Get();
        uint8_t* pCanProcess = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x8ED870;

        // Fix for menu not appearing: jnz past the update when off the main thread.
        NopChecked(pProcessMessage + 0x91C, {0x0F, 0x85, 0x38, 0x09, 0x00, 0x00}, "main thread check");
        // No freeze frame background, which VR never draws: or dword ptr [rsi+0x1C], 0x20.
        NopChecked(pProcessMessage + 0xBB6, {0x83, 0x4E, 0x1C, 0x20}, "freeze frame flag write");
        // Keep the menu updated.
        NopChecked(pProcessMessage + 0x109C, {0x74, 0x5F}, "update check");
        // Fix for controls not working.
        NopChecked(pCanProcess + 0x98, {0x0F, 0x85, 0x79, 0x01, 0x00, 0x00}, "controls guard");

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
