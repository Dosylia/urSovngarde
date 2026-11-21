// How a party member follows the leader's civil war without undoing its own (client/Services/WarFollow.h).

#include <catch2/catch.hpp>

#include <Services/WarFollow.h>

namespace
{
constexpr uint8_t kShown = WarState::kDisplayed;
constexpr uint8_t kDone = WarState::kDisplayed | WarState::kCompleted;
constexpr uint8_t kLost = WarState::kDisplayed | WarState::kFailed;
} // namespace

TEST_CASE("A member takes the objective flags the leader has and it lacks", "[war]")
{
    CHECK(WarFollow::FlagsToAdd(0, kShown) == WarState::kDisplayed);
    CHECK(WarFollow::FlagsToAdd(0, kDone) == kDone);
    CHECK(WarFollow::FlagsToAdd(kShown, kDone) == WarState::kCompleted);
    CHECK(WarFollow::FlagsToAdd(kShown, kLost) == WarState::kFailed);
}

TEST_CASE("A member never takes a flag away", "[war]")
{
    // The leader is behind: its objective is only shown, or not even that.
    CHECK(WarFollow::FlagsToAdd(kDone, kShown) == 0);
    CHECK(WarFollow::FlagsToAdd(kDone, 0) == 0);
    CHECK(WarFollow::FlagsToAdd(kShown, 0) == 0);
    CHECK(WarFollow::FlagsToAdd(kLost, kShown) == 0);
}

TEST_CASE("An objective finished here keeps its outcome", "[war]")
{
    // Completed here and failed for the leader: the two wars went different ways, and this game keeps its own.
    CHECK(WarFollow::FlagsToAdd(kDone, kLost) == 0);
    CHECK(WarFollow::FlagsToAdd(kLost, kDone) == 0);
    // Finished here but not shown: it may still be shown as the leader shows it.
    CHECK(WarFollow::FlagsToAdd(WarState::kCompleted, kDone) == WarState::kDisplayed);
    CHECK(WarFollow::FlagsToAdd(WarState::kFailed, kLost) == WarState::kDisplayed);
    CHECK(WarFollow::IsFinished(kDone));
    CHECK(WarFollow::IsFinished(kLost));
    CHECK_FALSE(WarFollow::IsFinished(kShown));
    CHECK_FALSE(WarFollow::IsFinished(0));
}

TEST_CASE("The leader's war is behind when this game finished an objective the leader has not", "[war]")
{
    decltype(WarState::Objectives) here{};
    decltype(WarState::Objectives) leader{};
    CHECK_FALSE(WarFollow::FirstFinishedOnlyHere(here, leader).has_value());

    // Both finished the same objective: not behind.
    here[2] = kDone;
    leader[2] = kDone;
    CHECK_FALSE(WarFollow::FirstFinishedOnlyHere(here, leader).has_value());

    // The leader finished more: ahead, not behind.
    leader[4] = kDone;
    CHECK_FALSE(WarFollow::FirstFinishedOnlyHere(here, leader).has_value());

    // This game finished one the leader only shows: behind, and that objective is named.
    here[6] = kDone;
    leader[6] = kShown;
    const auto cBehind = WarFollow::FirstFinishedOnlyHere(here, leader);
    REQUIRE(cBehind.has_value());
    CHECK(*cBehind == 6);
}

TEST_CASE("A failed objective counts as finished on both sides", "[war]")
{
    decltype(WarState::Objectives) here{};
    decltype(WarState::Objectives) leader{};

    // Failed here and completed for the leader: both finished, so not behind.
    here[3] = kLost;
    leader[3] = kDone;
    CHECK_FALSE(WarFollow::FirstFinishedOnlyHere(here, leader).has_value());

    // Failed here and only shown for the leader: behind.
    leader[3] = kShown;
    const auto cBehind = WarFollow::FirstFinishedOnlyHere(here, leader);
    REQUIRE(cBehind.has_value());
    CHECK(*cBehind == 3);
}
