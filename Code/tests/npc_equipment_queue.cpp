// How the checks of what local NPCs wear are queued and merged (client/Services/NpcEquipmentQueue.h).

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>

#include <catch2/catch.hpp>

#include <Services/NpcEquipmentQueue.h>

namespace
{
using NpcEquipmentQueue::PendingCheck;

constexpr uint32_t kGuard = 0x1000;
constexpr uint32_t kBandit = 0x2000;

/**
 * Make the check that follows one equipment change, as NpcEquipmentCheck::Note does.
 *
 * @param aFormId The NPC.
 * @param aSentAtMs When the change was sent, in milliseconds from the start of the test.
 * @param aSentEntries How many entries the equipment sent with the change lists, to tell the changes apart.
 * @param aUndone True when the change was already undone when it was sent.
 * @return The check, due one second after the change, with two checks left.
 */
PendingCheck CheckAfterChange(const uint32_t aFormId, const int aSentAtMs, const size_t aSentEntries, const bool aUndone)
{
    PendingCheck check;
    check.FormId = aFormId;
    check.SentAt = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(aSentAtMs);
    check.Due = check.SentAt + std::chrono::seconds(1);
    check.ChecksLeft = 2;
    check.Sent.Entries.resize(aSentEntries);
    check.Undone = aUndone;
    return check;
}
} // namespace

TEST_CASE("An undone change followed by a normal one is still undone", "[equipment]")
{
    TiltedPhoques::Vector<PendingCheck> checks;
    // A shield unequip the game undid at once, then a sword handed over 400 ms later.
    NpcEquipmentQueue::Queue(checks, CheckAfterChange(kGuard, 0, 1, true));
    const PendingCheck cSword = CheckAfterChange(kGuard, 400, 2, false);
    NpcEquipmentQueue::Queue(checks, cSword);

    REQUIRE(checks.size() == 1);
    CHECK(checks[0].Undone);
    CHECK(checks[0].Sent.Entries.size() == 2);
    CHECK(checks[0].Due == cSword.Due);
    CHECK(checks[0].SentAt == cSword.SentAt);
}

TEST_CASE("Two normal changes keep the newest equipment as the baseline", "[equipment]")
{
    TiltedPhoques::Vector<PendingCheck> checks;
    NpcEquipmentQueue::Queue(checks, CheckAfterChange(kGuard, 0, 1, false));
    const PendingCheck cSecond = CheckAfterChange(kGuard, 400, 2, false);
    NpcEquipmentQueue::Queue(checks, cSecond);

    REQUIRE(checks.size() == 1);
    CHECK_FALSE(checks[0].Undone);
    CHECK(checks[0].Sent.Entries.size() == 2);
    CHECK(checks[0].Due == cSecond.Due);
}

TEST_CASE("A change of another NPC gets a check of its own", "[equipment]")
{
    TiltedPhoques::Vector<PendingCheck> checks;
    NpcEquipmentQueue::Queue(checks, CheckAfterChange(kGuard, 0, 1, true));
    NpcEquipmentQueue::Queue(checks, CheckAfterChange(kBandit, 400, 2, false));

    REQUIRE(checks.size() == 2);
    CHECK(checks[0].FormId == kGuard);
    CHECK(checks[0].Undone);
    CHECK(checks[0].Sent.Entries.size() == 1);
    CHECK(checks[1].FormId == kBandit);
    CHECK_FALSE(checks[1].Undone);
}
