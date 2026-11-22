#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>

#include <catch2/catch.hpp>

#include <Services/SharedSkipClock.h>

namespace
{
// Game time when a skip begins in these tests, in milliseconds.
constexpr uint64_t kStartMs = 50000;

// The first month of the year has 31 days, the second 28 (DateTime's months count from 0); the fifth is any month in the middle.
constexpr uint32_t kFirstMonth = 0;
constexpr uint32_t kSecondMonth = 1;
constexpr uint32_t kFifthMonth = 4;

/**
 * Make a clock at a date and hour, at the default time scale of 20.
 *
 * @param aDay The day of the month, from 1.
 * @param aMonth The month, from 0.
 * @param aHour The hour, from 0 to 24.
 * @return The clock.
 */
TimeModel ClockAt(const uint32_t aDay, const uint32_t aMonth, const float aHour)
{
    TimeModel clock{};
    clock.Year = 201;
    clock.Month = aMonth;
    clock.Day = aDay;
    clock.Time = aHour;
    return clock;
}
} // namespace

TEST_CASE("a skip past midnight moves the player's own date to the next day", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 23.5f), 100.f, 1.f, kStartMs);

    const auto cResult = skip.End(100.f, kStartMs);

    REQUIRE(cResult.PersonalClock.Day == 11);
    REQUIRE(cResult.PersonalClock.Month == kFifthMonth);
    REQUIRE(cResult.PersonalClock.Time == Approx(0.5f));
}

TEST_CASE("a skip past the last day of a month moves to the next month", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(31, kFirstMonth, 23.f), 100.f, 2.f, kStartMs);

    const auto cResult = skip.End(100.f, kStartMs);

    REQUIRE(cResult.PersonalClock.Day == 1);
    REQUIRE(cResult.PersonalClock.Month == kSecondMonth);
    REQUIRE(cResult.PersonalClock.Time == Approx(1.f));
}

TEST_CASE("the days passed count the whole skip, even when the rest was cut short", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 8.f), 100.f, 8.f, kStartMs);

    // The game's own rest stopped a third of the way.
    const auto cResult = skip.End(100.1f, kStartMs);

    REQUIRE(cResult.DaysPassed == Approx(100.f + 8.f / 24.f));
}

TEST_CASE("the days passed keep what the game's own rest counted beyond the skip", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 8.f), 100.f, 8.f, kStartMs);

    const auto cResult = skip.End(100.4f, kStartMs);

    REQUIRE(cResult.DaysPassed == Approx(100.4f));
}

TEST_CASE("the real time the rest took is counted at the time scale", "[shared_skip_clock]")
{
    // 9 s at a time scale of 20 is 180 game seconds, 0.05 h.
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 10.f), 100.f, 1.f, kStartMs);

    const auto cResult = skip.End(100.f, kStartMs + 9000);

    REQUIRE(cResult.PersonalClock.Time == Approx(11.05f));
    REQUIRE(cResult.DaysPassed == Approx(100.f + 1.05f / 24.f));
}

TEST_CASE("a skip runs from its start to its end", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    REQUIRE_FALSE(skip.IsRunning());

    skip.Begin(ClockAt(10, kFifthMonth, 10.f), 100.f, 1.f, kStartMs);
    REQUIRE(skip.IsRunning());

    skip.End(100.f, kStartMs);
    REQUIRE_FALSE(skip.IsRunning());
}

TEST_CASE("a rest that outlasts a minute and a half is ended", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 10.f), 100.f, 1.f, kStartMs);

    REQUIRE_FALSE(skip.HasRunTooLong(kStartMs + 89999));
    REQUIRE(skip.HasRunTooLong(kStartMs + 90000));
}

TEST_CASE("a lost connection drops the skip", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 10.f), 100.f, 1.f, kStartMs);

    skip.Cancel();
    REQUIRE_FALSE(skip.IsRunning());
    REQUIRE_FALSE(skip.HasRunTooLong(kStartMs + 90000));
}

TEST_CASE("a game clock that went back counts the skip's length alone", "[shared_skip_clock]")
{
    SharedSkipClock skip;
    skip.Begin(ClockAt(10, kFifthMonth, 10.f), 100.f, 1.f, kStartMs);

    const auto cResult = skip.End(100.f, kStartMs - 1);

    REQUIRE(cResult.PersonalClock.Time == Approx(11.f));
}
