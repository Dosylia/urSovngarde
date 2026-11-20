#pragma once

#include <chrono>

/**
 * Lets something happen at most once in each interval, such as sending an update to the server.
 * The first turn always passes.
 * Header-only, without the game, so that the tests can check it.
 */
class Throttle
{
public:
    using Clock = std::chrono::steady_clock;

    /**
     * Make a throttle.
     *
     * @param aEvery The shortest time between two turns.
     */
    explicit constexpr Throttle(const Clock::duration aEvery) noexcept
        : m_every(aEvery)
    {
    }

    /**
     * Take a turn when the interval has passed since the last one taken, and start a new interval from now.
     *
     * @param aNow The time now; the clock's own when not given.
     * @return True when it may happen now, false while the interval has not passed yet.
     */
    bool TakeTurn(const Clock::time_point aNow = Clock::now()) noexcept
    {
        if (aNow - m_last < m_every)
            return false;
        m_last = aNow;
        return true;
    }

private:
    Clock::duration m_every;
    Clock::time_point m_last{};
};
