#include <TiltedOnlinePCH.h>
#include <Interface/Menus/SleepWaitOk.h>

#include <Interface/GFxValue.h>
#include <Interface/IMenu.h>
#include <Interface/UI.h>
#include <Services/LocalSleepVote.h>
#include <PlayerCharacter.h>
#include <World.h>

namespace
{
// FxDelegateArgs, the arguments the game hands a menu's Scaleform callback, laid out alike on flat and VR.
constexpr size_t kArgsSize = 0x40;
constexpr size_t kArgsMenuOffset = 0x18;       // the menu that runs the callback
constexpr size_t kArgsValuesOffset = 0x28;     // the array of GFxValue arguments
constexpr size_t kArgsValueCountOffset = 0x30; // how many there are

using TOk = void(void*);
TOk* RealOk = nullptr;

LocalSleepVote s_localVote;
// Set once the main loop presses the agreed OK; read by the OK hook.
std::atomic<bool> s_holdEnabled{false};

/**
 * Read the menu out of the OK callback's arguments.
 *
 * @param apArgs The callback's FxDelegateArgs.
 * @return The menu that runs the callback; null when there is none.
 */
const void* ReadArgsMenu(const void* apArgs) noexcept
{
    return *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(apArgs) + kArgsMenuOffset);
}

/**
 * Read the length the player picked out of the OK callback's arguments.
 *
 * @param apArgs The callback's FxDelegateArgs.
 * @return The length in game hours, the first argument; nothing when the arguments hold no number.
 */
std::optional<float> ReadArgsHours(const void* apArgs) noexcept
{
    const auto* pArgs = static_cast<const uint8_t*>(apArgs);
    const auto* pValues = *reinterpret_cast<const GFxValue* const*>(pArgs + kArgsValuesOffset);
    const uint32_t cValueCount = *reinterpret_cast<const uint32_t*>(pArgs + kArgsValueCountOffset);
    if (!pValues || cValueCount == 0)
        return std::nullopt;

    if (const auto cNumber = pValues[0].GetNumber())
        return static_cast<float>(*cNumber);
    return std::nullopt;
}

/**
 * Run the game's own OK for a length, as if the player had pressed it in that menu.
 * The game then fades out and starts the sleep or the wait, whichever the menu shows.
 *
 * @param apMenu The open Sleep/Wait menu.
 * @param aHours The length, in game hours.
 */
void RunRealOk(const void* apMenu, const float aHours) noexcept
{
    GFxValue hours;
    hours.SetNumber(aHours);

    alignas(16) uint8_t args[kArgsSize]{};
    *reinterpret_cast<const void**>(args + kArgsMenuOffset) = apMenu;
    *reinterpret_cast<GFxValue**>(args + kArgsValuesOffset) = &hours;
    *reinterpret_cast<uint32_t*>(args + kArgsValueCountOffset) = 1;
    RealOk(args);
}

/**
 * Hold the player's OK press as their vote while online; otherwise, run the game's own OK.
 *
 * @param apArgs The callback's FxDelegateArgs.
 */
void HookOk(void* apArgs)
{
    // Only a server that has accepted this player counts its vote, and only a main loop that presses the agreed OK can end it.
    const auto cHours = ReadArgsHours(apArgs);
    if (!World::Get().GetTransport().IsOnline() || !s_holdEnabled || !cHours)
    {
        RealOk(apArgs);
        return;
    }

    // A press during the wait the server asked for is dropped; the menu then says how long is left.
    if (s_localVote.Press(ReadArgsMenu(apArgs), *cHours, LocalSleepVote::SteadyNowMs()) == LocalSleepVote::PressResult::kNotTaken)
        spdlog::warn("SleepVote: OK for {} h not taken, a length no menu offers or a skip already under way", *cHours);
}

/**
 * Log whether the game's own OK began a sleep or a wait, as the rig and session reports read it.
 */
void LogRestStart() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->IsSleepingOrWaiting())
        spdlog::info("SleepVote: the game's own rest began");
    else
        spdlog::warn("SleepVote: the game's own OK began no rest");
}
} // namespace

/**
 * Give this game's part in the vote, shared by the OK hook, the client's update and the main loop.
 *
 * @return The one vote of this game.
 */
LocalSleepVote& SleepWaitOk::GetLocalVote() noexcept
{
    return s_localVote;
}

/**
 * Find the Sleep/Wait menu that is open now.
 *
 * @return The menu; null when none is open.
 */
const void* SleepWaitOk::FindOpenMenu() noexcept
{
    // Interned once: the name is looked up every frame.
    static const BSFixedString s_name("Sleep/Wait Menu");
    UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen(s_name) ? pUI->FindMenuByName(s_name) : nullptr;
}

/**
 * Press the game's own OK for the agreed length, once the vote has settled it.
 * Called from the main loop, on the main thread, before the frame's update.
 */
void SleepWaitOk::PressAgreedOk() noexcept
{
    if (!RealOk || s_localVote.GetPhase() != LocalSleepVote::Phase::kAgreed)
        return;

    const void* pMenu = FindOpenMenu();
    const auto cHours = s_localVote.TakeAgreed(pMenu);
    if (!cHours)
    {
        spdlog::info("SleepVote: the menu closed before the agreed skip; the clock moves here without a sleep");
        return;
    }

    spdlog::info("SleepVote: pressing the game's own OK for {} h", *cHours);
    RunRealOk(pMenu, *cHours);
    LogRestStart();
}

/**
 * Let the OK button hold presses as votes: the main loop now calls PressAgreedOk.
 * Until then the button works as in the game, so a vote can never wait on a main loop that does not run it.
 */
void SleepWaitOk::EnableHold() noexcept
{
    s_holdEnabled = true;
}

// Hooks the menu's OK at start-up, on both games.
static TiltedPhoques::Initializer s_sleepWaitOkHook(
    []()
    {
        POINTER_SKYRIMSE(TOk, s_ok, 524025, 9000017);
        RealOk = s_ok.Get();
        if (!RealOk)
        {
            spdlog::error("SleepVote: the Sleep/Wait menu's OK was not found; the button works as in the game and time is not shared");
            return;
        }

        TP_HOOK(&RealOk, HookOk);
    });
