#include <catch2/catch.hpp>

#include <Services/Generic/IntroWaitRepair.h>

using namespace IntroWaitRepair;

// The game's chargen flags: bit 0 keeps saving off, bit 1 waiting and sleeping, bit 2 shows the controls message.
// The intro's stage 10 sets 0b111, its stages 65 and 80 set 0b110, its stage 255 clears all three.

TEST_CASE("an intro that switched waiting off and is over without its stage 255 left waiting off", "[intro_wait_repair]")
{
    constexpr IntroStages cLeftOff{.switchedWaitingOff = true, .over = true, .switchedWaitingOn = false};
    REQUIRE(IsWaitingLeftOffByIntro(cLeftOff, 0b110));
    REQUIRE(IsWaitingLeftOffByIntro(cLeftOff, 0b111));
    REQUIRE(IsWaitingLeftOffByIntro(cLeftOff, 0b010));
}

TEST_CASE("waiting is left alone while the intro runs", "[intro_wait_repair]")
{
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = true, .over = false, .switchedWaitingOn = false}, 0b110));
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = true, .over = false, .switchedWaitingOn = false}, 0b111));
    // Its stage 255 ran, and another script switched waiting off while it still runs.
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = true, .over = false, .switchedWaitingOn = true}, 0b010));
}

TEST_CASE("waiting switched off after the intro's stage 255 is another script's", "[intro_wait_repair]")
{
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = true, .over = true, .switchedWaitingOn = true}, 0b010));
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = true, .over = true, .switchedWaitingOn = true}, 0b111));
}

TEST_CASE("an alternate start that skipped the stages that switch waiting off leaves waiting to other scripts", "[intro_wait_repair]")
{
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = false, .over = true, .switchedWaitingOn = false}, 0b010));
    REQUIRE_FALSE(IsWaitingLeftOffByIntro({.switchedWaitingOff = false, .over = true, .switchedWaitingOn = false}, 0b110));
}

TEST_CASE("nothing is repaired while waiting is on", "[intro_wait_repair]")
{
    constexpr IntroStages cLeftOff{.switchedWaitingOff = true, .over = true, .switchedWaitingOn = false};
    REQUIRE_FALSE(IsWaitingLeftOffByIntro(cLeftOff, 0b000));
    REQUIRE_FALSE(IsWaitingLeftOffByIntro(cLeftOff, 0b001));
    REQUIRE_FALSE(IsWaitingLeftOffByIntro(cLeftOff, 0b101));
}

TEST_CASE("switching waiting back on keeps the other flags", "[intro_wait_repair]")
{
    REQUIRE(WithWaitingOn(0b110) == 0b100);
    REQUIRE(WithWaitingOn(0b111) == 0b101);
    REQUIRE(WithWaitingOn(0b010) == 0b000);
    REQUIRE(WithWaitingOn(0b001) == 0b001);
    REQUIRE(WithWaitingOn(0xFF) == 0xFD);
}
