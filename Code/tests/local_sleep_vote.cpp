#include <catch2/catch.hpp>

#include <Services/LocalSleepVote.h>

#include <limits>

namespace
{
// Two menus, by address only: the vote compares them and never reads them.
// Two places in one array, so the linker cannot fold them into one address.
const int kMenus[2]{};
const void* const kFirstMenu = &kMenus[0];
const void* const kSecondMenu = &kMenus[1];

using Phase = LocalSleepVote::Phase;
using Press = LocalSleepVote::PressResult;

// This game's steady time in these tests, in milliseconds.
constexpr uint64_t kNow = 1000;
} // namespace

TEST_CASE("an OK press goes out once as a confirmation", "[local_sleep_vote]")
{
    LocalSleepVote vote;

    REQUIRE(vote.Press(kFirstMenu, 8.f, kNow) == Press::kHeld);
    REQUIRE(vote.GetPhase() == Phase::kPressed);

    REQUIRE(vote.TakePress() == 8.f);
    REQUIRE(vote.GetPhase() == Phase::kConfirmed);
    REQUIRE_FALSE(vote.TakePress().has_value());
}

TEST_CASE("a length the menu cannot offer is not held", "[local_sleep_vote]")
{
    LocalSleepVote vote;

    REQUIRE(vote.Press(kFirstMenu, 0.f, kNow) == Press::kNotTaken);
    REQUIRE(vote.Press(kFirstMenu, 25.f, kNow) == Press::kNotTaken);
    REQUIRE(vote.Press(kFirstMenu, std::numeric_limits<float>::quiet_NaN(), kNow) == Press::kNotTaken);
    REQUIRE(vote.GetPhase() == Phase::kIdle);
}

TEST_CASE("pressing again replaces the length and sends it again", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    REQUIRE(vote.Press(kFirstMenu, 3.f, kNow) == Press::kHeld);
    REQUIRE(vote.TakePress() == 3.f);
}

TEST_CASE("the vote follows the menu it was pressed in", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    REQUIRE_FALSE(vote.IsMenuGone(kFirstMenu));
    // Closed, or closed and another opened between two updates.
    REQUIRE(vote.IsMenuGone(nullptr));
    REQUIRE(vote.IsMenuGone(kSecondMenu));
}

TEST_CASE("no vote means no menu to lose", "[local_sleep_vote]")
{
    LocalSleepVote vote;

    REQUIRE_FALSE(vote.IsMenuGone(nullptr));
}

TEST_CASE("withdrawing a sent confirmation tells the server", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    REQUIRE(vote.Withdraw());
    REQUIRE(vote.GetPhase() == Phase::kIdle);
    REQUIRE_FALSE(vote.Withdraw());
}

TEST_CASE("withdrawing a press never sent tells nobody", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);

    REQUIRE_FALSE(vote.Withdraw());
    REQUIRE(vote.GetPhase() == Phase::kIdle);
}

TEST_CASE("the agreed length is pressed once, in the menu the vote began in", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    // Player 2 picked 3 hours, the shortest.
    REQUIRE(vote.Agree(3.f));
    REQUIRE(vote.GetPhase() == Phase::kAgreed);

    REQUIRE(vote.TakeAgreed(kFirstMenu) == 3.f);
    REQUIRE(vote.GetPhase() == Phase::kResting);
    REQUIRE_FALSE(vote.TakeAgreed(kFirstMenu).has_value());
}

TEST_CASE("an agreement without a vote here changes nothing", "[local_sleep_vote]")
{
    LocalSleepVote vote;

    REQUIRE_FALSE(vote.Agree(3.f));
    REQUIRE_FALSE(vote.AgreeAlone());
    REQUIRE(vote.GetPhase() == Phase::kIdle);
}

TEST_CASE("a broken agreed length is not pressed", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    REQUIRE_FALSE(vote.Agree(std::numeric_limits<float>::infinity()));
    REQUIRE(vote.GetPhase() == Phase::kConfirmed);
}

TEST_CASE("an agreement after the menu closed is dropped, not pressed in another menu", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Agree(8.f);

    REQUIRE_FALSE(vote.TakeAgreed(kSecondMenu).has_value());
    REQUIRE(vote.GetPhase() == Phase::kIdle);
}

