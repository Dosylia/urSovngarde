#include <Services/LocalSleepVote.h>

#include <TimeSkipHours.h>

#include <chrono>
#include <utility>

/**
 * Tell where this game's vote stands.
 *
 * @return The current phase.
 */
LocalSleepVote::Phase LocalSleepVote::GetPhase()
{
    std::scoped_lock lock(m_lock);
    return m_phase;
}

/**
 * Tell whether this game's vote waits on the other players: pressed or confirmed.
 *
 * @return True in those two phases.
 */
bool LocalSleepVote::IsVoting()
{
    std::scoped_lock lock(m_lock);
    return IsVotingPhase(m_phase);
}

/**
 * Hold the menu's OK press for the vote, replacing an earlier press of another length.
 *
 * @param apMenu The Sleep/Wait menu the press came from; only compared, never read.
 * @param aHours The length the player picked, in game hours.
 * @param aNowMs This game's steady time now, in milliseconds (SteadyNowMs).
 * @return kHeld when the press is to be sent; otherwise why not, and nothing changes.
 */
LocalSleepVote::PressResult LocalSleepVote::Press(const void* apMenu, const float aHours, const uint64_t aNowMs)
{
    std::scoped_lock lock(m_lock);
    if (aNowMs < m_waitUntilMs)
    {
        m_pressDropped = true;
        return PressResult::kWaiting;
    }

    if (!TimeSkipHours::IsValid(aHours) || !(m_phase == Phase::kIdle || IsVotingPhase(m_phase)))
        return PressResult::kNotTaken;

    // OK pressed again for the same length sends nothing, so mashing it does not flood the other players.
    if (IsVotingPhase(m_phase) && apMenu == m_pMenu && aHours == m_hours)
        return PressResult::kAlreadyHeld;

    m_phase = Phase::kPressed;
    m_pMenu = apMenu;
    m_hours = aHours;
    return PressResult::kHeld;
}

/**
 * Drop every OK press until a time, as the server asked after refusing one.
 *
 * @param aUntilMs This game's steady time the wait ends, in milliseconds.
 */
void LocalSleepVote::WaitBeforePressing(const uint64_t aUntilMs)
{
    std::scoped_lock lock(m_lock);
    m_waitUntilMs = aUntilMs;
}

/**
 * Tell how long this game still drops OK presses.
 *
 * @param aNowMs This game's steady time now, in milliseconds.
 * @return The time left, in milliseconds; 0 when presses count again.
 */
uint64_t LocalSleepVote::GetWaitLeftMs(const uint64_t aNowMs)
{
    std::scoped_lock lock(m_lock);
    return aNowMs < m_waitUntilMs ? m_waitUntilMs - aNowMs : 0;
}

/**
 * Take the note that a press was dropped during the wait, so the menu can say how long is left.
 *
 * @return True when a press was dropped since the last call.
 */
bool LocalSleepVote::TakeDroppedPress()
{
    std::scoped_lock lock(m_lock);
    return std::exchange(m_pressDropped, false);
}

/**
 * Read this game's steady time, which never goes back, for the wait.
 *
 * @return The time, in milliseconds.
 */
uint64_t LocalSleepVote::SteadyNowMs() noexcept
{
    const auto cSinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(cSinceStart).count());
}

/**
 * Take a press still to be sent, which makes it confirmed.
 *
 * @return The pressed length, in game hours; nothing when no press waits.
 */
std::optional<float> LocalSleepVote::TakePress()
{
    std::scoped_lock lock(m_lock);
    if (m_phase != Phase::kPressed)
        return std::nullopt;

    m_phase = Phase::kConfirmed;
    m_confirmationSent = true;
    return m_hours;
}

/**
 * Tell whether the menu the vote was pressed in is no longer open: closed, or closed and opened again.
 *
 * @param apOpenMenu The Sleep/Wait menu open now; null when none is.
 * @return True while pressed or confirmed and the open menu is not the one pressed in.
 */
bool LocalSleepVote::IsMenuGone(const void* apOpenMenu)
{
    std::scoped_lock lock(m_lock);
    return IsVotingPhase(m_phase) && apOpenMenu != m_pMenu;
}

/**
 * Take the vote back: its menu closed, combat began, the vote ran out, or the server refused the press; a skip already under way is kept.
 *
 * @return True when a confirmation of this vote was sent, so the server may still hold it.
 */
bool LocalSleepVote::Withdraw()
{
    std::scoped_lock lock(m_lock);
    if (!IsVotingPhase(m_phase))
        return false;

    m_phase = Phase::kIdle;
    return std::exchange(m_confirmationSent, false);
}

/**
 * Settle the length every player agreed on, so the main loop presses the game's own OK.
 *
 * @param aHours The agreed length, in game hours.
 * @return False when this game holds no pressed or confirmed vote; nothing changes then.
 */
bool LocalSleepVote::Agree(const float aHours)
{
    std::scoped_lock lock(m_lock);
    if (!IsVotingPhase(m_phase) || !TimeSkipHours::IsValid(aHours))
        return false;

    m_phase = Phase::kAgreed;
    m_hours = aHours;
    m_confirmationSent = false;
    return true;
}

/**
 * Settle on this player's own pressed length, when the server does not share the time.
 *
 * @return False when this game holds no pressed or confirmed vote; nothing changes then.
 */
bool LocalSleepVote::AgreeAlone()
{
    std::scoped_lock lock(m_lock);
    if (!IsVotingPhase(m_phase))
        return false;

    m_phase = Phase::kAgreed;
    m_confirmationSent = false;
    return true;
}

/**
 * Take the agreed length for the game's own OK, which makes the game resting.
 * When the menu pressed in is gone, the vote is dropped instead.
 *
 * @param apOpenMenu The Sleep/Wait menu open now; null when none is.
 * @return The agreed length, in game hours; nothing when none is agreed or the menu is gone.
 */
std::optional<float> LocalSleepVote::TakeAgreed(const void* apOpenMenu)
{
    std::scoped_lock lock(m_lock);
    if (m_phase != Phase::kAgreed)
        return std::nullopt;

    if (!apOpenMenu || apOpenMenu != m_pMenu)
    {
        m_phase = Phase::kIdle;
        return std::nullopt;
    }

    m_phase = Phase::kResting;
    return m_hours;
}

/**
 * Note that the game no longer sleeps or waits.
 *
 * @return True when the game was resting from the vote, so the server must be told.
 */
bool LocalSleepVote::FinishResting()
{
    std::scoped_lock lock(m_lock);
    if (m_phase != Phase::kResting)
        return false;

    m_phase = Phase::kIdle;
    return true;
}

/**
 * Drop everything whatever the phase, the wait included, after the connection was lost.
 */
void LocalSleepVote::Reset()
{
    std::scoped_lock lock(m_lock);
    m_phase = Phase::kIdle;
    m_confirmationSent = false;
    m_waitUntilMs = 0;
    m_pressDropped = false;
}

/**
 * Tell whether a phase waits on the other players: pressed or confirmed.
 *
 * @param aPhase The phase.
 * @return True for those two phases.
 */
bool LocalSleepVote::IsVotingPhase(const Phase aPhase) noexcept
{
    return aPhase == Phase::kPressed || aPhase == Phase::kConfirmed;
}
