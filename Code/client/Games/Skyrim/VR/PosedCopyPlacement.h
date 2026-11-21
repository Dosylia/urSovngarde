#pragma once

#include <cstdint>

struct Actor;
struct NiPoint3;

/**
 * Where a posed copy of a VR player goes, given at the frame end instead of when the interpolation computes it.
 * The interpolation runs on the game's script threads at no fixed point in the frame, and placing or turning a copy rebuilds its skeleton from its animation.
 * Its pose is written at the frame end on the main thread (VRBodySync::OnFrameEnd), so the place is given there, right before the pose: placement, then pose, in that order.
 * The places kept when a frame end starts are the ones it gives; one kept while it runs waits for the next.
 */
namespace PosedCopyPlacement
{
/**
 * Check whether the frame end runs, so a place kept for it is given within a frame.
 * Without it (no renderer hook, or not yet), the interpolation places copies itself.
 *
 * @return True when the frame end ran in the last half second.
 */
bool IsFrameEndRunning() noexcept;

/**
 * Keep a posed copy's place for the frame end, instead of giving it now; a newer place replaces an older one not given yet.
 *
 * @param aFormId The copy's form id.
 * @param acPosition Where it goes.
 * @param acAngles How it turns (x, y, z in radians), as TESObjectREFR::SetRotation takes them.
 */
void KeepForFrameEnd(uint32_t aFormId, const NiPoint3& acPosition, const NiPoint3& acAngles) noexcept;

/**
 * Forget a copy's kept place: the interpolation's step for it starts over, and only a place kept in that step is given.
 *
 * @param aFormId The copy's form id.
 */
void DropKeptPlace(uint32_t aFormId) noexcept;

/**
 * At the start of the frame end: take the places kept so far, the ones this frame end gives.
 */
void StartFrameEnd() noexcept;

/**
 * Give a copy the place this frame end took for it, now: on the main thread, right before its pose is written.
 *
 * @param apActor The copy.
 * @return True when a place was given.
 */
bool GiveKeptPlace(Actor* apActor) noexcept;

/**
 * At the end of the frame end: give the places it took that were not given yet, so a copy not posed in this frame still moves, and note that the frame end runs.
 */
void FinishFrameEnd() noexcept;
} // namespace PosedCopyPlacement
