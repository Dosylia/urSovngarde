#include "HourMove.h"

#include <cmath>

namespace HourMove
{
/**
 * Work out how far to move the clock forward to reach an hour.
 *
 * @param aNow The clock's hour now, from 0 to 24.
 * @param aTarget The hour asked for, from 0 to 24.
 * @return The hours to move; nothing when it is that hour already, the clock just passed it, or a number is broken.
 */
std::optional<float> HoursUntil(const float aNow, const float aTarget) noexcept
{
    // Two days added first keep the remainder positive for any two hours of a day.
    const float cHours = std::fmod(aTarget - aNow + 2.f * DateTime::kHoursPerDay, DateTime::kHoursPerDay);

    // Both comparisons fail on NaN, so a broken number moves nothing.
    const bool cMoves = cHours >= kShortestMoveHours && cHours <= DateTime::kHoursPerDay - kJustPassedHours;
    if (!cMoves)
        return std::nullopt;

    return cHours;
}
} // namespace HourMove
