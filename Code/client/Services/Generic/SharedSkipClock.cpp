#include <Services/SharedSkipClock.h>

#include <algorithm>

/**
 * Start a skip: remember the clock this player saw when every player agreed.
 *
 * @param acPersonalClock The date and hour this game showed, with the server's time scale.
 * @param aDaysPassed The game's GameDaysPassed then.
 * @param aHours The agreed length, in game hours.
 * @param aNowMs The game's time now, in milliseconds.
 */
void SharedSkipClock::Begin(const TimeModel& acPersonalClock, const float aDaysPassed, const float aHours, const uint64_t aNowMs) noexcept
{
    m_running = true;
    m_start = acPersonalClock;
    m_startDaysPassed = aDaysPassed;
    m_hours = aHours;
    m_startMs = aNowMs;
}

/**
 * Tell whether a skip runs: begun and not ended yet.
 *
 * @return True while it runs.
 */
bool SharedSkipClock::IsRunning() const noexcept
{
    return m_running;
}

/**
 * Tell whether a running skip has kept the game's own clock for too long.
 *
 * @param aNowMs The game's time now, in milliseconds.
 * @return True once TimeSkipHours::kLongestRestMs has passed since it began; false while the game's clock is behind the start.
 */
bool SharedSkipClock::HasRunTooLong(const uint64_t aNowMs) const noexcept
{
    // Unlike the server's clock, the game's follows the server's and can step back a little when they resync, which must not end a rest early.
    return m_running && aNowMs >= m_startMs && aNowMs - m_startMs >= TimeSkipHours::kLongestRestMs;
}

/**
 * End the skip and work out the clock after it.
 *
 * @param aDaysPassedNow The game's GameDaysPassed now, after its own rest.
 * @param aNowMs The game's time now, in milliseconds.
 * @return The date and hour, and the days passed, the game takes back.
 */
SharedSkipClock::Result SharedSkipClock::End(const float aDaysPassedNow, const uint64_t aNowMs) noexcept
{
    m_running = false;

    const float cCoveredHours = CoveredHours(aNowMs);
    DateTime clock(m_start);
    clock.AdvanceHours(cCoveredHours);

    Result result{};
    result.PersonalClock = clock.m_timeModel;
    result.DaysPassed = std::max(aDaysPassedNow, m_startDaysPassed + cCoveredHours / DateTime::kHoursPerDay);
    return result;
}

/**
 * Drop a running skip without any clock, as when the connection is lost.
 */
void SharedSkipClock::Cancel() noexcept
{
    m_running = false;
}

/**
 * Work out the game hours the skip has covered so far: its length and the real time since it began.
 *
 * @param aNowMs The game's time now, in milliseconds.
 * @return The hours; the length alone when the time went back.
 */
float SharedSkipClock::CoveredHours(const uint64_t aNowMs) const noexcept
{
    if (aNowMs < m_startMs)
        return m_hours;

    return m_hours + DateTime(m_start).GetDeltaTime(aNowMs - m_startMs);
}
