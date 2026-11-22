#pragma once

#include <DateTime.h>
#include <TimeSkipHours.h>

#include <cstdint>

/**
 * This game's clock across a shared time skip: the game keeps its own clock while it rests, and afterwards the date and the days passed are those of the whole skip.
 * An interrupted rest still counts in full, as the shared clock has moved in full.
 * Plain values in and out, so the unit tests check it without the game (CalendarService applies it).
 */
class SharedSkipClock
{
public:
    /** The clock once the skip is over. */
    struct Result
    {
        TimeModel PersonalClock{}; // this player's date and hour: the start, moved by the skip and by the real time it took
        float DaysPassed{};        // GameDaysPassed: never less than the start plus the skip
    };

    /**
     * Start a skip: remember the clock this player saw when every player agreed.
     *
     * @param acPersonalClock The date and hour this game showed, with the server's time scale.
     * @param aDaysPassed The game's GameDaysPassed then.
     * @param aHours The agreed length, in game hours.
     * @param aNowMs The game's time now, in milliseconds.
     */
    void Begin(const TimeModel& acPersonalClock, float aDaysPassed, float aHours, uint64_t aNowMs) noexcept;

    /**
     * Tell whether a skip runs: begun and not ended yet.
     *
     * @return True while it runs.
     */
    bool IsRunning() const noexcept;

    /**
     * Tell whether a running skip has kept the game's own clock for too long.
     *
     * @param aNowMs The game's time now, in milliseconds.
     * @return True once TimeSkipHours::kLongestRestMs has passed since it began; false while the game's clock is behind the start.
     */
    bool HasRunTooLong(uint64_t aNowMs) const noexcept;

    /**
     * End the skip and work out the clock after it.
     *
     * @param aDaysPassedNow The game's GameDaysPassed now, after its own rest.
     * @param aNowMs The game's time now, in milliseconds.
     * @return The date and hour, and the days passed, the game takes back.
     */
    Result End(float aDaysPassedNow, uint64_t aNowMs) noexcept;

    /**
     * Drop a running skip without any clock, as when the connection is lost.
     */
    void Cancel() noexcept;

private:
    /**
     * Work out the game hours the skip has covered so far: its length and the real time since it began.
     *
     * @param aNowMs The game's time now, in milliseconds.
     * @return The hours; the length alone when the time went back.
     */
    float CoveredHours(uint64_t aNowMs) const noexcept;

    bool m_running = false;
    TimeModel m_start{};          // the clock this player saw at the agreement
    float m_startDaysPassed = 0.f; // GameDaysPassed then
    float m_hours = 0.f;          // the agreed length, in game hours
    uint64_t m_startMs = 0;       // when it began, in the game's milliseconds
};
