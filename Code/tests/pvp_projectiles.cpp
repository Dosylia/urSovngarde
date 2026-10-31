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

using PvpProjectiles::DropOldSpellSides;
using PvpProjectiles::TakeOtherSide;
using PvpProjectiles::TakeUnmade;
using PvpProjectiles::SpellSide;
using PvpProjectiles::SpellTopUp;

TEST_CASE("a hit waits for its report, and a report for its hit", "[pvpprojectiles]")
{
    std::vector<SpellSide> waiting;
    const SpellSide cHit{1, 2, 5, false, 30.f, {}};
    const SpellSide cReport{1, 2, 5, true, 45.f, {}};

    CHECK_FALSE(TakeOtherSide(waiting, cHit));
    waiting.push_back(cHit);
    const auto cFound = TakeOtherSide(waiting, cReport);
    REQUIRE(cFound);
    CHECK(cFound->Amount == 30.f);
    CHECK(waiting.empty());

    CHECK_FALSE(TakeOtherSide(waiting, cReport));
    waiting.push_back(cReport);
    const auto cOther = TakeOtherSide(waiting, cHit);
    REQUIRE(cOther);
    CHECK(cOther->Amount == 45.f);
    CHECK(waiting.empty());
}

TEST_CASE("sides of another spell, effect or copy do not match", "[pvpprojectiles]")
{
    std::vector<SpellSide> waiting{SpellSide{1, 2, 5, false, 30.f, {}}};
    CHECK_FALSE(TakeOtherSide(waiting, SpellSide{1, 3, 5, true, 45.f, {}}));
    CHECK_FALSE(TakeOtherSide(waiting, SpellSide{1, 2, 6, true, 45.f, {}}));
    CHECK_FALSE(TakeOtherSide(waiting, SpellSide{4, 2, 5, true, 45.f, {}}));
    CHECK_FALSE(TakeOtherSide(waiting, SpellSide{1, 2, 5, false, 30.f, {}}));
    CHECK(waiting.size() == 1);
}

TEST_CASE("an unmade launch answers one report of that copy and spell", "[pvpprojectiles]")
{
    PvpProjectiles::NoteUnmade(7, 8);
    CHECK_FALSE(TakeUnmade(7, 9));
    CHECK_FALSE(TakeUnmade(6, 8));
    CHECK(TakeUnmade(7, 8));
    CHECK_FALSE(TakeUnmade(7, 8));
}

TEST_CASE("the report only ever adds to the hit", "[pvpprojectiles]")
{
    CHECK(SpellTopUp(45.f, 30.f) == 15.f);
    CHECK(SpellTopUp(30.f, 30.f) == 0.f);
    CHECK(SpellTopUp(20.f, 30.f) == 0.f);
}

TEST_CASE("a side waits for a projectile's longest flight, and a lone report counts as a spell that missed here", "[pvpprojectiles]")
{
    const auto cStart = std::chrono::steady_clock::time_point{} + 1h;
    std::vector<SpellSide> waiting{SpellSide{1, 2, 5, true, 45.f, cStart}, SpellSide{1, 3, 5, false, 30.f, cStart}};

    CHECK(DropOldSpellSides(waiting, cStart + PvpProjectiles::cSpellSideWait - 1ms) == 0);
    CHECK(waiting.size() == 2);
    CHECK(DropOldSpellSides(waiting, cStart + PvpProjectiles::cSpellSideWait) == 1);
    CHECK(waiting.empty());
}
