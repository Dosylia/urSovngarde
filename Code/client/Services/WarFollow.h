#pragma once

#include <Structs/WarState.h>

#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * How a party member's game follows the leader's civil war without undoing its own.
 * A member's journal takes only what the leader's war has and its own lacks, and the leader's holds are not taken while the leader's war is behind.
 * Header-only, without the game, so that the tests can check it.
 */
namespace WarFollow
{
/**
 * Check whether an objective is finished: completed or failed.
 *
 * @param aFlags The objective's WarState::ObjectiveFlags.
 * @return True when it is completed or failed.
 */
constexpr bool IsFinished(const uint8_t aFlags) noexcept
{
    return (aFlags & (WarState::kCompleted | WarState::kFailed)) != 0;
}

/**
 * Choose the flags a member adds to one objective to follow the leader.
 * Only flags the leader's objective has and this one lacks are added.
 * An objective already completed or failed in this game can only be shown, so it never changes its outcome.
 * No flag is ever taken away, so a member's journal never loses a completed, failed or shown objective.
 *
 * @param aHere The objective's flags in this game.
 * @param aLeader The objective's flags in the leader's game.
 * @return The flags to add; 0 when there is nothing to add.
 */
constexpr uint8_t FlagsToAdd(const uint8_t aHere, const uint8_t aLeader) noexcept
{
    const uint8_t cMissing = static_cast<uint8_t>(aLeader & ~aHere);
    return IsFinished(aHere) ? static_cast<uint8_t>(cMissing & WarState::kDisplayed) : cMissing;
}

/**
 * Find an objective this game's war finished that the leader's war has not finished.
 * One such objective means the leader's war is behind this game's, so the leader's holds would undo this game's own wins.
 *
 * @param acHere The objectives' flags in this game, in the order of WarState::kObjectives.
 * @param acLeader The objectives' flags in the leader's game, in the same order.
 * @return The first such objective's index in WarState::kObjectives, or nothing when the leader's war is not behind.
 */
inline std::optional<size_t> FirstFinishedOnlyHere(const decltype(WarState::Objectives)& acHere, const decltype(WarState::Objectives)& acLeader) noexcept
{
    for (size_t i = 0; i < acHere.size(); ++i)
        if (IsFinished(acHere[i]) && !IsFinished(acLeader[i]))
            return i;
    return std::nullopt;
}
} // namespace WarFollow
