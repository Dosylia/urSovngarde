#pragma once

struct Actor;

// A player's copy is not drawn until it has been put where its owner is, so it does not visibly jump after spawning.
// The position the server stored can be wrong in height, and the first movement update corrects it.
// The copy's 3D stays hidden until the interpolation first places it from a movement update, or 1.5 s at most.
// Only living players' copies are hidden, because an NPC standing still sends no movement.
// This is the hidden flag rather than a fade with Actor.SetAlpha, because a failed fade would leave the copy invisible.
namespace SpawnReveal
{
void HideUntilPlaced(Actor* apActor) noexcept;
void Placed(Actor* apActor) noexcept;
void Tick() noexcept;
} // namespace SpawnReveal
