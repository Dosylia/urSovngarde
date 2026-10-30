#include <Interface/Menus/MapMenu.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Memory.h>
#include <PatchCheck.h>
#include <PlayerCharacter.h>

#include <intrin.h>

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
constexpr uint32_t kHidden = 0x1;

TP_THIS_FUNCTION(TMapProcessMessage, uint32_t, IMenu, UIMessage&);
TP_THIS_FUNCTION(TMainUpdateSky, void, void);
static TMapProcessMessage* RealMapProcessMessage;
static TMainUpdateSky* RealMainUpdateSky;
// Written by the thread that processes UI messages, read from the client's update on the VM worker pool.
static std::atomic<bool> s_mapOpen{false};
static std::atomic<bool> s_closeRequested{false};
// The map's model this code hid at an unpaused close; only that model is shown again.
static std::atomic<uint8_t*> s_modelHiddenOnClose{nullptr};
// Set once every hooked or called function was found as read; until then the map pauses as in the game.
static bool s_hooksReady = false;

bool MapMenuCanRunUnpaused() noexcept
{
    return s_hooksReady;
}

bool MapMenuShown() noexcept
{
    return s_mapOpen.load(std::memory_order_relaxed);
}

static std::atomic<uint64_t> s_lastHideFrame{0};

uint64_t MapMenuLastHideFrame() noexcept
{
    return s_lastHideFrame.load(std::memory_order_relaxed);
}

// The map is open, and was opened unpaused (UI.cpp records it at the opening).
static bool IsMapOpenUnpaused() noexcept
{
    static const BSFixedString s_mapName("MapMenu");
    return s_mapOpen && IsMenuOpenUnpaused(s_mapName);
}

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

    // Which thread shows and takes down the map against the one that draws it: the hide frees the map's 3D.
    if (aMessage.eType == UIMessage::kShow || cHiding)
    {
        const uint32_t cThread = GetCurrentThreadId();
        const uint32_t cMain = GameMainThreadId();
        spdlog::info("Map menu: {} handled on thread {} ({}), the map {}, frame {}", aMessage.eType == UIMessage::kShow ? "show" : "hide", cThread,
                     cThread == cMain ? "the main thread" : fmt::format("not the main thread {}", cMain), apThis->PausesGame() ? "pausing" : "unpaused",
                     MainLoopFrame());
    }

    if (aMessage.eType == UIMessage::kShow)
        s_mapOpen = true;
    else if (cHiding)
    {
        s_mapOpen = false;
        s_lastHideFrame.store(MainLoopFrame(), std::memory_order_relaxed);
    }

    // A request that arrived as the map was going down must not shut the next one the instant it opens.
    if (aMessage.eType == UIMessage::kShow || cHiding)
        s_closeRequested.store(false, std::memory_order_relaxed);

    // Cleared on the way out, so the cached model does not stay exempt from the fade that removes it.
    // Before the game handles the hide, while the model is surely there.
    if (cHiding)
    {
        if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kMenuModel))
        {
            auto* pFlags = reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags);
            *pFlags &= ~kIgnoreFade;

            // The world keeps running after an unpaused close, so the panels would fade out slowly from the fade held at 1 below.
            // Hidden at once instead; the next show takes the flag off again.
            if (!apThis->PausesGame() && !(*pFlags & kHidden))
            {
                *pFlags |= kHidden;
                s_modelHiddenOnClose.store(pModel, std::memory_order_relaxed);
            }
        }
    }

    // Taken off before the game shows the map, so its show finds the model as it was.
    if (aMessage.eType == UIMessage::kShow)
    {
        auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kMenuModel);
        if (pModel && s_modelHiddenOnClose.exchange(nullptr, std::memory_order_relaxed) == pModel)
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) &= ~kHidden;
    }

    const uint32_t result = TiltedPhoques::ThisCall(RealMapProcessMessage, apThis, aMessage);

    if (cHiding)
        spdlog::info("Map menu: hide done on thread {}, frame {}", GetCurrentThreadId(), MainLoopFrame());

    // Or after, when the model is only back in place once shown.
    if (aMessage.eType == UIMessage::kShow)
    {
        auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kMenuModel);
        if (pModel && s_modelHiddenOnClose.exchange(nullptr, std::memory_order_relaxed) == pModel)
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) &= ~kHidden;
    }

    // Set while open unpaused.
    if (s_mapOpen && !apThis->PausesGame())
    {
        if (auto* pModel = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(apThis) + kMenuModel))
        {
            *reinterpret_cast<float*>(pModel + kFadeNodeCurrentFade) = 1.f;
            *reinterpret_cast<uint32_t*>(pModel + kAvObjectFlags) |= kIgnoreFade;
        }
    }

    return result;
}

