#pragma once

#include <Structs/Inventory.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

/**
 * The checks waiting to read again what local NPCs wear, at most one per NPC.
 * Header-only, without the game, so that the tests can check it.
 */
namespace NpcEquipmentQueue
{
// A bound for a fight with many NPCs changing gear at once.
constexpr size_t kMaxChecks = 256;

/**
 * A check waiting to read again what a local NPC wears after its equipment changes were sent.
 */
struct PendingCheck
{
    uint32_t FormId{};
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    // What the other game's copy is taken to wear: the equipment last sent for the NPC.
    Inventory Sent{};
    // When the next check reads what the NPC wears.
    std::chrono::steady_clock::time_point Due{};
    uint32_t ChecksLeft{};
    // When the last change was sent, for the log.
    std::chrono::steady_clock::time_point SentAt{};
    // A change was already undone when it was sent, so the next check sends what the NPC wears whatever it finds.
    bool Undone{};
};

/**
 * Queue the check that follows an equipment change, merged with the one already waiting for that NPC.
 * The merged check stays undone when either change was undone, so a change the game undid at once is still corrected.
 * The newest change's equipment is kept as the baseline: by then the other game's copy has applied both changes, and only the newest reading holds both.
 * An undone change is sent again whatever the baseline says, so the older baseline is not needed for it.
 * The due time, the checks left and the time sent come from the newest change.
 * When the queue is full, the oldest check goes first.
 *
 * @param aChecks The waiting checks, oldest first.
 * @param aCheck The check for the newest change.
 */
inline void Queue(TiltedPhoques::Vector<PendingCheck>& aChecks, PendingCheck aCheck) noexcept
{
    const auto cWaiting = std::find_if(aChecks.begin(), aChecks.end(), [&aCheck](const PendingCheck& acCheck) { return acCheck.FormId == aCheck.FormId; });
    if (cWaiting != aChecks.end())
    {
        aCheck.Undone = aCheck.Undone || cWaiting->Undone;
        aChecks.erase(cWaiting);
    }
    if (aChecks.size() >= kMaxChecks)
        aChecks.erase(aChecks.begin());
    aChecks.push_back(std::move(aCheck));
}
} // namespace NpcEquipmentQueue
