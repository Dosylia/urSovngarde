#include <Games/Skyrim/Interface/IMenu.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <TiltedOnlinePCH.h>
#include "immersive_launcher/stubs/DllBlocklist.h"

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/DeferredSleepWait.h>
#endif
#include <World.h>

static bool g_RequestUnpauseAll{false};

UI* UI::Get()
{
    POINTER_SKYRIMSE(UI*, s_instance, 400327, 514178);
    return *s_instance.Get();
}

bool UI::GetMenuOpen(const BSFixedString& acName) const
{
    if (acName.data == nullptr)
        return false;

    TP_THIS_FUNCTION(TMenuSystem_IsOpen, bool, const UI, const BSFixedString&);
    POINTER_SKYRIMSE(TMenuSystem_IsOpen, s_isMenuOpen, 82074, 82074);

    return TiltedPhoques::ThisCall(s_isMenuOpen.Get(), this, acName);
}

void UI::CloseAllMenus()
{
    TP_THIS_FUNCTION(TUI_CloseAll, void, const UI);
    POINTER_SKYRIMSE(TUI_CloseAll, s_CloseAll, 82088, 82088);

    TiltedPhoques::ThisCall(s_CloseAll.Get(), this);
}

BSFixedString* UI::LookupMenuNameByInstance(IMenu* apMenu)
{
    for (auto& it : menuMap)
    {
        if (it.value.spMenu == apMenu)
            return &it.key;
    }
    return nullptr;
}

IMenu* UI::FindMenuByName(const BSFixedString& acName)
{
    for (const auto& it : menuMap)
    {
        if (it.key == acName)
            return it.value.spMenu;
    }
    return nullptr;
}

void UI::DebugLogAllMenus()
{
    for (auto& e : menuStack)
    {
        spdlog::info("Menu {}", e->uiMenuFlags);
    }
}

static void UnfreezeMenu(IMenu* apEntry)
{
    if (apEntry->PausesGame())
    {
        apEntry->ClearFlag(IMenu::kPausesGame);
#ifdef SKYRIMVR
        // VR shows its menu panel (PlayerCharacter's UINode) only while a menu pauses the game or carries this flag.
        // MessageBoxMenu and Console lack it, so unpaused they would open invisibly in the headset (TiltedEvolutionVR 5c30ec6).
        apEntry->SetFlag(IMenu::kUpdateUsesCursor);
#endif
    }

    if (apEntry->FreezesBackground())
        apEntry->ClearFlag(IMenu::kFreezeFrameBackground);

    if (apEntry->FreezesFramePause())
        apEntry->ClearFlag(IMenu::kFreezeFramePause);
}

