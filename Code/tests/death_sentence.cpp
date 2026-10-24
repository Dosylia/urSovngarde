#include <catch2/catch.hpp>

#include <Games/Skyrim/DeathSentence.h>

using DeathSentence::Cause;
using DeathSentence::Describe;
using DeathSentence::How;

TEST_CASE("the death line names the killer, and what it used when that has a name", "[death]")
{
    REQUIRE(Describe("Lydia", Cause{"Bandit Chief", "Iron War Axe", How::kMelee}) == "Lydia was slain by Bandit Chief using Iron War Axe");
    REQUIRE(Describe("Lydia", Cause{"Wolf", "", How::kMelee}) == "Lydia was slain by Wolf");
    REQUIRE(Describe("Lydia", Cause{"Bandit Archer", "Hunting Bow", How::kShot}) == "Lydia was shot by Bandit Archer");
    REQUIRE(Describe("Lydia", Cause{"Dragon", "Fire Breath", How::kMagic}) == "Lydia was killed by Dragon using Fire Breath");
    REQUIRE(Describe("Lydia", Cause{"Hagraven", "", How::kMagic}) == "Lydia was killed by Hagraven using magic");
    REQUIRE(Describe("Lydia", Cause{"Bandit", "Iron Shield", How::kBash}) == "Lydia was bashed by Bandit");
}

TEST_CASE("the death line without a killer: a fall, or nothing known", "[death]")
{
    Cause fall{};
    fall.Fell = true;
    REQUIRE(Describe("Lydia", fall) == "Lydia fell from a high place");
    fall.Killer = "Giant";
    REQUIRE(Describe("Lydia", fall) == "Lydia was doomed to fall by Giant");
    REQUIRE(Describe("Lydia", Cause{}) == "Lydia died");
}

TEST_CASE("the death card's capitals leave other scripts alone", "[death]")
{
    REQUIRE(DeathSentence::Capitals("Lydia was slain by Wolf") == "LYDIA WAS SLAIN BY WOLF");
    REQUIRE(DeathSentence::Capitals("\xC3\xA9tienne") == "\xC3\xA9TIENNE");
}
