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
//! TEMPORARY: what a living NPC carries over from being a copy, read as it is gained and 3 s later, and how far from its last owner's newest report it is gained.
void LogGain(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept;
//! Each frame: the stops and readings that are due.
void Run(World& aWorld) noexcept;
} // namespace Takeover
