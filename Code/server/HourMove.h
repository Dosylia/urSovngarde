#pragma once

#include <DateTime.h>

#include <optional>

/**
 * How far a plugin's hour move takes the shared clock forward: to the next time it is the hour asked for, never back.
 * Plain values in and out, so the unit tests check it without a server (HourMoveService applies it).
 */
namespace HourMove
{
/** An hour the clock passed by no more than this, in game hours (six game minutes), counts as reached, not a day away. */
constexpr float kJustPassedHours = 0.1f;

/** A move shorter than this, in game hours (one game minute), moves nothing. */
constexpr float kShortestMoveHours = 1.f / 60.f;

/**
 * Work out how far to move the clock forward to reach an hour.
 *
 * @param aNow The clock's hour now, from 0 to 24.
 * @param aTarget The hour asked for, from 0 to 24.
 * @return The hours to move; nothing when it is that hour already, the clock just passed it, or a number is broken.
 */
std::optional<float> HoursUntil(float aNow, float aTarget) noexcept;
} // namespace HourMove
