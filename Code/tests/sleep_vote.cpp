#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <Messages/RequestTimeSkip.h>
#include <Messages/NotifyTimeSkip.h>

#include <RestingPlayers.h>
#include <SleepVote.h>

#include "MessageRoundTrip.h"

#include <limits>

namespace
{
constexpr uint32_t kPlayer1 = 1;
constexpr uint32_t kPlayer2 = 2;
constexpr uint32_t kPlayer3 = 3;

// Server time of the first confirmation in these tests, in milliseconds.
constexpr uint64_t kOpenedAt = 1000;

const std::vector<uint32_t> kTwoPlayers{kPlayer1, kPlayer2};

using Result = SleepVote::ConfirmResult;

// Server time the vote of VoteWithCrossedConfirmation ended, in milliseconds.
constexpr uint64_t kEndedAt = kOpenedAt + 500;

/**
 * Make a vote everyone agreed on, and a second OK of player 1's that reached the server 100 ms after the end and opened a new vote.
 *
 * @return The vote.
 */
SleepVote VoteWithCrossedConfirmation()
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Close(kEndedAt);
    vote.Confirm(kPlayer1, 3.f, kEndedAt + 100);
    return vote;
}
const std::vector<uint32_t> kThreePlayers{kPlayer1, kPlayer2, kPlayer3};
} // namespace

TEST_CASE("a time skip step keeps its step and hours", "[sleep_vote]")
{
    const auto cStep = GENERATE(RequestTimeSkip::Step::kConfirm, RequestTimeSkip::Step::kWithdraw, RequestTimeSkip::Step::kWithdrawCombat, RequestTimeSkip::Step::kAwake,
                                RequestTimeSkip::Step::kDropCrossed);
    RequestTimeSkip request{};
    request.SkipStep = cStep;
    request.Hours = 7.75f;

    REQUIRE(RoundTrip(request) == request);
}

TEST_CASE("a time skip notice keeps its counts and the names of who is missing", "[sleep_vote]")
{
    const auto cEvent = GENERATE(NotifyTimeSkip::Event::kVoteChanged, NotifyTimeSkip::Event::kAgreed, NotifyTimeSkip::Event::kTimedOut, NotifyTimeSkip::Event::kDisabled,
                                 NotifyTimeSkip::Event::kTooSoon);
    const auto cChange = GENERATE(NotifyTimeSkip::Change::kNone, NotifyTimeSkip::Change::kConfirmed, NotifyTimeSkip::Change::kWithdrew, NotifyTimeSkip::Change::kWithdrewCombat,
                                  NotifyTimeSkip::Change::kLeft);
    NotifyTimeSkip notice{};
    notice.SkipEvent = cEvent;
    notice.PlayerChange = cChange;
    notice.ChangedPlayerName = "Player one";
    notice.Hours = 3.5f;
    notice.ReadyCount = 1;
    notice.RequiredCount = 3;
    notice.MissingPlayerNames = {"Player two", "Player three"};
    notice.WaitSeconds = 27;

    REQUIRE(RoundTrip(notice) == notice);
}

TEST_CASE("a damaged name count reads no more names than there are players", "[sleep_vote]")
{
    NotifyTimeSkip notice{};
    notice.RequiredCount = 1;
    notice.MissingPlayerNames = {"Player two", "Player three", "Player four"};

    REQUIRE(RoundTrip(notice).MissingPlayerNames.size() == 1);
}

TEST_CASE("only lengths the menu offers can be voted for", "[sleep_vote]")
{
    REQUIRE(TimeSkipHours::IsValid(1.f));
    REQUIRE(TimeSkipHours::IsValid(7.f));
    REQUIRE(TimeSkipHours::IsValid(24.f));

    REQUIRE_FALSE(TimeSkipHours::IsValid(0.5f));
    REQUIRE_FALSE(TimeSkipHours::IsValid(0.f));
    REQUIRE_FALSE(TimeSkipHours::IsValid(-1.f));
    REQUIRE_FALSE(TimeSkipHours::IsValid(24.01f));
    REQUIRE_FALSE(TimeSkipHours::IsValid(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(TimeSkipHours::IsValid(std::numeric_limits<float>::infinity()));
}

TEST_CASE("a refused length opens no vote", "[sleep_vote]")
{
    SleepVote vote;

    REQUIRE(vote.Confirm(kPlayer1, std::numeric_limits<float>::quiet_NaN(), kOpenedAt) == Result::kInvalidHours);
    REQUIRE(vote.Confirm(kPlayer1, 0.f, kOpenedAt) == Result::kInvalidHours);

    REQUIRE_FALSE(vote.HasConfirmed(kPlayer1));
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kClosed);
}

TEST_CASE("nobody confirmed means no vote", "[sleep_vote]")
{
    SleepVote vote;

    REQUIRE_FALSE(vote.IsOpen());
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kClosed);
    REQUIRE(vote.GetShortestHours() == 0.f);
}

