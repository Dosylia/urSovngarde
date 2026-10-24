#include <catch2/catch.hpp>

#include <DateTime.h>
#include <Services/GameClockFrame.h>

#include <cmath>

namespace
{
// main's DateTime::Update and CalendarService::HandleUpdate before sleep support, verbatim apart from
// members becoming parameters. The ordinary clock must match them exactly, frame by frame.
void LegacyUpdate(TimeModel& aModel, uint64_t aDeltaTick)
{
    const float cDeltaSeconds = static_cast<float>(aDeltaTick) / 1000.f;
    aModel.Time += cDeltaSeconds * (aModel.TimeScale * 0.00027777778f);

    aModel.Day += static_cast<uint32_t>(aModel.Time / 24.f);
    aModel.Time = std::fmod(aModel.Time, 24.f);

    while (aModel.Day > DateTime::GetNumberOfDaysByMonthIndex(aModel.Month))
    {
        aModel.Day -= DateTime::GetNumberOfDaysByMonthIndex(aModel.Month);
        aModel.Month++;
    }
    aModel.Year += aModel.Month / 12;
    aModel.Month %= 12;
}

float LegacyInterpolate(const TimeModel& aFrom, const TimeModel& aTo, float aFadeTimer)
{
    const auto t = aTo.Time - aFrom.Time;
    if (t < 0.f)
    {
        const auto v = t + 24.f;
        const auto x = TiltedPhoques::Lerp(0.f, v, aFadeTimer / 5.f) + aFrom.Time;
        return TiltedPhoques::Mod(x, 24.f);
    }
    return TiltedPhoques::Lerp(aFrom.Time, aTo.Time, aFadeTimer / 5.f);
}

void LegacyFrame(TimeModel& aOnline, const TimeModel& acOffline, uint64_t aDeltaTick, float aFrameDelta, float& aFadeTimer,
                 GameClockFrame::Globals& aGlobals)
{
    LegacyUpdate(aOnline, aDeltaTick);
    aGlobals.TimeScale = aOnline.TimeScale;
    aGlobals.GameDay = aOnline.Day;
    aGlobals.GameMonth = aOnline.Month;
    aGlobals.GameYear = aOnline.Year;
    aGlobals.GameDaysPassed += (static_cast<float>(aDeltaTick) / 1000.f * (aOnline.TimeScale * 0.00027777778f)) * (1.f / 24.f);

    if (aFadeTimer < 5.f)
    {
        aGlobals.GameHour = LegacyInterpolate(acOffline, aOnline, aFadeTimer);
        aFadeTimer += aFrameDelta;
    }
    else
        aGlobals.GameHour = aOnline.Time;
}

bool SameGlobals(const GameClockFrame::Globals& acLhs, const GameClockFrame::Globals& acRhs)
{
    return acLhs.TimeScale == acRhs.TimeScale && acLhs.GameHour == acRhs.GameHour && acLhs.GameDay == acRhs.GameDay && acLhs.GameMonth == acRhs.GameMonth &&
           acLhs.GameYear == acRhs.GameYear && acLhs.GameDaysPassed == acRhs.GameDaysPassed;
}

// Typical frame times, a zero-length frame, a hitch and a loading screen.
constexpr uint64_t kFrameTicks[] = {16, 17, 16, 33, 0, 11, 16, 250, 16, 7, 5000, 16};

TimeModel StartModel(float aTimeScale, uint32_t aMonth, float aHour)
{
    TimeModel model{};
    model.TimeScale = aTimeScale;
    model.Year = 201;
    model.Month = aMonth;
    model.Day = DateTime::GetNumberOfDaysByMonthIndex(aMonth);
    model.Time = aHour;
    return model;
}
} // namespace

TEST_CASE("the per-frame clock advances exactly as on main", "[clock]")
{
    const float cTimeScale = GENERATE(1.f, 20.f, 1000.f, 20000.f);
    const uint32_t cMonth = GENERATE(0u, 1u, 11u);
    const float cHour = GENERATE(0.f, 12.f, 23.9f);

    TimeModel legacy = StartModel(cTimeScale, cMonth, cHour);
    DateTime clock(legacy);
    // Long enough to cross midnight, a month and the year at every time scale above 1.
    for (int i = 0; i < 20000; ++i)
    {
        const uint64_t cTicks = kFrameTicks[i % std::size(kFrameTicks)];
        LegacyUpdate(legacy, cTicks);
        clock.Update(cTicks);
        REQUIRE(clock.m_timeModel == legacy);
    }
}

TEST_CASE("the locked game clock writes the same globals as on main, including the fade in", "[clock]")
{
    const float cTimeScale = GENERATE(20.f, 1000.f);
    // The fade runs forwards, and backwards across midnight.
    const float cOfflineHour = GENERATE(3.f, 23.f);

    TimeModel offline = StartModel(cTimeScale, 11u, cOfflineHour);
    TimeModel legacy = StartModel(cTimeScale, 11u, 23.5f);
    DateTime online(legacy);
    GameClockFrame::Globals expected{};
    expected.GameDaysPassed = 90.f;
    GameClockFrame::Globals actual = expected;
    float legacyFade = 0.f;
    float fade = 0.f;

    for (int i = 0; i < 5000; ++i)
    {
        const uint64_t cTicks = kFrameTicks[i % std::size(kFrameTicks)];
        const float cFrameDelta = static_cast<float>(cTicks) / 1000.f;
        LegacyFrame(legacy, offline, cTicks, cFrameDelta, legacyFade, expected);
        REQUIRE(GameClockFrame::Step(online, offline, cTicks, cFrameDelta, fade, true, actual));
        REQUIRE(online.m_timeModel == legacy);
        REQUIRE(fade == legacyFade);
        REQUIRE(SameGlobals(actual, expected));
    }
    REQUIRE(fade >= GameClockFrame::kTransitionSpeed);
}

TEST_CASE("an unlocked game clock keeps the baseline running without touching the globals", "[clock]")
{
    TimeModel legacy = StartModel(20.f, 11u, 23.5f);
    DateTime online(legacy);
    GameClockFrame::Globals globals{};
    globals.GameHour = 4.f;
    globals.GameDaysPassed = 90.f;
    const GameClockFrame::Globals cUntouched = globals;
    float fade = 0.f;

    for (int i = 0; i < 1000; ++i)
    {
        const uint64_t cTicks = kFrameTicks[i % std::size(kFrameTicks)];
        LegacyUpdate(legacy, cTicks);
        REQUIRE_FALSE(GameClockFrame::Step(online, legacy, cTicks, 0.016f, fade, false, globals));
        REQUIRE(online.m_timeModel == legacy);
    }
    REQUIRE(SameGlobals(globals, cUntouched));
    REQUIRE(fade == 0.f);
}
