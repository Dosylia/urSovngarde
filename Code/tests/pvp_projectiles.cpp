#include <catch2/catch.hpp>

#include <Services/PvpProjectiles.h>

using namespace std::chrono_literals;
using PvpProjectiles::FlightFrom;

TEST_CASE("next to the shooter a shot is over almost at once", "[pvpprojectiles]")
{
    // A sword's reach.
    CHECK(FlightFrom(100.f) <= 500ms);
}

TEST_CASE("the window grows with the distance", "[pvpprojectiles]")
{
    CHECK(FlightFrom(100.f) < FlightFrom(1000.f));
    CHECK(FlightFrom(1000.f) < FlightFrom(2000.f));
}

TEST_CASE("the slowest spell is still counted as in flight", "[pvpprojectiles]")
{
    // Ice Storm flies 512 units a second.
    CHECK(FlightFrom(1000.f) >= 1953ms);
}

TEST_CASE("the longest flight is the limit, however far", "[pvpprojectiles]")
{
    CHECK(FlightFrom(100000.f) == PvpProjectiles::cFlight);
}
