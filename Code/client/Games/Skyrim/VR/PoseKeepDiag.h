#pragma once

#include <cstdint>

/**
 * TEMPORARY: whether another player's posed copy stays where the frame end put it, for the copy that jumps between two places.
 * The frame end writes the pose after the frame is drawn, so it is drawn only if nothing moves the bones again before the next draw.
 * One line per copy every 10 s, written by the game that watches the copy.
 */
namespace PoseKeepDiag
{
/**
 * Count one frame end at which a live copy of another player is posed.
 *
 * @param aFormId The copy's form id.
 * @param aStillWhereLeft True when its pelvis was still where the last frame end put it, false when something moved it since.
 * @param aBodyShift How far across the ground, in game units, this frame end moved the whole body to put its hips where the pose has them.
 */
void NotePosed(uint32_t aFormId, bool aStillWhereLeft, float aBodyShift) noexcept;

/**
 * Count one position the interpolation gives a copy of another player.
 *
 * @param aFormId The copy's form id.
 * @param aMovedAtOnce True when the game moves its 3D at once, false when its character controller takes it to the next physics step.
 */
void NotePlaced(uint32_t aFormId, bool aMovedAtOnce) noexcept;
} // namespace PoseKeepDiag