TEST_CASE("a server that does not share the time lets the player rest their own length", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 6.f, kNow);
    vote.TakePress();

    REQUIRE(vote.AgreeAlone());
    REQUIRE(vote.TakeAgreed(kFirstMenu) == 6.f);
}

TEST_CASE("no new press while the skip is under way", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Agree(8.f);

    REQUIRE(vote.Press(kFirstMenu, 2.f, kNow) == Press::kNotTaken);

    vote.TakeAgreed(kFirstMenu);
    REQUIRE(vote.Press(kFirstMenu, 2.f, kNow) == Press::kNotTaken);
    REQUIRE(vote.GetPhase() == Phase::kResting);
}

TEST_CASE("the end of the game's own rest is reported once", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Agree(8.f);
    vote.TakeAgreed(kFirstMenu);

    REQUIRE(vote.FinishResting());
    REQUIRE(vote.GetPhase() == Phase::kIdle);
    REQUIRE_FALSE(vote.FinishResting());
}

TEST_CASE("a newer press still waiting to be sent does not hide the confirmation the server holds", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Press(kFirstMenu, 3.f, kNow);

    REQUIRE(vote.GetPhase() == Phase::kPressed);
    REQUIRE(vote.Withdraw());
}

TEST_CASE("a new vote after one taken back starts with nothing sent", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Withdraw();

    vote.Press(kFirstMenu, 8.f, kNow);
    REQUIRE_FALSE(vote.Withdraw());
}

TEST_CASE("the vote says whether it waits on the other players", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    REQUIRE_FALSE(vote.IsVoting());

    vote.Press(kFirstMenu, 8.f, kNow);
    REQUIRE(vote.IsVoting());
    vote.TakePress();
    REQUIRE(vote.IsVoting());

    vote.Agree(8.f);
    REQUIRE_FALSE(vote.IsVoting());
}

TEST_CASE("another vote running out keeps this game's rest and its wake-up", "[local_sleep_vote]")
{
    // This game rests from the last skip while player 2's new vote runs out.
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Agree(8.f);
    vote.TakeAgreed(kFirstMenu);

    REQUIRE_FALSE(vote.Withdraw());
    REQUIRE(vote.GetPhase() == Phase::kResting);
    REQUIRE(vote.FinishResting());
}

TEST_CASE("a lost connection drops everything, a rest under way included", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();
    vote.Agree(8.f);
    vote.TakeAgreed(kFirstMenu);

    vote.Reset();
    REQUIRE(vote.GetPhase() == Phase::kIdle);
    REQUIRE_FALSE(vote.FinishResting());
}

TEST_CASE("OK pressed again for the same length sends nothing", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.Press(kFirstMenu, 8.f, kNow);
    vote.TakePress();

    REQUIRE(vote.Press(kFirstMenu, 8.f, kNow) == Press::kAlreadyHeld);
    REQUIRE_FALSE(vote.TakePress().has_value());
    REQUIRE(vote.GetPhase() == Phase::kConfirmed);
}

TEST_CASE("presses are dropped during the wait the server asked for, and the drop is noted once", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.WaitBeforePressing(kNow + 5000);

    REQUIRE(vote.Press(kFirstMenu, 8.f, kNow + 4999) == Press::kWaiting);
    REQUIRE(vote.GetPhase() == Phase::kIdle);
    REQUIRE(vote.GetWaitLeftMs(kNow + 4999) == 1);
    REQUIRE(vote.TakeDroppedPress());
    REQUIRE_FALSE(vote.TakeDroppedPress());

    REQUIRE(vote.GetWaitLeftMs(kNow + 5000) == 0);
    REQUIRE(vote.Press(kFirstMenu, 8.f, kNow + 5000) == Press::kHeld);
}

TEST_CASE("a lost connection ends the wait", "[local_sleep_vote]")
{
    LocalSleepVote vote;
    vote.WaitBeforePressing(kNow + 5000);
    vote.Press(kFirstMenu, 8.f, kNow);

    vote.Reset();
    REQUIRE_FALSE(vote.TakeDroppedPress());
    REQUIRE(vote.Press(kFirstMenu, 8.f, kNow) == Press::kHeld);
}
