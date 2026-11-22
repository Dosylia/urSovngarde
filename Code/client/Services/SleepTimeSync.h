#pragma once

#include <DateTime.h>

#include <cmath>
#include <optional>

namespace SleepTimeSync
{
// Calendar sync chooses the date's origin. Elapsed sleep still advances a personal calendar when it is off.
inline void ApplyServerTime(DateTime& aClock, const TimeModel& acServer, const TimeModel& acLocal, bool aSyncCalendar) noexcept
{
    aClock.m_timeModel = aSyncCalendar ? acServer : acLocal;
    aClock.m_timeModel.Time = acServer.Time;
    aClock.m_timeModel.TimeScale = acServer.TimeScale;
}

/**
 * Move a personal calendar by the hours the shared clock moved, before the server's resync.
 * A resync keeps this game's own date, so hours past midnight would be lost without this step.
 *
 * @param aClock The game's online clock.
 * @param aHours How far the shared clock moved, in game hours.
 * @return The GameDaysPassed correction, in days; nothing for no hours or a broken number.
 */
inline std::optional<float> AdvancePersonalClock(DateTime& aClock, float aHours) noexcept
{
    if (!std::isfinite(aHours) || aHours <= 0.f || aHours > DateTime::kHoursPerDay)
        return std::nullopt;

    aClock.AdvanceHours(aHours);
    return aHours / DateTime::kHoursPerDay;
}
}