static void HookMainUpdateSky(void* apMain)
{
    if (IsMapOpenUnpaused())
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
    if (*reinterpret_cast<const uint8_t*>(exe + kMainPausedByte) == 0 && IsMapOpenUnpaused())
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
    if (*pParity != parityBefore && *reinterpret_cast<const uint8_t*>(exe + kMainPausedByte) == 0 && IsMapOpenUnpaused())
        *pParity = parityBefore;
}

// The cull job (VR 0x141322390) culls the world's roots when the world is drawn, then the extra roots, both through 0x141315B40.
// Both go on one thread into one culler, the roots array at args+0x50.
// While the map is open the world is left out and the player's world node (player+0x3F0) is one of the extra roots.
// The extra roots are listed at the frame's start (VR 0x1405BFC90, while UI::IsMenuOpen("MapMenu")), but the world's part is decided in the job.
// An unpaused hide, on a UI worker, gives the world back in between, and that job culls the node twice: its passes go on one list twice.
// So when the job's world cull reaches the node, its extra roots are culled from a copy without it.
constexpr uintptr_t kCullRoots = 0x1315B40;
constexpr uintptr_t kWorldCullReturn = 0x1322468;
constexpr uintptr_t kExtraCullReturn = 0x13224A0;
constexpr uintptr_t kExtraCullRoots = 0x3485AD8;
constexpr size_t kCullArgsRoots = 0x50;
constexpr size_t kPlayerWorldNode = 0x3F0;
constexpr size_t kObjectParent = 0x30;

// A BSTArray of node pointers, as the cull reads it.
struct NodeArray
{
    const uint8_t** pData;
    uint32_t Capacity;
    uint32_t Pad;
    uint32_t Size;
    uint32_t Pad2;
};

using TCullRoots = uint64_t(uint8_t*, uint32_t, uint32_t);
static TCullRoots* RealCullRoots;
static uintptr_t s_exe = 0;

// The job's world cull, as the extra roots' cull of the same job finds it.
static thread_local const uint8_t* t_worldCullArgs = nullptr;
static thread_local const NodeArray* t_worldRoots = nullptr;
static thread_local bool t_worldReachedNode = false;

static std::atomic<uint32_t> s_nodeLeftOut{0};

static const uint8_t* PlayerWorldNode() noexcept
{
    const auto* pPlayer = reinterpret_cast<const uint8_t*>(PlayerCharacter::Get());
    return pPlayer ? *reinterpret_cast<const uint8_t* const*>(pPlayer + kPlayerWorldNode) : nullptr;
}

// A cull from these roots reaches the node when one of them is the node or above it, with nothing hidden on the way.
static bool Reaches(const NodeArray* apRoots, const uint8_t* apNode) noexcept
{
    if (!apRoots || !apNode)
        return false;
    for (const uint8_t* pNode = apNode; pNode; pNode = *reinterpret_cast<const uint8_t* const*>(pNode + kObjectParent))
    {
        if (*reinterpret_cast<const uint32_t*>(pNode + kAvObjectFlags) & kHidden)
            return false;
        for (uint32_t i = 0; i < apRoots->Size; ++i)
            if (apRoots->pData[i] == pNode)
                return true;
    }
    return false;
}

