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

// Returns the GameDaysPassed correction. The native sleeper has already advanced its own globals;
// party members may have provisional progress, while everyone else needs the entire committed duration.
inline std::optional<float> CommitSleep(DateTime& aClock, uint32_t aLocalPlayerId, uint32_t aSleeperId, float aHours,
                                        std::optional<uint32_t> aWatchedSleeper, float aWatchedHours) noexcept
{
    const bool cWatching = aWatchedSleeper == aSleeperId;
    if (aSleeperId == aLocalPlayerId || !std::isfinite(aHours) || aHours < 0.f || aHours > 24.f || (!cWatching && aHours == 0.f))
        return std::nullopt;

    const float cAppliedHours = cWatching ? aWatchedHours : 0.f;
    const float cCorrection = aHours - cAppliedHours;
    aClock.AdvanceHours(cCorrection);
    return cCorrection / 24.f;
}
}
