#pragma once

#include <optional>
#include <unordered_set>
#include <utility>

//! The temporary actors this client made itself, and the references some of them stand in for.
//! One place answers "is this one of our copies" and "which copy stands in for which reference", for every service.
namespace CopyRegistry
{
// Own copies: form ids of the temporary actors this client made itself, copies of other players and of their creatures.
// Only these are this client's to disable or delete; game-spawned temporary actors can be remote too.
// Disabling a game actor while the game disposes of it crashes in its movement code (SkyrimVR.exe+0x714EB2).
// An id leaves the set as soon as its copy is gone, because the game reuses a deleted actor's form id (`live-temp-reuse`).
// When in doubt an id is removed: a copy left standing is harmless, disabling a game actor is the crash.
bool IsOwnCopy(uint32_t aFormId) noexcept;
void AddOwnCopy(uint32_t aFormId) noexcept;
void ForgetOwnCopy(uint32_t aFormId) noexcept;
const std::unordered_set<uint32_t>& OwnCopies() noexcept;
void ForgetAllOwnCopies() noexcept;

// Stand-ins: references the owner rolled as another kind of creature, or that are not loaded here, keyed by the reference (the "ghost").
// The owner's creature is spawned here as a copy instead: adopting the wrong creature freezes it or makes it slide.
// The ghost is disabled while its copy stands in, and is never assigned to the server.
// A ghost is enabled again when its copy is deleted, on disconnect, or when this client goes away.
// The save guard (Actor.cpp) writes a ghost to a save as enabled, so a save made meanwhile does not keep it disabled.
// A copy of 0: no copy yet, as for this player's own follower while another player's stands far away.
void SetStandIn(uint32_t aGhost, uint32_t aCopy) noexcept;
bool IsGhost(uint32_t aFormId) noexcept;
//! The copy standing in for that reference (0 for none yet); nothing when it is no ghost.
std::optional<uint32_t> StandInFor(uint32_t aGhost) noexcept;
//! Whether it was a ghost.
bool ForgetGhost(uint32_t aGhost) noexcept;
//! The reference that copy stood in for, which is a ghost no longer.
std::optional<uint32_t> TakeGhostOfCopy(uint32_t aCopy) noexcept;
//! The ghost and its copy for an id that is either of them.
std::optional<std::pair<uint32_t, uint32_t>> StandInEntryOf(uint32_t aGhostOrCopy) noexcept;
const TiltedPhoques::Map<uint32_t, uint32_t>& Ghosts() noexcept;
//! All ghosts and their copies, which are ghosts no longer.
TiltedPhoques::Map<uint32_t, uint32_t> TakeAllGhosts() noexcept;
//! The copies standing in for a ghost now.
TiltedPhoques::Set<uint32_t> StandInCopies() noexcept;
} // namespace CopyRegistry