TEST_CASE("time moves only when every connected player confirmed", "[sleep_vote]")
{
    SleepVote vote;

    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt) == Result::kAccepted);
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kWaiting);
    REQUIRE(vote.CountReady(kTwoPlayers) == 1);

    REQUIRE(vote.Confirm(kPlayer2, 8.f, kOpenedAt + 500) == Result::kAccepted);
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt + 500) == SleepVote::Standing::kAgreed);
    REQUIRE(vote.CountReady(kTwoPlayers) == 2);
}

TEST_CASE("a player still loading or in the main menu blocks the vote and is named", "[sleep_vote]")
{
    // Player 3 is connected but has no world yet, so has not confirmed.
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 6.f, kOpenedAt);

    REQUIRE(vote.GetStanding(kThreePlayers, kOpenedAt) == SleepVote::Standing::kWaiting);
    REQUIRE(vote.ListMissing(kThreePlayers) == std::vector<uint32_t>{kPlayer3});
    REQUIRE(vote.CountReady(kThreePlayers) == 2);
}

TEST_CASE("the shortest confirmed length wins", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 3.f, kOpenedAt);
    vote.Confirm(kPlayer3, 12.f, kOpenedAt);

    REQUIRE(vote.GetShortestHours() == 3.f);
}

TEST_CASE("a player who sleeps and a player who waits skip the same hours", "[sleep_vote]")
{
    // Player 1 picked 8 hours of sleep in a bed, player 2 a 3 hour wait: only the hours are shared, each game keeps its own choice.
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 3.f, kOpenedAt);

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kAgreed);
    REQUIRE(vote.GetShortestHours() == 3.f);
}

TEST_CASE("confirming again replaces the first length", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 2.f, kOpenedAt);
    vote.Confirm(kPlayer2, 6.f, kOpenedAt);
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.GetShortestHours() == 6.f);
}

TEST_CASE("a withdrawn confirmation no longer counts", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 2.f, kOpenedAt);
    vote.Confirm(kPlayer2, 6.f, kOpenedAt);

    REQUIRE(vote.Withdraw(kPlayer1, kOpenedAt));
    REQUIRE_FALSE(vote.Withdraw(kPlayer1, kOpenedAt));

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kWaiting);
    REQUIRE(vote.ListMissing(kTwoPlayers) == std::vector<uint32_t>{kPlayer1});
    REQUIRE(vote.GetShortestHours() == 6.f);
}

TEST_CASE("the vote closes when the last confirmation is withdrawn", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    REQUIRE(vote.IsOpen());
    vote.Withdraw(kPlayer1, kOpenedAt);

    REQUIRE_FALSE(vote.IsOpen());
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt + SleepVote::kTimeoutMs) == SleepVote::Standing::kClosed);
}

TEST_CASE("the last missing player leaving lets the others skip", "[sleep_vote]")
{
    // The service forgets a leaving player and stops listing them as connected.
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 4.f, kOpenedAt);
    vote.Forget(kPlayer3);

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kAgreed);
    REQUIRE(vote.GetShortestHours() == 4.f);
}

TEST_CASE("a confirmed player leaving takes their length with them", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 4.f, kOpenedAt);
    vote.Forget(kPlayer2);

    const std::vector<uint32_t> cOnlyPlayer1{kPlayer1};
    REQUIRE(vote.GetStanding(cOnlyPlayer1, kOpenedAt) == SleepVote::Standing::kAgreed);
    REQUIRE(vote.GetShortestHours() == 8.f);
}

TEST_CASE("the vote runs out 60 seconds after the first confirmation", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    // A later confirmation does not restart the timeout.
    vote.Confirm(kPlayer2, 8.f, kOpenedAt + 30000);

    REQUIRE(vote.GetStanding(kThreePlayers, kOpenedAt + 59999) == SleepVote::Standing::kWaiting);
    REQUIRE(vote.GetStanding(kThreePlayers, kOpenedAt + 60000) == SleepVote::Standing::kTimedOut);
    REQUIRE(vote.ListMissing(kThreePlayers) == std::vector<uint32_t>{kPlayer3});
}

TEST_CASE("agreement at the last moment wins over the timeout", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 8.f, kOpenedAt + 60000);

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt + 60000) == SleepVote::Standing::kAgreed);
}

