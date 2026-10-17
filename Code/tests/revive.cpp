#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/glm.hpp>

#include <catch2/catch.hpp>

#include <Messages/PlayerDownedRequest.h>
#include <Messages/ReviveRequest.h>
#include <Messages/SafePointRequest.h>
#include <Messages/NotifyDownedState.h>

#include <ReviveRules.h>

using namespace TiltedPhoques;

namespace
{
template <class T> T RoundTrip(const T& acMessage)
{
    Buffer buffer(128);
    Buffer::Writer writer(&buffer);
    acMessage.SerializeRaw(writer);
    Buffer::Reader reader(&buffer);
    T back{};
    back.DeserializeRaw(reader);
    return back;
}

// Two party members a few steps apart, the target down with time left: a revive that goes ahead.
ReviveRules::ReviveCheck Allowed()
{
    ReviveRules::ReviveCheck check{};
    check.ReviverId = 1;
    check.TargetId = 2;
    check.TargetDown = true;
    check.TargetTimeLeft = true;
    check.SameParty = true;
    check.SamePlace = true;
    check.Distance = 80.f;
    return check;
}
} // namespace

TEST_CASE("the revive messages keep who, what, how long and where", "[revive]")
{
    REQUIRE(RoundTrip(PlayerDownedRequest{}) == PlayerDownedRequest{});
    PlayerDownedRequest up{};
    up.IsDown = false;
    REQUIRE(RoundTrip(up) == up);
    REQUIRE_FALSE(RoundTrip(up).IsDown);
    PlayerDownedRequest respawning{};
    respawning.IsDown = false;
    respawning.Respawning = true;
    REQUIRE(RoundTrip(respawning) == respawning);
    REQUIRE(RoundTrip(respawning).Respawning);

    ReviveRequest revive{};
    revive.TargetPlayerId = 0xFFFFFFFFu;
    REQUIRE(RoundTrip(revive) == revive);

    SafePointRequest safePoint{};
    safePoint.CellId = GameId(3, 0x1A26F);
    safePoint.WorldSpaceId = GameId(0, 0x3C);
    safePoint.Position = glm::vec3(-12345.f, 67890.f, -250.f);
    REQUIRE(RoundTrip(safePoint) == safePoint);
    SafePointRequest interior{};
    interior.CellId = GameId(0, 0x16BDB);
    interior.Position = glm::vec3(10.f, -20.f, 30.f);
    REQUIRE(RoundTrip(interior) == interior);

    for (const auto cChange :
         {NotifyDownedState::kDown, NotifyDownedState::kRevived, NotifyDownedState::kRespawned, NotifyDownedState::kPartyFallen, NotifyDownedState::kDied,
          NotifyDownedState::kStoodUp, NotifyDownedState::kReviveRefused, NotifyDownedState::kLeft})
    {
        NotifyDownedState notify{};
        notify.PlayerId = 7;
        notify.Change = cChange;
        notify.ReviverId = 0xFFFFFFFFu;
        notify.SecondsLeft = 600;
        notify.Refusal = ReviveRefusal::kTooFar;
        REQUIRE(RoundTrip(notify) == notify);

        notify.HasRespawnPoint = true;
        notify.RespawnCellId = GameId(3, 0x1A26F);
        notify.RespawnWorldSpaceId = GameId(0, 0x3C);
        notify.RespawnPosition = glm::vec3(-12345.f, 67890.f, -250.f);
        REQUIRE(RoundTrip(notify) == notify);
    }
}

TEST_CASE("a revive goes ahead only between party members close together, on a player still down", "[revive]")
{
    using ReviveRules::Refusal;

    REQUIRE(ReviveRules::CheckRevive(Allowed()) == Refusal::kNone);

    auto check = Allowed();
    check.TargetId = check.ReviverId;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kSelf);

    check = Allowed();
    check.TargetDown = false;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kTargetNotDown);

    check = Allowed();
    check.TargetTimeLeft = false;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kTargetOutOfTime);

    check = Allowed();
    check.ReviverDown = true;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kReviverDown);

    check = Allowed();
    check.SameParty = false;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kNotSameParty);

    check = Allowed();
    check.SamePlace = false;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kNotSamePlace);

    check = Allowed();
    check.Distance = ReviveRules::cReachUnits;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kNone);
    check.Distance = ReviveRules::cReachUnits + 1.f;
    REQUIRE(ReviveRules::CheckRevive(check) == Refusal::kTooFar);
}

TEST_CASE("a player alone respawns at once, a party member waits the server's time", "[revive]")
{
    REQUIRE(ReviveRules::DownedSeconds(1, 60) == 0);
    REQUIRE(ReviveRules::DownedSeconds(2, 60) == 60);
    REQUIRE(ReviveRules::DownedSeconds(5, 30) == 30);
}

TEST_CASE("nobody is left to revive when every member is down, a party left with one down member too", "[revive]")
{
    REQUIRE_FALSE(ReviveRules::NobodyToRevive({}));
    REQUIRE(ReviveRules::NobodyToRevive({true}));
    REQUIRE_FALSE(ReviveRules::NobodyToRevive({true, false}));
    REQUIRE(ReviveRules::NobodyToRevive({true, true}));
    REQUIRE_FALSE(ReviveRules::NobodyToRevive({true, true, false}));
    REQUIRE(ReviveRules::NobodyToRevive({true, true, true}));
}

TEST_CASE("a downed player waits only while a standing member could come: same place, within reach", "[revive]")
{
    using ReviveRules::Helper;
    constexpr float cFar = ReviveRules::cComeToHelpUnits + 1.f;

    // Nobody standing is NobodyToRevive's case.
    REQUIRE_FALSE(ReviveRules::NobodyCanCome({}));
    REQUIRE_FALSE(ReviveRules::NobodyCanCome({Helper{true, true, 500.f}}));
    REQUIRE_FALSE(ReviveRules::NobodyCanCome({Helper{true, true, ReviveRules::cComeToHelpUnits}}));
    REQUIRE(ReviveRules::NobodyCanCome({Helper{true, true, cFar}}));
    // Another worldspace or interior: however close the numbers, they cannot come.
    REQUIRE(ReviveRules::NobodyCanCome({Helper{true, false, 100.f}}));
    // One who can come is enough.
    REQUIRE_FALSE(ReviveRules::NobodyCanCome({Helper{true, false, 100.f}, Helper{true, true, 2000.f}}));
    REQUIRE(ReviveRules::NobodyCanCome({Helper{true, false, 100.f}, Helper{true, true, cFar}}));
    // A member whose place the server does not know yet might be near: the wait goes on.
    REQUIRE_FALSE(ReviveRules::NobodyCanCome({Helper{false, false, 0.f}, Helper{true, true, cFar}}));
}

TEST_CASE("a player not revived respawns at the party leader's safe point, else their own, else the game's spot", "[revive]")
{
    using ReviveRules::RespawnPoint;

    REQUIRE(ReviveRules::ChooseRespawnPoint(true, true) == RespawnPoint::kPartyLeader);
    REQUIRE(ReviveRules::ChooseRespawnPoint(true, false) == RespawnPoint::kPartyLeader);
    REQUIRE(ReviveRules::ChooseRespawnPoint(false, true) == RespawnPoint::kOwn);
    REQUIRE(ReviveRules::ChooseRespawnPoint(false, false) == RespawnPoint::kGameDefault);
}
