#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/BSInput/GameInputHold.h>
#include <Helpers/ModuleImage.h>
#include <Helpers/PatchCheck.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace
{
// The game's queued input events, a list rebuilt at every poll; this much is laid out the same in SE, AE and VR.
struct InputEvent
{
    void* pVtable;
    uint32_t Device;
    uint32_t Type;
    InputEvent* pNext;
};

// The kinds of event this hold treats, as the game numbers them.
constexpr uint32_t kButtonEvent = 0;
constexpr uint32_t kThumbstickEvent = 3;
constexpr uint32_t kDeviceConnectEvent = 4;

// Where a button or stick event keeps its parts.
constexpr size_t kButtonCode = 0x20;
#ifdef SKYRIMVR
// A VR button event carries the controller's own data before its value.
constexpr size_t kButtonValue = 0x30;
#else
constexpr size_t kButtonValue = 0x28;
#endif
constexpr size_t kButtonHeld = kButtonValue + 4;
constexpr size_t kThumbstickX = 0x28;
constexpr size_t kThumbstickY = 0x2C;

/**
 * Reach one part of an input event, by where the game keeps it.
 *
 * @param apEvent The event.
 * @param aOffset Where the part sits, from the event's start.
 * @return The part, to read or change.
 */
template <class T> T& At(InputEvent* apEvent, const size_t aOffset) noexcept
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(apEvent) + aOffset);
}

// Whether the game's input is held back now: set from the menu, read at every input poll.
std::atomic<bool> s_held{false};

// The buttons the game was told are down, by device and button: only their releases reach it.
// The input poll runs on the main thread only.
TiltedPhoques::Vector<uint64_t> s_gameHasDown;

/**
 * Make one number that names a button on a device, from a game input event.
 * It is how the list of buttons the game has been told are down tells them apart.
 *
 * @param apEvent A button event the game is about to receive.
 * @return The device in the upper half of the number, the button's code in the lower half.
 */
uint64_t ButtonIdOf(InputEvent* apEvent) noexcept
{
    return (static_cast<uint64_t>(apEvent->Device) << 32) | At<uint32_t>(apEvent, kButtonCode);
}

/**
 * Decide whether the game gets a button event, and keep track of which buttons it has down.
 * While the menu is open it gets no press; a button pressed then stays the menu's until it is let go, release included.
 *
 * @param apEvent A button event the game is about to receive.
 * @param aHeld Whether the game's input is held back now.
 * @return True when the game gets the event.
 */
bool GameTakesButton(InputEvent* apEvent, const bool aHeld) noexcept
{
    const uint64_t cButton = ButtonIdOf(apEvent);
    const auto cKnown = std::find(s_gameHasDown.begin(), s_gameHasDown.end(), cButton);
    const bool cGameHasIt = cKnown != s_gameHasDown.end();
    if (At<float>(apEvent, kButtonValue) == 0.f)
    {
        if (cGameHasIt)
            s_gameHasDown.erase(cKnown);
        return cGameHasIt;
    }
    if (cGameHasIt)
        return !aHeld;
    // A press the game never had the start of is not given to it later, as a held one.
    if (aHeld || At<float>(apEvent, kButtonHeld) != 0.f)
        return false;
    s_gameHasDown.push_back(cButton);
    return true;
}

/**
 * Decide whether the game gets an input event, changing it when it gets it in another form.
 * Buttons follow GameTakesButton; while the input is held, sticks reach the game at rest, device connections go through, and the rest is kept back.
 *
 * @param apEvent An event the game is about to receive.
 * @param aHeld Whether the game's input is held back now.
 * @return True when the game gets the event.
 */
bool GameTakesEvent(InputEvent* apEvent, const bool aHeld) noexcept
{
    if (apEvent->Type == kButtonEvent)
        return GameTakesButton(apEvent, aHeld);
    if (!aHeld)
        return true;

    if (apEvent->Type == kThumbstickEvent)
    {
        // At rest, so a player walking as the menu opens stops.
        At<float>(apEvent, kThumbstickX) = 0.f;
        At<float>(apEvent, kThumbstickY) = 0.f;
        return true;
    }
    return apEvent->Type == kDeviceConnectEvent;
}

