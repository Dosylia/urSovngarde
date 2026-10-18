#include <Interface/Menus/MapMenu.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Memory.h>

// Online the map is opened from TweenMenu, which UI.cpp's kAllowList unpauses, so the frame after the map opens
// still runs the game's unpaused updates (offline that frame is already paused). Two things go wrong in it:
// - Main's sky update job (0x1405BBF30, tail-calls Sky::Update) writes the cell's fog back over the map fog,
//   so the world map renders in the interior's fog.
// - The map's 3D content (MapMenu+0x48, BSFadeNode "skyVR_map.nif", fading out between 500 and 1000 units) is
//   faded to 0, so the floating panels draw fully transparent while still taking input. The model is cached.
// Skip the sky job while the map is open (the map drives Sky::Update itself) and keep the model faded in.
// Online VR also unpauses MapMenu itself (kAllowList), which makes both apply every frame.
constexpr uintptr_t kMainUpdateSkyJob = 0x5BBF30;
constexpr uintptr_t kMapProcessMessage = 0x916D10; // MapMenu vtable slot 4
constexpr size_t kMenuModel = 0x48;
constexpr size_t kFadeNodeCurrentFade = 0x158;
constexpr size_t kAvObjectFlags = 0x10C;
constexpr uint32_t kIgnoreFade = 0x8000;

TP_THIS_FUNCTION(TMapProcessMessage, uint32_t, IMenu, UIMessage&);
TP_THIS_FUNCTION(TMainUpdateSky, void, void);
static TMapProcessMessage* RealMapProcessMessage;
static TMainUpdateSky* RealMainUpdateSky;
// Written by the thread that processes UI messages, read from the client's update on the VM worker pool.
static std::atomic<bool> s_mapOpen{false};
static std::atomic<bool> s_closeRequested{false};

// The UIMessageQueue singleton, not in the address table. All 148 callers of AddMessage (id 13631) that load
// `this` right before the call load it from this one global (SkyrimVR 1.4.15).
constexpr uintptr_t kUIMessageQueue = 0x1F850F8;

using TAddMessage = void(void*, const BSFixedString*, uint32_t, void*);

// Records the wish; the game thread carries it out. Posting from the VM worker pool deadlocked on the message
// queue's lock, the string table lock and the address lookup's static guard.
void CloseMapMenu() noexcept
{
    if (s_mapOpen)
        s_closeRequested.store(true, std::memory_order_relaxed);
}

// Posts a requested close from the player update. Exchanged, so one request posts one close.
static void RunDeferredMapClose() noexcept
{
    if (!s_mapOpen || !s_closeRequested.exchange(false, std::memory_order_relaxed))
        return;

    void* pQueue = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kUIMessageQueue);
    if (!pQueue)
        return;

    POINTER_SKYRIMSE(TAddMessage, s_addMessage, 13530, 13631);

    // Built on first use: the game's string table does not exist at static init.
    static const BSFixedString s_mapName("MapMenu");

    // The cancel the player sends, not a bare hide: a bare kHide frees the map's 3D on a UI worker while the main
    // thread draws it. BSUIMessageData read off the call site at 0x1405D0BF5: 0x28 bytes, vtable at the RVA below,
    // the user event string at +0x18, rest zero. Game heap, since the game frees it.
    constexpr uintptr_t kMessageDataVtable = 0x16C8B38;
    constexpr size_t kMessageDataSize = 0x28;
    constexpr size_t kUserEventOffset = 0x18;

    auto* pData = static_cast<uint8_t*>(Memory::Allocate(kMessageDataSize));
    if (!pData)
        return;

    std::memset(pData, 0, kMessageDataSize);
    *reinterpret_cast<uintptr_t*>(pData) = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kMessageDataVtable;

    // The game's setter, so the string is interned and refcounted.
    reinterpret_cast<BSFixedString*>(pData + kUserEventOffset)->Set("Cancel");

    s_addMessage.Get()(pQueue, &s_mapName, UIMessage::kUserEvent, pData);
}

static uint32_t HookMapProcessMessage(IMenu* apThis, UIMessage& aMessage)
{
    const bool cHiding = aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide;

    if (aMessage.eType == UIMessage::kShow)
        s_mapOpen = true;
    else if (cHiding)
        s_mapOpen = false;

    // A request that arrived as the map was going down must not shut the next one the instant it opens.
    if (aMessage.eType == UIMessage::kShow || cHiding)
        s_closeRequested.store(false, std::memory_order_relaxed);

    const uint32_t result = TiltedPhoques::ThisCall(RealMapProcessMessage, apThis, aMessage);

    // Set while open, cleared on the way out, so the cached model does not stay exempt from the fade that removes it.
    if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kMenuModel))
    {
        if (s_mapOpen && !apThis->PausesGame())
        {
            *reinterpret_cast<float*>(pModel + kFadeNodeCurrentFade) = 1.f;
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) |= kIgnoreFade;
        }
        else if (cHiding)
        {
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) &= ~kIgnoreFade;
        }
    }

    return result;
}