TEST_CASE("a new vote after a closed one gets a fresh timeout", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Close(kOpenedAt);

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt) == SleepVote::Standing::kClosed);

    vote.Confirm(kPlayer2, 8.f, kOpenedAt + 50000);
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt + 100000) == SleepVote::Standing::kWaiting);
    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt + 110000) == SleepVote::Standing::kTimedOut);
}

TEST_CASE("a server clock that went back times the vote out instead of keeping it open", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.GetStanding(kTwoPlayers, kOpenedAt - 1) == SleepVote::Standing::kTimedOut);
}

TEST_CASE("nobody connected leaves no vote", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.GetStanding({}, kOpenedAt) == SleepVote::Standing::kClosed);
}

TEST_CASE("a player who withdrew waits half a minute to confirm again", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Withdraw(kPlayer1, kOpenedAt + 1000);

    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 1000 + 29999) == Result::kTooSoon);
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kOpenedAt + 1000 + 29999) == 1);
    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 1000 + 30000) == Result::kAccepted);
}

TEST_CASE("a refused confirmation opens no vote", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Withdraw(kPlayer1, kOpenedAt);

    vote.Confirm(kPlayer1, 8.f, kOpenedAt + 5000);
    REQUIRE_FALSE(vote.IsOpen());
}

TEST_CASE("a change of length counts at once, and everyone hears of one player's changes at most every 5 seconds", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.Confirm(kPlayer1, 3.f, kOpenedAt + 100) == Result::kChangedQuietly);
    REQUIRE(vote.GetShortestHours() == 3.f);

    REQUIRE(vote.Confirm(kPlayer1, 5.f, kOpenedAt + 4999) == Result::kChangedQuietly);
    REQUIRE(vote.Confirm(kPlayer1, 6.f, kOpenedAt + 5000) == Result::kAccepted);
    REQUIRE(vote.Confirm(kPlayer1, 7.f, kOpenedAt + 9999) == Result::kChangedQuietly);
    REQUIRE(vote.GetShortestHours() == 7.f);
}

TEST_CASE("the same length again changes nothing and tells nobody", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 100) == Result::kUnchanged);
    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 10000) == Result::kUnchanged);
    REQUIRE(vote.GetShortestHours() == 8.f);
}

TEST_CASE("one player's wait does not stop another from joining", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Withdraw(kPlayer1, kOpenedAt);

    REQUIRE(vote.Confirm(kPlayer2, 8.f, kOpenedAt + 100) == Result::kAccepted);
}

TEST_CASE("everyone who confirmed in a vote that ran out waits, and nobody else", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 8.f, kOpenedAt);
    vote.RunOut(kOpenedAt + 60000);

    REQUIRE_FALSE(vote.IsOpen());
    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 61000) == Result::kTooSoon);
    REQUIRE(vote.Confirm(kPlayer2, 8.f, kOpenedAt + 61000) == Result::kTooSoon);
    REQUIRE(vote.Confirm(kPlayer3, 8.f, kOpenedAt + 61000) == Result::kAccepted);
}

TEST_CASE("a skip that ran leaves nobody waiting", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer2, 8.f, kOpenedAt);
    vote.Close(kOpenedAt);

    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 100) == Result::kAccepted);
}

TEST_CASE("a player who left is forgotten with their wait", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Withdraw(kPlayer1, kOpenedAt);
    vote.Forget(kPlayer1);

    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kOpenedAt) == 0);
}

TEST_CASE("a server clock that went back ends the wait", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt + 50000);
    vote.Withdraw(kPlayer1, kOpenedAt + 50000);

    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kOpenedAt) == 0);
    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt) == Result::kAccepted);
}

TEST_CASE("withdrawing without having confirmed starts no wait", "[sleep_vote]")
{
    SleepVote vote;

    REQUIRE_FALSE(vote.Withdraw(kPlayer1, kOpenedAt));
    REQUIRE(vote.Confirm(kPlayer1, 8.f, kOpenedAt + 1) == Result::kAccepted);
}

TEST_CASE("a withdrawal after the vote ran out does not lengthen the wait", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.RunOut(kOpenedAt + 60000);

    REQUIRE_FALSE(vote.Withdraw(kPlayer1, kOpenedAt + 70000));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kOpenedAt + 60000 + 29999) == 1);
}

TEST_CASE("a confirmation that crossed the end of a vote is dropped with no wait", "[sleep_vote]")
{
    SleepVote vote = VoteWithCrossedConfirmation();

    REQUIRE(vote.DropCrossed(kPlayer1, kEndedAt + 200));
    REQUIRE_FALSE(vote.IsOpen());
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kEndedAt + 200) == 0);
}

