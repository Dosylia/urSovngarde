#include <Games/Skyrim/Interface/IMenu.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Misc/BSFixedString.h>
#include <TiltedOnlinePCH.h>
#include "immersive_launcher/stubs/DllBlocklist.h"

#include <World.h>
#include <PatchCheck.h>
#include <Services/CalendarService.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <Interface/Menus/SkillsMenu.h>
#include <Interface/Menus/MapMenu.h>

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

// Whether each menu that can run unpaused was, at its last opening: kAllowList, then the Sleep/Wait menu.
// Recorded where the menu is queued (UI_AddToActiveQueue_Hook), after the decision, from its flags.
// The checks below run per input event and per pose send on several threads. Reading the menu object there meant
// walking menuMap without the UI's lock and reading flags of a menu another thread may be freeing as it closes.
static constexpr const char* kSleepWaitMenuName = "Sleep/Wait Menu";
static std::atomic<bool> s_openedUnpaused[std::size(kAllowList) + 1];

static std::atomic<bool>* OpenedUnpausedFlag(const char* acpName) noexcept
{
    if (!acpName)
        return nullptr;
    for (size_t i = 0; i < std::size(kAllowList); ++i)
        if (std::strcmp(acpName, kAllowList[i]) == 0)
            return &s_openedUnpaused[i];
    if (std::strcmp(acpName, kSleepWaitMenuName) == 0)
        return &s_openedUnpaused[std::size(kAllowList)];
    return nullptr;
}

bool IsMenuOpenUnpaused(const BSFixedString& acName) noexcept
{
    const std::atomic<bool>* pFlag = OpenedUnpausedFlag(acName.AsAscii());
    if (!pFlag || !pFlag->load())
        return false;
    // The game's own IsMenuOpen, which takes the UI's lock. A flag left from an earlier opening counts only while open.
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(acName);
}