// Whether a memory range is committed and readable, so a guessed pointer is checked before it is read.
namespace
{
bool ReadableBytes(const void* apAddress, const size_t aBytes) noexcept
{
    if (!apAddress)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(apAddress, &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return reinterpret_cast<uintptr_t>(apAddress) + aBytes <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}
} // namespace

static constexpr const char* kAllowList[] = {
    "TweenMenu",     "MagicMenu",     "InventoryMenu",
    // On VR Menus/SkillsMenu.cpp keeps the skill tree from going black while unpaused.
    "StatsMenu",
    // UnfreezeMenu gives a message box the flag the VR panel needs, so it can run unpaused and stay visible.
    "MessageBoxMenu",
    "ContainerMenu", "FavoritesMenu", "Tutorial Menu", "Console",
#ifdef SKYRIMVR
    // Unpaused in VR only. Menus/MapMenu.cpp fixes what that breaks: fog, fade and laser pointer.
    "MapMenu",
    // Unpaused in VR only, so the others keep playing while one player picks how long to sleep or wait.
    "Sleep/Wait Menu",
#endif
    //"MapMenu", // MapMenu is disabled till we find a proper fix for first person.
    //"Journal Menu", // Journal menu, aka pause menu, is disabled until we find a fix for manual save crashing while unpaused.
};

#ifdef SKYRIMVR
bool UnpausedMenusOnline() noexcept
{
    World& world = World::Get();
    return world.GetTransport().IsConnected() && world.GetServerSettings().UnpausedMenus;
}

bool IsMenuOpenUnpaused(const BSFixedString& acName) noexcept
{
    UI* pUI = UI::Get();
    if (!pUI || !pUI->GetMenuOpen(acName))
        return false;
    const IMenu* pMenu = pUI->FindMenuByName(acName);
    return pMenu && !pMenu->PausesGame();
}

// The menus Gameplay:bUnpausedMenus decides about; the rest of kAllowList is unpaused either way.
static constexpr const char* kOptionalUnpausedMenus[] = {"StatsMenu", "MapMenu", "Sleep/Wait Menu"};

static bool IsOptionalUnpausedMenu(const char* acpName)
{
    for (const char* pName : kOptionalUnpausedMenus)
        if (std::strcmp(acpName, pName) == 0)
            return true;
    return false;
}

bool IsOptionalMenuOpenUnpaused() noexcept
{
    // Built on first use: the game's string table does not exist yet when static initializers run.
    static const auto* s_names = []()
    {
        auto* pNames = new std::vector<BSFixedString>();
        for (const char* pName : kOptionalUnpausedMenus)
            pNames->emplace_back(pName);
        return pNames;
    }();

    for (const auto& name : *s_names)
        if (IsMenuOpenUnpaused(name))
            return true;
    return false;
}

// Built on first use: the game's string table does not exist yet when static initializers run.
static bool IsAllowListMenuOpenUnpaused()
{
    static const auto* s_names = []()
    {
        auto* pNames = new std::vector<BSFixedString>();
        for (const char* pName : kAllowList)
            pNames->emplace_back(pName);
        return pNames;
    }();

    for (const auto& name : *s_names)
    {
        if (IsMenuOpenUnpaused(name))
            return true;
    }
    return false;
}

// VR melee is physics driven: a hit lands when a swing is detected (player update). Paused, nothing moved; the menus
// unpaused above leave it running, so swinging a drawn weapon in them hits. The swing detector already cancels the
// swing while either of two menus is open (UI::IsMenuOpen at 0x1406B93BB and 0x1406B93D9); the second also reports these.
constexpr uintptr_t kMeleeSwingMenuCheck = 0x6B93D9;

using TIsMenuOpen = bool(const UI*, const BSFixedString&);
static TIsMenuOpen* RealMeleeSwingMenuCheck;

static bool HookMeleeSwingMenuCheck(const UI* apUI, const BSFixedString& acName)
{
    return RealMeleeSwingMenuCheck(apUI, acName) || IsAllowListMenuOpenUnpaused();
}

// PlayerControls' can-process check (0x14072DB00) fails while the game is paused, which is how a paused menu stops
// gameplay input. The menus unpaused above still let some through (jump and sneak in all; walking and turning in
// MapMenu). Fail it while one is open. Menu navigation is MenuControls, and the map's pan, zoom and rotate keep working.
constexpr uintptr_t kPlayerControlsCanProcess = 0x72DB00;

TP_THIS_FUNCTION(TPlayerControlsCanProcess, bool, void);
static TPlayerControlsCanProcess* RealPlayerControlsCanProcess;

// SleepWaitMenu::ProcessMessage (0x1408EA840) passes input (Scaleform key events, Accept, Cancel) on only from the main
// thread: GetCurrentThreadId, compared with the main thread's id, then this jnz past the handling. Unpaused, the UI
// handles messages on its job pool, so the menu ignored every input. Paused it always runs on the main thread.
constexpr uintptr_t kSleepWaitMainThreadCheck = 0x8EA896;
constexpr uint8_t kSleepWaitMainThreadJump[]{0x0F, 0x85, 0xD4, 0x02, 0x00, 0x00};

// The menu's OK callback (0x1408EACA0: click sound, FadeOutGame, start sleep or wait for N hours) is the original sleep
// start. Run from a UI worker it hung Main::Render; run on the main thread it works in the unpaused menu. So it is
// taken off the worker and run at the next main loop. Pausing the game under the open menu by hand hung it.
constexpr uintptr_t kSleepWaitOkCallback = 0x8EACA0; // registered as "OK" in SleepWaitMenu::Accept (0x1408EAB90)
constexpr uintptr_t kMainSingleton = 0x2FEB7A0;      // what the menu's own check reads; main thread id at +0x20
constexpr uintptr_t kSleepWaitProcessMessage = 0x8EA840;

// SleepWaitMenu::ProcessMessage paces the native sleep step (0x6C2C00) using its timer at +0x40.
// The two regular player updates also call that step, normally stopped by the paused menu. Unpaused, they
// consume the remaining sleep every frame. Suppress only those calls while the menu owns the countdown;
// leave the menu's timed call at 0x8EA9E7 and the jail fast-forward at 0x6D6170 alone.
// The menu's +0x61 flag means the countdown is active, not finished. Its timed update clears that flag
// and closes the menu naturally; queuing the menu again is not a reason to send Cancel.
constexpr uintptr_t kPlayerSleepWaitCalls[] = {0x6BF425, 0x6BFDF5};
constexpr uintptr_t kSleepWaitStep = 0x6C2C00;
using TSleepWaitStep = void(void* apPlayer);
static TSleepWaitStep* RealPlayerSleepWaitStep;

static void HookPlayerSleepWaitStep(void* apPlayer)
{
    static const BSFixedString s_menuName("Sleep/Wait Menu");
    if (IsMenuOpenUnpaused(s_menuName))
        return;

    RealPlayerSleepWaitStep(apPlayer);
}

using TSleepWaitOk = void(void* apArgs);
static TSleepWaitOk* RealSleepWaitOk;

static DeferredSleepWait s_sleepWaitAccept;
TP_THIS_FUNCTION(TSleepWaitProcessMessage, uint32_t, IMenu, UIMessage&);
static TSleepWaitProcessMessage* RealSleepWaitProcessMessage;

static uint32_t HookSleepWaitProcessMessage(IMenu* apMenu, UIMessage& aMessage)
{
    // Invalidate before the native close can destroy the menu. A show starts a fresh session even
    // when the allocator reuses the previous menu's address.
    if (aMessage.eType == UIMessage::kShow)
        s_sleepWaitAccept.Open(apMenu);
    else if (aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide)
        s_sleepWaitAccept.Close(apMenu);

    return TiltedPhoques::ThisCall(RealSleepWaitProcessMessage, apMenu, aMessage);
}

static bool OnMainThread()
{
    const auto* pMain = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kMainSingleton);
    return pMain && GetCurrentThreadId() == *reinterpret_cast<const DWORD*>(pMain + 0x20);
}

