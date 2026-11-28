#pragma once

struct World;
struct Actor;
struct InterpolationComponent;

//! What happens to an NPC as this game takes it over from another game.
namespace Takeover
{
//! An NPC gained while its owner's reports had it moving: half a second later its legs are stopped if this game's AI has it standing.
//! Standing, that AI never had it moving, so it never stops the legs, and they would hold a stride until its next move.
void NoteGainedMoving(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept;
//! TEMPORARY: what a living NPC carries over from being a copy, read as it is gained and 3 s later, how far from its last owner's newest report it is gained, and its action trail.
void LogGain(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept;
//! TEMPORARY: the actions last replayed on an NPC gained just now, and what this game's AI performs on it in the next 6 s.
void LogTrailAtGain(const Actor* apActor, const char* acpHow) noexcept;
//! TEMPORARY: one action replayed on an NPC copy, and whether its graph took it.
void NoteReplay(uint32_t aFormId, const char* acpEvent, bool aTaken) noexcept;
//! TEMPORARY: one action this game's AI performed, kept for an NPC gained in the last 6 s.
void NoteOwnAction(uint32_t aFormId, const char* acpEvent, bool aTaken) noexcept;

/**
 * Note that a copy here used a piece of furniture, which this game's AI or an idle replayed on it made it do.
 * Read when this game gains the NPC (NoteGainedInFurniture).
 *
 * @param aFormId The copy's form id.
 * @param aFurnitureId The furniture reference's form id.
 */
void NoteCopyFurniture(uint32_t aFormId, uint32_t aFurnitureId) noexcept;

/**
 * Note an NPC gained whose copy here used a piece of furniture and sits in it by its graph alone: its graph is sent the furniture's exit a moment later.
 *
 * @param apActor The NPC gained, still a copy here.
 */
void NoteGainedInFurniture(Actor* apActor) noexcept;
//! Each frame: the stops, resets and readings that are due.
void Run(World& aWorld) noexcept;
} // namespace Takeover