TEST_CASE("a drop of a confirmation recorded long after the end counts as a withdrawal", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.RunOut(kOpenedAt + 60000);
    vote.Confirm(kPlayer2, 8.f, kOpenedAt + 60000 + 2001);

    REQUIRE(vote.DropCrossed(kPlayer2, kOpenedAt + 63000));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer2, kOpenedAt + 63000) == SleepVote::kCooldownMs);
}

TEST_CASE("a drop before any vote ended counts as a withdrawal", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);

    REQUIRE(vote.DropCrossed(kPlayer1, kOpenedAt + 100));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kOpenedAt + 100) == SleepVote::kCooldownMs);
}

TEST_CASE("a drop from a player who had not confirmed changes nothing", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Close(kOpenedAt + 500);

    REQUIRE_FALSE(vote.DropCrossed(kPlayer2, kOpenedAt + 600));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer2, kOpenedAt + 600) == 0);
}

TEST_CASE("players who rest an agreed skip keep the server busy until each is awake", "[sleep_vote]")
{
    RestingPlayers rests;
    REQUIRE_FALSE(rests.IsAnyoneResting(kOpenedAt));

    rests.Start(kTwoPlayers, kOpenedAt);
    REQUIRE(rests.IsAnyoneResting(kOpenedAt + 1000));

    rests.Wake(kPlayer1);
    REQUIRE(rests.IsAnyoneResting(kOpenedAt + 1000));
    rests.Wake(kPlayer2);
    REQUIRE_FALSE(rests.IsAnyoneResting(kOpenedAt + 1000));
}

TEST_CASE("a game that never says it is awake stops counting after a minute and a half", "[sleep_vote]")
{
    RestingPlayers rests;
    rests.Start(kTwoPlayers, kOpenedAt);

    REQUIRE(rests.IsAnyoneResting(kOpenedAt + 89999));
    REQUIRE_FALSE(rests.IsAnyoneResting(kOpenedAt + 90000));
}

TEST_CASE("a server clock that went back ends every rest", "[sleep_vote]")
{
    RestingPlayers rests;
    rests.Start(kTwoPlayers, kOpenedAt);

    REQUIRE_FALSE(rests.IsAnyoneResting(kOpenedAt - 1));
}

TEST_CASE("a free drop is had once per end; the next counts as a withdrawal", "[sleep_vote]")
{
    // A game that confirms and drops again and again right after an end would flood everyone otherwise.
    SleepVote vote = VoteWithCrossedConfirmation();
    REQUIRE(vote.DropCrossed(kPlayer1, kEndedAt + 200));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kEndedAt + 200) == 0);

    vote.Confirm(kPlayer1, 3.f, kEndedAt + 300);
    REQUIRE(vote.DropCrossed(kPlayer1, kEndedAt + 400));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kEndedAt + 400) == SleepVote::kCooldownMs);
}

TEST_CASE("a drop that comes long after the end counts as a withdrawal, even for a confirmation that crossed it", "[sleep_vote]")
{
    SleepVote vote = VoteWithCrossedConfirmation();

    REQUIRE(vote.DropCrossed(kPlayer1, kEndedAt + 2000));
    REQUIRE(vote.GetCooldownLeftMs(kPlayer1, kEndedAt + 2000) == SleepVote::kCooldownMs);
}

TEST_CASE("a change recorded quietly is told once 5 seconds have passed since the last told one", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 24.f, kOpenedAt);
    vote.Confirm(kPlayer2, 8.f, kOpenedAt);
    vote.Confirm(kPlayer1, 2.f, kOpenedAt + 1000);

    REQUIRE(vote.TakeChangesDue(kOpenedAt + 4999).empty());
    REQUIRE(vote.TakeChangesDue(kOpenedAt + 5000) == std::vector<uint32_t>{kPlayer1});
    REQUIRE(vote.TakeChangesDue(kOpenedAt + 20000).empty());
    REQUIRE(vote.Confirm(kPlayer1, 3.f, kOpenedAt + 9999) == Result::kChangedQuietly);
}

TEST_CASE("a change told at once leaves nothing to tell later", "[sleep_vote]")
{
    SleepVote vote;
    vote.Confirm(kPlayer1, 8.f, kOpenedAt);
    vote.Confirm(kPlayer1, 3.f, kOpenedAt + 1000);
    vote.Confirm(kPlayer1, 4.f, kOpenedAt + 6000);

    REQUIRE(vote.TakeChangesDue(kOpenedAt + 20000).empty());
}