// FxDelegateArgs as the OK callback reads it: the menu at +0x18, the argument array at +0x28, the hours a double at +0x10 of the first.
static void HookSleepWaitOk(void* apArgs)
{
    if (OnMainThread())
    {
        s_sleepWaitAccept.Take(*reinterpret_cast<void**>(static_cast<uint8_t*>(apArgs) + 0x18));
        RealSleepWaitOk(apArgs);
        return;
    }

    auto* pArgs = static_cast<uint8_t*>(apArgs);
    const void* pMenu = *reinterpret_cast<void**>(pArgs + 0x18);
    if (pMenu)
        s_sleepWaitAccept.Queue(pMenu, *reinterpret_cast<const double*>(*reinterpret_cast<uint8_t**>(pArgs + 0x28) + 0x10));
}

// Called from the start of Main::Update (SkyrimVM64.cpp).
void RunDeferredSleepWaitAccept()
{
    if (!s_sleepWaitAccept.HasPending())
        return;

    static const BSFixedString s_menuName("Sleep/Wait Menu");
    UI* pUI = UI::Get();
    IMenu* pMenu = pUI && pUI->GetMenuOpen(s_menuName) ? pUI->FindMenuByName(s_menuName) : nullptr;
    const auto cHours = s_sleepWaitAccept.Take(pMenu);
    if (!cHours)
        return;

    // A GFxValue holding the hours as a number, and the FxDelegateArgs fields the callback reads.
    alignas(16) uint8_t value[0x18]{};
    *reinterpret_cast<uint32_t*>(value + 0x08) = 3; // GFxValue::kNumber
    *reinterpret_cast<double*>(value + 0x10) = *cHours;
    alignas(16) uint8_t args[0x40]{};
    *reinterpret_cast<IMenu**>(args + 0x18) = pMenu;
    *reinterpret_cast<uint8_t**>(args + 0x28) = value;
    *reinterpret_cast<uint32_t*>(args + 0x30) = 1;

    RealSleepWaitOk(args);
}

static bool HookPlayerControlsCanProcess(void* apControls)
{
    if (IsAllowListMenuOpenUnpaused())
        return false;

    return TiltedPhoques::ThisCall(RealPlayerControlsCanProcess, apControls);
}
#endif

static void* (*UI_AddToActiveQueue)(UI*, IMenu*, void*);

