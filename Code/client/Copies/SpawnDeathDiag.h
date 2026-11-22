#pragma once

#include <cstdint>

struct Actor;

/**
 * A diagnostic, kept until its cause is known: what makes an NPC copy die before its 3D step (CharacterService::RunRemoteUpdates).
 * Such a copy is laid down with nothing played, and in one state its ragdoll later has no ground under it; what killed it so early is not known.
 * The copy is marked from its spawn to its 3D step (ActorExtension::AwaitingFirst3D), and a death in between is logged once with its stack.
 */
namespace SpawnDeathDiag
{
/**
 * Note a life state the game gives an actor, and log it with its stack when it is the first death of a copy still waiting for its 3D step.
 * Called from the game's Actor::SetLifeState, before the state is set; an actor that is no longer a copy, or already dead, is left out.
 *
 * @param apActor The actor.
 * @param aState The life state it is given.
 * @param apCaller The address Actor::SetLifeState returns to, named where no stack can be read (flat).
 */
void NoteLifeState(Actor* apActor, uint32_t aState, const void* apCaller) noexcept;
} // namespace SpawnDeathDiag
