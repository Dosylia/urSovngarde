#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <Messages/RequestHourMove.h>
#include <Messages/RequestCombatState.h>
#include <Messages/NotifyHourMove.h>

#include <HourMove.h>

#include "MessageRoundTrip.h"

#include <limits>

TEST_CASE("an hour move ask keeps its hour", "[hour_move]")
{
    RequestHourMove request{};
    request.TargetHour = 6.5f;

    REQUIRE(RoundTrip(request) == request);
}

TEST_CASE("a combat report keeps whether the player fights", "[hour_move]")
{
    RequestCombatState report{};
    report.InCombat = GENERATE(true, false);

    REQUIRE(RoundTrip(report) == report);
}

TEST_CASE("an hour move answer keeps its event, player and hours", "[hour_move]")
{
    NotifyHourMove answer{};
    answer.MoveEvent = GENERATE(NotifyHourMove::Event::kMoved, NotifyHourMove::Event::kAlreadyThere, NotifyHourMove::Event::kRefusedDisabled,
                                NotifyHourMove::Event::kRefusedPublic, NotifyHourMove::Event::kRefusedCombat, NotifyHourMove::Event::kRefusedBusy,
                                NotifyHourMove::Event::kRefusedNoParty);
    answer.PlayerId = 0xFFFFFFFFu;
    answer.Hours = 7.25f;

    REQUIRE(RoundTrip(answer) == answer);
}

TEST_CASE("the clock moves forward to a later hour of the same day", "[hour_move]")
{
    REQUIRE(HourMove::HoursUntil(8.f, 20.f) == Approx(12.f));
}

TEST_CASE("an earlier hour is reached the next day, never by going back", "[hour_move]")
{
    REQUIRE(HourMove::HoursUntil(20.f, 8.f) == Approx(12.f));
    REQUIRE(HourMove::HoursUntil(23.5f, 0.5f) == Approx(1.f));
}

TEST_CASE("the hour it is already moves nothing", "[hour_move]")
{
    REQUIRE_FALSE(HourMove::HoursUntil(10.f, 10.f).has_value());
}

TEST_CASE("an hour passed by up to six game minutes counts as reached", "[hour_move]")
{
    // 5.4 game minutes past ten.
    REQUIRE_FALSE(HourMove::HoursUntil(10.09f, 10.f).has_value());
}

TEST_CASE("an hour passed by more than six game minutes is a day away", "[hour_move]")
{
    // 12 game minutes past ten: the next ten o'clock, 23.8 hours on.
    REQUIRE(HourMove::HoursUntil(10.2f, 10.f) == Approx(23.8f));
}

TEST_CASE("a move of less than a game minute moves nothing", "[hour_move]")
{
    // 0.6 game minutes ahead, then 1.2.
    REQUIRE_FALSE(HourMove::HoursUntil(10.f, 10.01f).has_value());
    // The two days added before the remainder leave float hours exact to a few millionths, a hundredth of a real second.
    REQUIRE(HourMove::HoursUntil(10.f, 10.02f) == Approx(0.02f).margin(0.0001f));
}

TEST_CASE("midnight asked as 24 is midnight asked as 0", "[hour_move]")
{
    REQUIRE(HourMove::HoursUntil(22.f, 24.f) == Approx(2.f));
    REQUIRE(HourMove::HoursUntil(22.f, 0.f) == Approx(2.f));
}

TEST_CASE("a broken hour moves nothing", "[hour_move]")
{
    REQUIRE_FALSE(HourMove::HoursUntil(10.f, std::numeric_limits<float>::quiet_NaN()).has_value());
    REQUIRE_FALSE(HourMove::HoursUntil(std::numeric_limits<float>::infinity(), 10.f).has_value());
}
