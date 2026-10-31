#include <string>

#include <catch2/catch.hpp>

#include <CopyRemovalPolicy.h>

#include <deque>

using namespace CopyRemovalPolicy;

// The property that matters: a remote copy held back for safety must never be able to accumulate.
//
// "Wait until the game releases it" has no bound when the game never releases it.
// An unbounded wait fills the world with live copies and collapses the frame rate in VR.
// These tests check the bound in under a second instead of in a play session.

TEST_CASE("a copy nothing holds is freed at once", "[copyremoval]")
{
    REQUIRE(OnRemoved(0, 0) == Action::DeleteNow);
    REQUIRE(OnRemoved(0, kMaxWaiting - 1) == Action::DeleteNow);
    REQUIRE(OnRemoved(0, kMaxWaiting) == Action::DeleteNow);
}

TEST_CASE("a copy something still holds waits, while there is room", "[copyremoval]")
{
    REQUIRE(OnRemoved(3, 0) == Action::WaitDisabled);
    REQUIRE(OnRemoved(1, kMaxWaiting - 1) == Action::WaitDisabled);
}

TEST_CASE("at the limit it is freed anyway, holders or not", "[copyremoval]")
{
    REQUIRE(OnRemoved(3, kMaxWaiting) == Action::DeleteNow);
    REQUIRE(OnRemoved(999, kMaxWaiting + 50) == Action::DeleteNow);
}

TEST_CASE("the waiting list cannot grow past the bound, however the game behaves", "[copyremoval]")
{
    // The exact failure of 2026-09-27: the handle count never reaches zero because the holders are the game's
    // own lists. Five hundred removals in a row, none of them ever released.
    std::deque<int> waiting;
    for (int i = 0; i < static_cast<int>(kMaxWaiting) * 2; ++i)
    {
        if (OnRemoved(3, waiting.size()) == Action::WaitDisabled)
            waiting.push_back(i);

        // Nothing is ever released by the game, so the only way out is the bound.
        REQUIRE(waiting.size() <= kMaxWaiting);
    }

    REQUIRE(waiting.size() == kMaxWaiting);
}

TEST_CASE("a player leaving with a hundred and thirty copies held frees none of them early", "[copyremoval]")
{
    // A burst as large as one seen in play: every copy still held, none forced out by the bound.
    std::deque<int> waiting;
    for (int i = 0; i < 130; ++i)
        if (OnRemoved(3, waiting.size()) == Action::WaitDisabled)
            waiting.push_back(i);

    REQUIRE(waiting.size() == 130);
}

TEST_CASE("a waiting copy is released once its handles clear", "[copyremoval]")
{
    REQUIRE(ShouldRelease(0, 10, false));
    REQUIRE_FALSE(ShouldRelease(kOwnHolds, 10, false));
}

TEST_CASE("a waiting copy ends after the wait", "[copyremoval]")
{
    REQUIRE_FALSE(ShouldRelease(kOwnHolds, kMaxWaitMs - 1, false));
    REQUIRE(ShouldRelease(kOwnHolds, kMaxWaitMs, false));
    REQUIRE(ShouldRelease(kOwnHolds + 2, kMaxWaitMs, false));
}

// Every reference in a cell has the game's own two holds, so a live copy never reads zero.
// A copy at those two is the normal end of the wait, not a copy freed while held.
TEST_CASE("the game's own two holds are not something still holding the copy", "[copyremoval]")
{
    REQUIRE(ReleaseReason(kOwnHolds, kMaxWaitMs, false) == Released::HandlesGone);
    REQUIRE_FALSE(FreedWhileHeld(ReleaseReason(kOwnHolds, kMaxWaitMs, false)));

    REQUIRE(ReleaseReason(kOwnHolds + 1, kMaxWaitMs, false) == Released::WaitedOut);
    REQUIRE(FreedWhileHeld(ReleaseReason(kOwnHolds + 1, kMaxWaitMs, false)));
}

TEST_CASE("the oldest go when the list is over its bound", "[copyremoval]")
{
    REQUIRE(ShouldRelease(5, 0, true));
}

// Why a copy was freed: two different outcomes must never print the same sentence.

TEST_CASE("freeing a copy says which of the three reasons it was", "[copyremoval]")
{
    REQUIRE(ReleaseReason(0, 10, false) == Released::HandlesGone);
    REQUIRE(ReleaseReason(2, 10, false) == Released::NotYet);
    REQUIRE(ReleaseReason(kOwnHolds + 1, kMaxWaitMs, false) == Released::WaitedOut);
    REQUIRE(ReleaseReason(2, 10, true) == Released::ListFull);

    // Zero handles is safe on any grounds, so it outranks both give-up paths: there is nothing left to dangle.
    REQUIRE(ReleaseReason(0, kMaxWaitMs, true) == Released::HandlesGone);
}

TEST_CASE("only the give-up paths delete a copy something else holds", "[copyremoval]")
{
    REQUIRE_FALSE(FreedWhileHeld(Released::HandlesGone));
    REQUIRE_FALSE(FreedWhileHeld(Released::NotYet));
    REQUIRE(FreedWhileHeld(Released::WaitedOut));
    REQUIRE(FreedWhileHeld(Released::ListFull));
}

TEST_CASE("the three reasons do not share a description", "[copyremoval]")
{
    // The whole defect was two different outcomes printing the same sentence.
    const std::string gone = Describe(Released::HandlesGone);
    const std::string waited = Describe(Released::WaitedOut);
    const std::string full = Describe(Released::ListFull);

    REQUIRE(gone != waited);
    REQUIRE(gone != full);
    REQUIRE(waited != full);
}

TEST_CASE("ShouldRelease still agrees with the reason it is derived from", "[copyremoval]")
{
    for (uint32_t handles = 0; handles < 4; ++handles)
    {
        for (uint32_t waited : {0u, kMaxWaitMs - 1, kMaxWaitMs, kMaxWaitMs * 2})
        {
            for (bool overBound : {false, true})
            {
                INFO("handles " << handles << " waited " << waited << " overBound " << overBound);
                REQUIRE(ShouldRelease(handles, waited, overBound) ==
                        (ReleaseReason(handles, waited, overBound) != Released::NotYet));
            }
        }
    }
}