static void* UI_AddToActiveQueue_Hook(UI* apSelf, IMenu* apMenu, void* apFoundItem /*In reality a reference*/)
{
    // Logs every menu the game queues, connected or not; the launcher reads the "Menu queued" lines.
    // The VR HUD widgets (WS*) are queued every frame and are left out.
    if (apMenu)
    {
        const BSFixedString* pQueuedName = apSelf->LookupMenuNameByInstance(apMenu);
        if (pQueuedName && pQueuedName->AsAscii() && strncmp(pQueuedName->AsAscii(), "WS", 2) == 0)
            return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
        const BSFixedString* pName = apSelf->LookupMenuNameByInstance(apMenu);
        spdlog::info("Menu queued: {}{} (connected {})", pName ? pName->AsAscii() : "?", apMenu->PausesGame() ? " [pauses]" : "", World::Get().GetTransport().IsConnected() ? "yes" : "no");
#ifdef SKYRIMVR
        // The in-headset dashboard is made at the main menu, where a pause costs nothing.
        // Made later, on the first update in the world, it stops the game for up to 1.5 s right after the save loads.
        // OverlayService::OnUpdate still makes it if this did not.
        if (pName && pName->AsAscii() && strcmp(pName->AsAscii(), "Main Menu") == 0)
            World::Get().GetOverlayService().CreateVR();
#endif
    }

    // if the menu is empty we let the real function handle it.
    if (!apMenu || !World::Get().GetTransport().IsConnected() || stubs::g_IsSoulsREActive)
        return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);

#if 0
        if (auto* pName = apSelf->LookupMenuNameByInstance(apEntry))
        {
            spdlog::info("Menu requested {}", pName->AsAscii());
        }
#endif

    // A menu opened out of a conversation is left to pause, whatever the list says.
    // Unpaused, a trade panel opened over a Dialogue Menu draws but the dialogue keeps the input; pausing hands the focus over.
    // Only while a conversation is open, so looting a chest or a corpse still leaves everyone else moving.
    const bool cInConversation = apSelf->GetMenuOpen(BSFixedString("Dialogue Menu"));

    // NOTE(Force): could also compare by RTTI later on...
    for (const char* item : kAllowList)
    {
#ifdef SKYRIMVR
        if (IsOptionalUnpausedMenu(item) && !UnpausedMenusOnline())
            continue;
#endif
        if (auto* pMenu = apSelf->FindMenuByName(item))
        {
            if (pMenu == apMenu)
            {
                if (cInConversation)
                {
                    spdlog::info("Menu opened while connected: {} (left paused; a conversation is open and would keep the input)", item);
                    break;
                }

                spdlog::info("Menu opened while connected: {} (runs unpaused)", item);
                UnfreezeMenu(apMenu);
            }
        }
    }

    return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
}

using TCallback = void(void*, const BSFixedString*, uint32_t, void*);
static TCallback* UIMessageQueue__AddMessage_Real;

static bool s_sendingEnemyMeter = false;
static std::chrono::steady_clock::time_point s_lastGameEnemyMeterTargetAt{};

void UIMessageQueue__AddMessage(void* a1, const BSFixedString* a2, UIMessage::UI_MESSAGE_TYPE a3, void* a4)
{
    // An empty message box opens at the main menu and after every save load, pausing the world; that show is dropped.
    if (a2 && a2->AsAscii() && strcmp(a2->AsAscii(), "MessageBoxMenu") == 0)
    {
        // Only the phantom's caller (SkyrimVR 1.4.15 +0x168507) is dropped, and only while the main menu or a loading screen is up.
        // Other data-less shows are real, such as the level-up choice panel (from +0x8d8683); dropping it leaves the player stuck.
        // +0x168507 is a shared helper that also queues real boxes in play, and dropping one of those crashes the game.
        const uintptr_t callerOffset = reinterpret_cast<uintptr_t>(_ReturnAddress()) - 0x140000000ull;
        bool loadingOrMainMenu = false;
        if (UI* pUi = UI::Get())
        {
            for (uint32_t i = 0; i < pUi->menuStack.length; ++i)
            {
                IMenu* pMenu = pUi->menuStack[i];
                const BSFixedString* pName = pMenu ? pUi->LookupMenuNameByInstance(pMenu) : nullptr;
                if (pName && pName->AsAscii() && (strcmp(pName->AsAscii(), "Loading Menu") == 0 || strcmp(pName->AsAscii(), "Main Menu") == 0))
                    loadingOrMainMenu = true;
            }
        }
        const bool phantom = a3 == UIMessage::kShow && a4 == nullptr && callerOffset == 0x168507ull && loadingOrMainMenu;
        if (phantom)
        {
            spdlog::info("UI message for MessageBoxMenu from SkyrimVR.exe+0x168507 dropped, the empty box that paused the world at load");
            return;
        }
    }
    // The game pointing its enemy meter (WSEnemyMeters) at someone: HUDData type 0xB with a non-zero actor handle at +0x28.
    // Remembered so the other player's meter never steals a real enemy's.
    if (a2 && a2->AsAscii() && strcmp(a2->AsAscii(), "WSEnemyMeters") == 0 && a3 == UIMessage::kUpdate && a4 && !s_sendingEnemyMeter && ReadableBytes(a4, 0x30) &&
        *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(a4) + 0x10) == 0xB && *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(a4) + 0x28) != 0)
        s_lastGameEnemyMeterTargetAt = std::chrono::steady_clock::now();

    UIMessageQueue__AddMessage_Real(a1, a2, a3, a4);
}

