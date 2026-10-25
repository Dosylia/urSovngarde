#pragma once

#include <DateTime.h>
#include <TiltedCore/Math.hpp>

namespace GameClockFrame
{
constexpr float kTransitionSpeed = 5.f;

// The game's clock globals (TimeData), as plain values.
struct Globals
{
    float TimeScale = 0.f;
    float GameHour = 0.f;
    float GameDay = 0.f;
    float GameMonth = 0.f;
    float GameYear = 0.f;
    float GameDaysPassed = 0.f;
};

inline float Interpolate(const TimeModel& aFrom, const TimeModel& aTo, float aFadeTimer) noexcept
{
    const auto t = aTo.Time - aFrom.Time;
    if (t < 0.f)
    {
        const auto v = t + 24.f;
        // interpolate on the time difference, not the time
        const auto x = TiltedPhoques::Lerp(0.f, v, aFadeTimer / kTransitionSpeed) + aFrom.Time;

        return TiltedPhoques::Mod(x, 24.f);
    }

    return TiltedPhoques::Lerp(aFrom.Time, aTo.Time, aFadeTimer / kTransitionSpeed);
}

// One frame of the online clock. The baseline always advances; the game's globals follow it only while
// the clock is locked, which returns true. A granted sleeper unlocks it so the game can fast-forward.
inline bool Step(DateTime& aOnline, const TimeModel& acOffline, uint64_t aDeltaTick, float aFrameDelta, float& aFadeTimer, bool aLocked,
                 Globals& aGlobals) noexcept
{
    aOnline.Update(aDeltaTick);
    if (!aLocked)
        return false;

    aGlobals.TimeScale = aOnline.m_timeModel.TimeScale;
    aGlobals.GameDay = aOnline.m_timeModel.Day;
    aGlobals.GameMonth = aOnline.m_timeModel.Month;
    aGlobals.GameYear = aOnline.m_timeModel.Year;
    aGlobals.GameDaysPassed += aOnline.GetDeltaTime(aDeltaTick) * (1.f / 24.f);

    // time transition in
    if (aFadeTimer < kTransitionSpeed)
    {
        aGlobals.GameHour = Interpolate(acOffline, aOnline.m_timeModel, aFadeTimer);
        aFadeTimer += aFrameDelta;
    }
    else
        aGlobals.GameHour = aOnline.m_timeModel.Time;
    return true;
}
}
