#include <catch2/catch.hpp>

#include <Copies/FallStop.h>

using namespace FallStop;

// A copy's ragdoll is switched off only when it lies deep under its owner's corpse and is still dropping, seen within a short time.
// A body resting far below a corpse its owner has up a cliff, or one the owner carried between two looks, must stay.

TEST_CASE("a ragdoll deep under the ground and still dropping is falling away", "[fall_stop]")
{
    REQUIRE(SankAway(kFallenAwayDepth + 100.f, 300.f, 0.5f));
    REQUIRE(SankAway(1'000'000.f, kSinkingStep, kLookGapSeconds));
}

TEST_CASE("a ragdoll that is not deep enough is not falling away", "[fall_stop]")
{
    REQUIRE_FALSE(SankAway(kFallenAwayDepth, 300.f, 0.5f));
    REQUIRE_FALSE(SankAway(500.f, 300.f, 0.5f));
    REQUIRE_FALSE(SankAway(-5000.f, 300.f, 0.5f));
}

TEST_CASE("a deep ragdoll that rests is not falling away", "[fall_stop]")
{
    REQUIRE_FALSE(SankAway(5000.f, 0.f, 0.5f));
    REQUIRE_FALSE(SankAway(5000.f, kSinkingStep - 1.f, 0.5f));
    REQUIRE_FALSE(SankAway(5000.f, -200.f, 0.5f));
}

TEST_CASE("looks too far apart say nothing of a fall", "[fall_stop]")
{
    REQUIRE_FALSE(SankAway(5000.f, 3000.f, kLookGapSeconds + 0.1f));
    REQUIRE_FALSE(SankAway(5000.f, 3000.f, 30.f));
}