void UI::SetEnemyMeterTarget(uint32_t aHandle, uint16_t aLevel)
{
#ifdef SKYRIMVR
    // The message the game sends from its hit handling (SE 0x1408D5130+0x284, VR +0x902ed4), queued as an update for WSEnemyMeters.
    // A HUDData from the queue's factory: type 0xB at +0x10, level at +0x20, flag bytes 1,1 at +0x22, actor handle at +0x28 (0 clears).
    // EnemyHealth::Update then shows the meter itself.
    POINTER_SKYRIMSE(void*, s_queue, 400445, 400445);
    TP_THIS_FUNCTION(TCreateUIMessageData, void*, void, const BSFixedString*);
    POINTER_SKYRIMSE(TCreateUIMessageData, s_createData, 80061, 80061);
    void* pQueue = s_queue.Get() ? *s_queue.Get() : nullptr;
    if (!pQueue || !s_createData.Get() || !UIMessageQueue__AddMessage_Real)
        return;

    // The meter menu has to be open: the journal or the console rebuilds the HUD's sub-menus, and WSEnemyMeters is briefly gone.
    // An update sent into that gap allocates a HUDData for a menu that cannot receive it, and crashes the game soon after.
    BSFixedString meterMenu("WSEnemyMeters");
    UI* pUI = UI::Get();
    if (!pUI)
        return;

    if (!pUI->GetMenuOpen(meterMenu))
    {
        // The meter is not part of the HUD until something has shown it, so a closed one is asked to show.
        // kShow with no data is how the game opens a menu, and it allocates nothing from the factory.
        // If the menu comes up, the next tick finds it open and posts the real target.
        if (aHandle == 0)
            return;

        static std::chrono::steady_clock::time_point s_nextShow{};
        const auto now = std::chrono::steady_clock::now();
        if (now < s_nextShow)
            return;
        s_nextShow = now + std::chrono::seconds(2);

        spdlog::info("Enemy meter: WSEnemyMeters is not open, asking the UI to show it");
        s_sendingEnemyMeter = true;
        UIMessageQueue__AddMessage_Real(pQueue, &meterMenu, UIMessage::kShow, nullptr);
        s_sendingEnemyMeter = false;
        return;
    }

    BSFixedString dataName("HUDData");
    void* pData = TiltedPhoques::ThisCall(s_createData, pQueue, &dataName);
    if (!pData)
        return;
    auto* pBytes = static_cast<uint8_t*>(pData);
    *reinterpret_cast<uint32_t*>(pBytes + 0x10) = 0xB;
    *reinterpret_cast<uint16_t*>(pBytes + 0x20) = aLevel;
    *reinterpret_cast<uint16_t*>(pBytes + 0x22) = 0x0101;
    *reinterpret_cast<uint32_t*>(pBytes + 0x28) = aHandle;

    s_sendingEnemyMeter = true;
    UIMessageQueue__AddMessage_Real(pQueue, &meterMenu, UIMessage::kUpdate, pData);
    s_sendingEnemyMeter = false;
#endif
}

std::chrono::steady_clock::time_point UI::LastGameEnemyMeterTargetAt()
{
    return s_lastGameEnemyMeterTargetAt;
}

