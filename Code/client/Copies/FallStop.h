#pragma once

#include <cstdint>

/**
 * A stop for a copy's ragdoll that has fallen through the ground and keeps falling: a guard, not a fix.
 * A ragdoll far below where its owner's game has the body, whose own height keeps dropping, has nothing under it.
 * Such a copy is switched off and removed here; its owner's game keeps the real body, this game asks for it again when it announces its cell, and the copy made then starts afresh.
 * Only copies this game made are switched off (CopyRegistry::IsOwnCopy): a placed reference used as a copy, or an actor the game made itself, is left to PlaceCorpse.
 */
namespace FallStop
{
// Far past any lift (the owner's height plus 20) and any slope a settled body lies on.
constexpr float kFallenAwayDepth = 2048.f;
// The ragdoll's own height dropped by this much since the last look: still falling.
constexpr float kSinkingStep = 64.f;
// Looks further apart than this say nothing of a fall: the owner may have carried the body in between.
constexpr float kLookGapSeconds = 1.5f;

/**
 * The rule: a ragdoll is falling away when it lies deep under its owner's corpse and its own height dropped since a recent look.
 *
 * @param aDepth How far below where its owner has the body the ragdoll lies, in units (negative is above).
 * @param aDropSinceLastLook How far the ragdoll's own height dropped since the last look, in units.
 * @param aSecondsSinceLastLook How long ago that look was.
 * @return True when the ragdoll is falling away.
 */
constexpr bool SankAway(const float aDepth, const float aDropSinceLastLook, const float aSecondsSinceLastLook) noexcept
{
    return aDepth > kFallenAwayDepth && aDropSinceLastLook >= kSinkingStep && aSecondsSinceLastLook <= kLookGapSeconds;
}

/**
 * Note a look PlaceCorpse takes at a copy's ragdoll, and decide by SankAway whether it is falling away.
 *
 * @param aFormId The copy.
 * @param aDepth How far below where its owner has the body its ragdoll lies, in units (negative is above).
 * @param aRootHeight The height of the ragdoll's root now.
 * @return True when it is falling away: the caller switches the copy off.
 */
bool NoteLookAndCheckFall(uint32_t aFormId, float aDepth, float aRootHeight) noexcept;

/**
 * Forget the looks taken at a copy, so a new copy under the same id starts afresh.
 *
 * @param aFormId The copy.
 */
void Forget(uint32_t aFormId) noexcept;
} // namespace FallStop