static void HookMainUpdateSky(void* apMain)
{
    static const BSFixedString s_mapName("MapMenu");
    if (s_mapOpen && IsMenuOpenUnpaused(s_mapName))
        return;

    TiltedPhoques::ThisCall(RealMainUpdateSky, apMain);
}

// The paused player update calls 0x1406AD080 and then the map's laser update (0x1406B8980) while MapMenu is open.
// The unpaused update calls 0x1406AD080 but skips pointer drawing while the UI quad is hidden, which the map does,
// so with the map unpaused the laser froze. Follow 0x1406AD080 with the laser update when unpaused.
constexpr uintptr_t kPlayerMapModeUpdate = 0x6AD080;
constexpr uintptr_t kMapPointerUpdate = 0x6B8980;
constexpr uintptr_t kMainPausedByte = 0x2FEB76B;

TP_THIS_FUNCTION(TPlayerMapModeUpdate, void, void, float, float);
TP_THIS_FUNCTION(TMapPointerUpdate, void, void, float);
static TPlayerMapModeUpdate* RealPlayerMapModeUpdate;
static TMapPointerUpdate* MapPointerUpdate;

static void HookPlayerMapModeUpdate(void* apPlayer, float aDelta, float aRealDelta)
{
    TiltedPhoques::ThisCall(RealPlayerMapModeUpdate, apPlayer, aDelta, aRealDelta);

    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (s_mapOpen && *reinterpret_cast<const uint8_t*>(exe + kMainPausedByte) == 0)
        TiltedPhoques::ThisCall(MapPointerUpdate, apPlayer, aDelta);

    // From the player update, inside Main::Loop, the phase the player's own cancel is posted from.
    RunDeferredMapClose();
}

// Shader controllers write a double buffer that the frame parity (0x1434231A0) selects. The main loop flips it once
// per unpaused frame, and MapMenu's per-frame update (0x14091BE70) flips it too because paused nobody else does.
// With the map unpaused both flip and the map's fog and clouds blink, so the map's flip is undone when unpaused.
constexpr uintptr_t kMapWorldUpdate = 0x91BE70;
constexpr uintptr_t kFrameParity = 0x34231A0;

using TMapWorldUpdate = void(float);
static TMapWorldUpdate* RealMapWorldUpdate;

static void HookMapWorldUpdate(float aDelta)
{
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* pParity = reinterpret_cast<uint32_t*>(exe + kFrameParity);
    const uint32_t parityBefore = *pParity;

    RealMapWorldUpdate(aDelta);

    // The flip is conditional inside the map update, so only revert one that happened.
    if (*pParity != parityBefore && *reinterpret_cast<const uint8_t*>(exe + kMainPausedByte) == 0)
        *pParity = parityBefore;
}
#endif

static TiltedPhoques::Initializer s_init(
    []() {
// Disabled because the mapmenu in first person breaks.
// I fix that later, but for now it doesn't break gameplay.
#if 0
    // https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/MapMenuEx.cpp
    // Of course this isnt perfect yet. but we"ll see

    VersionDbPtr<void*> hookLoc(53112);
    TiltedPhoques::Nop(hookLoc.GetPtrU() + 0x53, 4);
    TiltedPhoques::Nop(hookLoc.GetPtrU() + 0x9D, 2);
    TiltedPhoques::Nop(hookLoc.GetPtrU() + 0x9F, 1);
#endif

#ifdef SKYRIMVR
        const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        RealMapProcessMessage = reinterpret_cast<TMapProcessMessage*>(exe + kMapProcessMessage);
        TP_HOOK(&RealMapProcessMessage, HookMapProcessMessage);
        RealMainUpdateSky = reinterpret_cast<TMainUpdateSky*>(exe + kMainUpdateSkyJob);
        TP_HOOK(&RealMainUpdateSky, HookMainUpdateSky);
        MapPointerUpdate = reinterpret_cast<TMapPointerUpdate*>(exe + kMapPointerUpdate);
        RealPlayerMapModeUpdate = reinterpret_cast<TPlayerMapModeUpdate*>(exe + kPlayerMapModeUpdate);
        TP_HOOK(&RealPlayerMapModeUpdate, HookPlayerMapModeUpdate);
        RealMapWorldUpdate = reinterpret_cast<TMapWorldUpdate*>(exe + kMapWorldUpdate);
        TP_HOOK(&RealMapWorldUpdate, HookMapWorldUpdate);
#endif
    });