static TiltedPhoques::Initializer s_s(
    []()
    {
        // Offsets inside the patched functions differ between builds.
        // The VR ones were checked in the VR code: the AddToActiveQueue call, the intro movie branch, and FavoritesHandler::CanProcess.
#ifdef SKYRIMVR
        constexpr size_t cAddToActiveQueueCall = 0x70C;
        constexpr size_t cStartupMovieBranch = 0x96;
#else
        constexpr size_t cAddToActiveQueueCall = 0x682;
        constexpr size_t cStartupMovieBranch = 0xFE;
#endif

        // Menus that don't pause the game while connected (see kAllowList).
        VersionDbPtr<uint8_t> ProcessHook(82082);
        TiltedPhoques::SwapCall(ProcessHook.Get() + cAddToActiveQueueCall, UI_AddToActiveQueue, &UI_AddToActiveQueue_Hook);

        // Ignore startup movie
        // TODO: Move me later.
        VersionDbPtr<uint8_t> MainInit(36548);
        TiltedPhoques::Put<uint8_t>(MainInit.Get() + cStartupMovieBranch, 0xEB);

        // Credits to Skyrim Souls RE for this fix.
        // Allows the favorites menu to be numbered during connect.
        VersionDbPtr<uint8_t> FavoritesCanProcess(51538);
        TiltedPhoques::Put<uint16_t>(FavoritesCanProcess.Get() + 0x15, 0x9090);

#ifdef SKYRIMVR
        {
            const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            TiltedPhoques::SwapCall(reinterpret_cast<uint8_t*>(exe + kMeleeSwingMenuCheck), RealMeleeSwingMenuCheck, &HookMeleeSwingMenuCheck);
            RealPlayerControlsCanProcess = reinterpret_cast<TPlayerControlsCanProcess*>(exe + kPlayerControlsCanProcess);
            TP_HOOK(&RealPlayerControlsCanProcess, HookPlayerControlsCanProcess);

            auto* pSleepWaitCheck = reinterpret_cast<uint8_t*>(exe + kSleepWaitMainThreadCheck);
            if (std::memcmp(pSleepWaitCheck, kSleepWaitMainThreadJump, sizeof(kSleepWaitMainThreadJump)) == 0)
                TiltedPhoques::Nop(pSleepWaitCheck, sizeof(kSleepWaitMainThreadJump));
            else
                spdlog::error("Sleep/Wait menu: {} is not the main thread check, leaving it alone; the menu ignores input while unpaused", fmt::ptr(pSleepWaitCheck));
            RealSleepWaitOk = reinterpret_cast<TSleepWaitOk*>(exe + kSleepWaitOkCallback);
            TP_HOOK(&RealSleepWaitOk, HookSleepWaitOk);
            RealSleepWaitProcessMessage = reinterpret_cast<TSleepWaitProcessMessage*>(exe + kSleepWaitProcessMessage);
            TP_HOOK(&RealSleepWaitProcessMessage, HookSleepWaitProcessMessage);

            for (const uintptr_t cCall : kPlayerSleepWaitCalls)
            {
                auto* pCall = reinterpret_cast<uint8_t*>(exe + cCall);
                if (*pCall == 0xE8 && pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1) == reinterpret_cast<uint8_t*>(exe + kSleepWaitStep))
                    TiltedPhoques::SwapCall(pCall, RealPlayerSleepWaitStep, &HookPlayerSleepWaitStep);
                else
                    spdlog::error("Sleep/Wait menu: {} is not the player sleep step call, leaving it alone; unpaused sleep may run too fast", fmt::ptr(pCall));
            }
        }
#endif

        // Some experiments:
#ifdef SKYRIMVR
        {
            POINTER_SKYRIMSE(TCallback, s_addMessage, 13530, 13631);
            if (s_addMessage.Get())
            {
                UIMessageQueue__AddMessage_Real = s_addMessage.Get();
                TP_HOOK(&UIMessageQueue__AddMessage_Real, UIMessageQueue__AddMessage);
                spdlog::info("UI message queue hooked for the message box diagnostic");
            }
            else
                spdlog::warn("UI message queue: id 13631 not resolved, message box diagnostic off");
        }
#endif

        // This kills the loading spinner
        // TiltedPhoques::Put<uint8_t>(0x1405D51C1, 0xEB);
        // TiltedPhoques::Nop(0x1405D51A2, 5);

        // use 8 threads by default!
        // TiltedPhoques::Put<uint8_t>(0x141E45770, 8);
    });