// The menus Gameplay:bUnpausedMenus decides about; the rest of kAllowList is unpaused either way.
static constexpr const char* kOptionalUnpausedMenus[] = {"StatsMenu", "MapMenu"};

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

    // The flag first: the game's IsMenuOpen is asked only for a menu that was opened unpaused.
    for (size_t i = 0; i < s_names->size(); ++i)
    {
        if (s_openedUnpaused[i].load() && IsMenuOpenUnpaused((*s_names)[i]))
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

// The one hook on it: MinHook takes a single hook per address, and a second one is silently not installed.
// So the party sleep's control lock (Menus/SleepWaitMenu.cpp) is asked here too.
static bool HookPlayerControlsCanProcess(void* apControls)
{
    if (IsAllowListMenuOpenUnpaused() || SleepWaitBlocksControls())
        return false;

    return TiltedPhoques::ThisCall(RealPlayerControlsCanProcess, apControls);
}
#endif

static void* (*UI_AddToActiveQueue)(UI*, IMenu*, void*);

// Counted where the game queues it, on whichever thread does; read by UI::LoadingScreensQueued.
static std::atomic<uint32_t> s_loadingScreensQueued{0};

// Before the game queues it, so the flag is current from the moment the menu counts as open.
static void RecordMenuOpening(UI* apSelf, IMenu* apMenu) noexcept
{
#ifdef SKYRIMVR
    if (!apMenu)
        return;
    const BSFixedString* pName = apSelf->LookupMenuNameByInstance(apMenu);
    if (std::atomic<bool>* pFlag = OpenedUnpausedFlag(pName ? pName->AsAscii() : nullptr))
        pFlag->store(!apMenu->PausesGame());
#endif
}

static void* UI_AddToActiveQueue_Hook(UI* apSelf, IMenu* apMenu, void* apFoundItem /*In reality a reference*/)
{
    // Logs every menu the game queues, connected or not; the launcher reads the "Menu queued" lines.
    // The VR HUD widgets (WS*) are queued every frame and are left out.
    if (apMenu)
    {
        const BSFixedString* pQueuedName = apSelf->LookupMenuNameByInstance(apMenu);
        // Never one of the menus RecordMenuOpening tracks, and queued every frame: left out there too.
        if (pQueuedName && pQueuedName->AsAscii() && strncmp(pQueuedName->AsAscii(), "WS", 2) == 0)
            return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
        const BSFixedString* pName = apSelf->LookupMenuNameByInstance(apMenu);
        spdlog::info("Menu queued: {}{} (connected {})", pName ? pName->AsAscii() : "?", apMenu->PausesGame() ? " [pauses]" : "", World::Get().GetTransport().IsConnected() ? "yes" : "no");
        if (pName && pName->AsAscii() && strcmp(pName->AsAscii(), "Loading Menu") == 0)
            ++s_loadingScreensQueued;
        // A sleep fades out a couple of seconds before the clock moves; the party fades with it.
        if (pName && pName->AsAscii() && strcmp(pName->AsAscii(), "Fader Menu") == 0)
            CalendarService::OnFaderQueued();
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
    {
        RecordMenuOpening(apSelf, apMenu);
        return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
    }

#ifdef SKYRIMVR
    static const BSFixedString s_sleepWaitMenu(kSleepWaitMenuName);
    // Only where the server lets a sleep pass time: elsewhere the menu pauses as in the game, and the sleep hooks stay idle.
    if (SleepWaitCanRunUnpaused() && apSelf->FindMenuByName(s_sleepWaitMenu) == apMenu)
    {
        UnfreezeMenu(apMenu);
        RecordMenuOpening(apSelf, apMenu);
        return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
    }
#endif

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
        // Unpaused only once its checks are skipped (Menus/SkillsMenu.cpp); until then it pauses as in the game.
        if (std::strcmp(item, "StatsMenu") == 0 && !SkillsMenuCanRunUnpaused())
            continue;
        // Unpaused only with its hooks in place (Menus/MapMenu.cpp); without them it pauses as in the game.
        if (std::strcmp(item, "MapMenu") == 0 && !MapMenuCanRunUnpaused())
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

    RecordMenuOpening(apSelf, apMenu);
    return UI_AddToActiveQueue(apSelf, apMenu, apFoundItem);
}

using TCallback = void(void*, const BSFixedString*, uint32_t, void*);
static TCallback* UIMessageQueue__AddMessage_Real;

#ifdef SKYRIMVR
// Recorded by the main loop at the start of each frame, on the main thread, the one that draws.
static std::atomic<uint32_t> s_mainThreadId{0};
static std::atomic<uint64_t> s_mainLoopFrame{0};

uint32_t GameMainThreadId() noexcept
{
    return s_mainThreadId.load(std::memory_order_relaxed);
}

uint64_t MainLoopFrame() noexcept
{
    return s_mainLoopFrame.load(std::memory_order_relaxed);
}

void UnpausedMenusOnMainLoop() noexcept
{
    s_mainThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    s_mainLoopFrame.fetch_add(1, std::memory_order_relaxed);
}
#endif

static bool s_sendingEnemyMeter = false;
static std::chrono::steady_clock::time_point s_lastGameEnemyMeterTargetAt{};

void UIMessageQueue__AddMessage(void* a1, const BSFixedString* a2, UIMessage::UI_MESSAGE_TYPE a3, void* a4)
{
    // The other half of "Menu queued": a menu asked to close, so a report shows which menu was left open.
    // The VR HUD widgets (WS*) are left out, as there.
    if ((a3 == UIMessage::kHide || a3 == UIMessage::kForceHide) && a2 && a2->AsAscii() && strncmp(a2->AsAscii(), "WS", 2) != 0)
        spdlog::info("Menu closing: {}{}", a2->AsAscii(), a3 == UIMessage::kForceHide ? " (forced)" : "");


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

uint32_t UI::LoadingScreensQueued()
{
    return s_loadingScreensQueued.load();
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

        // Each patch checks the instruction it replaces first, and leaves the game alone when another one is there.

        // Menus that don't pause the game while connected (see kAllowList).
        VersionDbPtr<uint8_t> ProcessHook(82082);
        uint8_t* pAddToActiveQueue = PatchCheck::At(ProcessHook.Get(), cAddToActiveQueueCall);
        if (PatchCheck::IsCall(pAddToActiveQueue))
            TiltedPhoques::SwapCall(pAddToActiveQueue, UI_AddToActiveQueue, &UI_AddToActiveQueue_Hook);
        else
            spdlog::error("UI: {} is not the call to AddToActiveQueue ({}), leaving it alone; menus pause the game while connected",
                          fmt::ptr(pAddToActiveQueue), PatchCheck::Bytes(pAddToActiveQueue, 5));

        // Ignore startup movie
        // TODO: Move me later.
        // A conditional jump made unconditional; it already is when another plugin skips the movie too.
        VersionDbPtr<uint8_t> MainInit(36548);
        uint8_t* pStartupMovie = PatchCheck::At(MainInit.Get(), cStartupMovieBranch);
        if (PatchCheck::IsShortConditionalJump(pStartupMovie) || PatchCheck::Is(pStartupMovie, {0xEB}))
            TiltedPhoques::Put<uint8_t>(pStartupMovie, 0xEB);
        else
            spdlog::error("UI: {} is not the intro movie's branch ({}), leaving it alone; the intro movie plays",
                          fmt::ptr(pStartupMovie), PatchCheck::Bytes(pStartupMovie, 2));

        // Credits to Skyrim Souls RE for this fix.
        // Allows the favorites menu to be numbered during connect.
        // A two-byte conditional jump removed; it already is when another plugin makes the same fix.
        VersionDbPtr<uint8_t> FavoritesCanProcess(51538);
        uint8_t* pFavorites = PatchCheck::At(FavoritesCanProcess.Get(), 0x15);
        if (PatchCheck::IsShortConditionalJump(pFavorites) || PatchCheck::Is(pFavorites, {0x90, 0x90}))
            TiltedPhoques::Put<uint16_t>(pFavorites, 0x9090);
        else
            spdlog::error("UI: {} is not the favorites menu's branch ({}), leaving it alone; favorites cannot be numbered while connected",
                          fmt::ptr(pFavorites), PatchCheck::Bytes(pFavorites, 2));

#ifdef SKYRIMVR
        {
            const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            // An E8 to another plugin's code is taken too: swapped again, ours calls theirs.
            auto* pMeleeCheck = reinterpret_cast<uint8_t*>(exe + kMeleeSwingMenuCheck);
            if (PatchCheck::IsCall(pMeleeCheck))
                TiltedPhoques::SwapCall(pMeleeCheck, RealMeleeSwingMenuCheck, &HookMeleeSwingMenuCheck);
            else
                spdlog::error("UI: {} is not the melee swing's menu check call ({}), leaving it alone; swings are not cancelled in unpaused menus",
                              fmt::ptr(pMeleeCheck), PatchCheck::Bytes(pMeleeCheck, 5));
            RealPlayerControlsCanProcess = reinterpret_cast<TPlayerControlsCanProcess*>(exe + kPlayerControlsCanProcess);
            TP_HOOK(&RealPlayerControlsCanProcess, HookPlayerControlsCanProcess);
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