using TDispatch = void(void* apSource, InputEvent** appEvents);
// The game's own hand-off, called by the hook with the events the game gets.
TDispatch* s_dispatch = nullptr;

/**
 * Stand in for the input poll's call that hands this poll's events to the game's listeners.
 * The game gets only the events GameTakesEvent lets through; its list is then put back as the poll built it.
 *
 * @param apSource The game's input device manager, passed on as it is.
 * @param appEvents Where the poll keeps the start of its list of events.
 */
void HookDispatch(void* apSource, InputEvent** appEvents)
{
    if (!appEvents || !*appEvents)
    {
        s_dispatch(apSource, appEvents);
        return;
    }
    const bool cHeld = s_held.load();

    // The events in the order the poll queued them; its queue holds a few dozen at most, and any past this many go through as they are.
    std::array<InputEvent*, 128> events{};
    size_t count = 0;
    for (InputEvent* pEvent = *appEvents; pEvent && count < events.size(); pEvent = pEvent->pNext)
        events[count++] = pEvent;
    InputEvent* const pRest = events[count - 1]->pNext;

    // A new chain of the events the game gets, in the same order, ending on any past the first 128.
    InputEvent* pFirst = nullptr;
    InputEvent* pLast = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        InputEvent* pEvent = events[i];
        if (!GameTakesEvent(pEvent, cHeld))
            continue;
        if (pLast)
            pLast->pNext = pEvent;
        else
            pFirst = pEvent;
        pLast = pEvent;
    }
    if (pLast)
        pLast->pNext = pRest;
    else
        pFirst = pRest;

    s_dispatch(apSource, &pFirst);

    // The list put back as the poll built it.
    for (size_t i = 0; i + 1 < count; ++i)
        events[i]->pNext = events[i + 1];
    events[count - 1]->pNext = pRest;
}
} // namespace

namespace GameInputHold
{
/**
 * Put the hold on the game's input poll, at the call that hands each poll's events to the game.
 * Called at the first update, once the plugins that hook the same call have done so (SKSE, at input load); later calls do nothing.
 * When the game's code there is not the expected one, nothing is changed and an error is logged.
 */
void Install() noexcept
{
    static bool s_tried = false;
    if (s_tried)
        return;
    s_tried = true;

#ifdef SKYRIMVR
    const VersionDbPtr<uint8_t> cCallSite(9000015);
    uint8_t* pCall = cCallSite.Get();
#else
    const VersionDbPtr<uint8_t> cPoll(68617);
    uint8_t* pCall = cPoll.Get() ? cPoll.Get() + 0x7B : nullptr;
#endif
    // The call and the two moves before it, the same in VR and AE: mov [rsp + 0x40], rcx (the list's head, whose address is rdx); mov rcx, rsi; call.
    // A layout shifted onto another call would be handed something that is not the event list.
    if (!pCall || !PatchCheck::Is(pCall - 8, {0x48, 0x89, 0x4C, 0x24, 0x40, 0x48, 0x8B, 0xCE, 0xE8}))
    {
        spdlog::error("GameInputHold: the input poll's hand-off to the game is not the expected code ({}); the game's input is not held while the menu is open",
                      pCall ? PatchCheck::Bytes(pCall - 8, 13) : std::string("no address"));
        return;
    }
    // SKSE hooks the same call first: its hook is what this one calls, so its input sinks get what the game gets.
    s_dispatch = TiltedPhoques::GetCall<TDispatch*>(pCall);
    TiltedPhoques::PutCall(pCall, &HookDispatch);
    spdlog::info("GameInputHold: installed on the input poll's hand-off to the game, before {}", ModuleImage::NameOf(reinterpret_cast<const void*>(s_dispatch)));
}

/**
 * Hold the game's input back, or let it through again.
 *
 * @param aHeld True while the menu is open in a way that should keep the game from acting on input.
 */
void SetHeld(const bool aHeld) noexcept
{
    if (s_held.exchange(aHeld) == aHeld)
        return;
    spdlog::info(aHeld ? "GameInputHold: the game's input is held while the menu is open" : "GameInputHold: the game takes its input again; buttons pressed in the menu count once let go");
}
} // namespace GameInputHold
