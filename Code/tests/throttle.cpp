// Something allowed at most once per interval, such as an update sent to the server (client/Helpers/Throttle.h).

#include <catch2/catch.hpp>

#include <Helpers/Throttle.h>

using namespace std::chrono_literals;

TEST_CASE("The first turn always passes", "[throttle]")
{
    Throttle throttle(250ms);
    CHECK(throttle.TakeTurn(Throttle::Clock::time_point(1h)));
}

TEST_CASE("A turn inside the interval is refused, and the next one passes once it is over", "[throttle]")
{
    const Throttle::Clock::time_point cStart(1h);
    Throttle throttle(250ms);
    REQUIRE(throttle.TakeTurn(cStart));

    CHECK_FALSE(throttle.TakeTurn(cStart + 100ms));
    CHECK_FALSE(throttle.TakeTurn(cStart + 249ms));
    CHECK(throttle.TakeTurn(cStart + 250ms));
}

TEST_CASE("The interval runs from the last turn taken, not from a refused one", "[throttle]")
{
    const Throttle::Clock::time_point cStart(1h);
    Throttle throttle(250ms);
    REQUIRE(throttle.TakeTurn(cStart));

    // Refused at 200 ms: the interval still ends at 250 ms, not at 450 ms.
    REQUIRE_FALSE(throttle.TakeTurn(cStart + 200ms));
    CHECK(throttle.TakeTurn(cStart + 260ms));
    // A new interval from 260 ms.
    CHECK_FALSE(throttle.TakeTurn(cStart + 400ms));
    CHECK(throttle.TakeTurn(cStart + 510ms));
}