static uint64_t HookCullRoots(uint8_t* apArgs, uint32_t aMode, uint32_t aFlags)
{
    const uintptr_t cReturn = reinterpret_cast<uintptr_t>(_ReturnAddress()) - s_exe;
    auto** ppRoots = reinterpret_cast<const NodeArray**>(apArgs + kCullArgsRoots);

    if (cReturn == kWorldCullReturn)
    {
        // Before and after: the hide may give the node back while the world is culled.
        const uint8_t* pNode = PlayerWorldNode();
        bool reached = Reaches(*ppRoots, pNode);
        const uint64_t cResult = RealCullRoots(apArgs, aMode, aFlags);
        reached = reached || Reaches(*ppRoots, pNode);
        t_worldCullArgs = apArgs;
        t_worldRoots = *ppRoots;
        t_worldReachedNode = reached;
        return cResult;
    }

    if (cReturn != kExtraCullReturn)
        return RealCullRoots(apArgs, aMode, aFlags);

    // Only after the world's cull of this same job.
    const bool cAfterWorldCull = std::exchange(t_worldCullArgs, nullptr) == apArgs;
    const uint8_t* pNode = PlayerWorldNode();
    const NodeArray* pExtraRoots = *ppRoots;
    if (!cAfterWorldCull || !pNode || reinterpret_cast<uintptr_t>(pExtraRoots) != s_exe + kExtraCullRoots ||
        !(t_worldReachedNode || Reaches(t_worldRoots, pNode)))
        return RealCullRoots(apArgs, aMode, aFlags);

    static thread_local const uint8_t* t_rootsCopy[64];
    NodeArray copy{t_rootsCopy, static_cast<uint32_t>(std::size(t_rootsCopy))};
    bool found = false;
    for (uint32_t i = 0; i < pExtraRoots->Size && copy.Size < copy.Capacity; ++i)
    {
        if (pExtraRoots->pData[i] == pNode)
            found = true;
        else
            t_rootsCopy[copy.Size++] = pExtraRoots->pData[i];
    }
    if (!found || pExtraRoots->Size > copy.Capacity)
        return RealCullRoots(apArgs, aMode, aFlags);

    // Every one counts; the first twenty are told, then one in five hundred.
    const uint32_t cCount = ++s_nodeLeftOut;
    if (cCount <= 20 || cCount % 500 == 0)
        spdlog::info("Map menu: the player's world node left out of the extra cull roots in frame {}: the world's cull of that job reached it (number {}, the map {})",
                     MainLoopFrame(), cCount, s_mapOpen ? "shown" : "not shown");

    *ppRoots = &copy;
    const uint64_t cResult = RealCullRoots(apArgs, aMode, aFlags);
    *ppRoots = pExtraRoots;
    return cResult;
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

        // Every function hooked or called here starts as read from SkyrimVR 1.4.15; on other bytes the map pauses as in the game.
        bool allFound = true;
        const auto expect = [&](uintptr_t aOffset, std::initializer_list<uint8_t> acExpected, const char* acpWhat)
        {
            const auto* pSite = reinterpret_cast<const uint8_t*>(exe + aOffset);
            if (PatchCheck::Is(pSite, acExpected))
                return;
            spdlog::error("Map menu: {} at {} is not the expected code ({}); the map keeps pausing", acpWhat, fmt::ptr(pSite), PatchCheck::Bytes(pSite, acExpected.size()));
            allFound = false;
        };
        expect(kMapProcessMessage, {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57}, "the map's ProcessMessage");
        expect(kMainUpdateSkyJob, {0x80, 0x79, 0x0E, 0x00, 0x75, 0x20}, "the main loop's sky job");
        expect(kPlayerMapModeUpdate, {0x48, 0x8B, 0xC4, 0x53, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00}, "the player's map mode update");
        expect(kMapPointerUpdate, {0x48, 0x8B, 0xC4, 0x55, 0x57, 0x41, 0x56}, "the map's laser update");
        expect(kMapWorldUpdate, {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05}, "the map's world update");
        if (!allFound)
            return;

        RealMapProcessMessage = reinterpret_cast<TMapProcessMessage*>(exe + kMapProcessMessage);
        TP_HOOK(&RealMapProcessMessage, HookMapProcessMessage);
        RealMainUpdateSky = reinterpret_cast<TMainUpdateSky*>(exe + kMainUpdateSkyJob);
        TP_HOOK(&RealMainUpdateSky, HookMainUpdateSky);
        MapPointerUpdate = reinterpret_cast<TMapPointerUpdate*>(exe + kMapPointerUpdate);
        RealPlayerMapModeUpdate = reinterpret_cast<TPlayerMapModeUpdate*>(exe + kPlayerMapModeUpdate);
        TP_HOOK(&RealPlayerMapModeUpdate, HookPlayerMapModeUpdate);
        RealMapWorldUpdate = reinterpret_cast<TMapWorldUpdate*>(exe + kMapWorldUpdate);
        TP_HOOK(&RealMapWorldUpdate, HookMapWorldUpdate);

        s_hooksReady = true;
        spdlog::info("Map menu: code found; it pauses as in the game until a server unpauses it");

        // Its two calls in the cull job are checked too, since the hook tells them apart by where they return.
        s_exe = exe;
        const auto* pCullRoots = reinterpret_cast<const uint8_t*>(exe + kCullRoots);
        const auto cCallsCullRoots = [&](const uintptr_t aReturn)
        {
            const auto* pCall = reinterpret_cast<const uint8_t*>(exe + aReturn - 5);
            return *pCall == 0xE8 && reinterpret_cast<uintptr_t>(pCall + 5 + *reinterpret_cast<const int32_t*>(pCall + 1)) == exe + kCullRoots;
        };
        if (PatchCheck::Is(pCullRoots, {0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55}) && cCallsCullRoots(kWorldCullReturn) && cCallsCullRoots(kExtraCullReturn))
        {
            RealCullRoots = reinterpret_cast<TCullRoots*>(exe + kCullRoots);
            TP_HOOK(&RealCullRoots, HookCullRoots);
            spdlog::info("Map menu: the player's world node is culled once a job, from the world or as an extra root");
        }
        else
            spdlog::error("Map menu: the cull of roots at {} or its calls in the cull job are not the expected code ({}); left as they are", fmt::ptr(pCullRoots),
                          PatchCheck::Bytes(pCullRoots, 9));
#endif
    });
