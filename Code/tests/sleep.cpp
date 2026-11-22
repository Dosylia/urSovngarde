#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <DateTime.h>
#include <Structs/ServerSettings.h>
#include <Services/SleepTimeSync.h>

#include <limits>

using namespace TiltedPhoques;

TEST_CASE("elapsed sleep retains minute precision at Skyrim calendar years", "[sleep]")
{
    TimeModel model{};
    model.Year = GENERATE(201u, 999u);
    model.Month = 3;
    model.Day = 10;
    model.Time = 12.f;
    const DateTime cStart(model);
    const float cHours = GENERATE(1.f / 60.f, 0.25f, 1.f, 8.f, 24.f);
    DateTime end(model);
    end.AdvanceHours(cHours);
    REQUIRE((end.GetTimeInDays() - cStart.GetTimeInDays()) * 24.0 == Approx(cHours).margin(0.000002));
}

TEST_CASE("aborted sleep rolls back provisional dates while keeping ordinary elapsed time", "[sleep]")
{
    TimeModel model{};
    model.Year = 201;
    model.Month = GENERATE(0u, 1u, 11u);
    model.Day = DateTime::GetNumberOfDaysByMonthIndex(model.Month);
    model.Time = 23.f;
    DateTime watcher(model);
    DateTime server(model);
    watcher.AdvanceHours(8.f);
    watcher.Update(60000);
    server.Update(60000);
    watcher.AdvanceHours(-8.f);
    REQUIRE(watcher.m_timeModel.Time == Approx(server.m_timeModel.Time));
    REQUIRE(watcher.m_timeModel.Day == server.m_timeModel.Day);
    REQUIRE(watcher.m_timeModel.Month == server.m_timeModel.Month);
    REQUIRE(watcher.m_timeModel.Year == server.m_timeModel.Year);
}

TEST_CASE("a clock rounded to midnight also advances its date", "[sleep]")
{
    TimeModel model{};
    model.Time = std::nextafter(24.f, 0.f);
    DateTime clock(model);
    clock.AdvanceHours(0.000001f);
    REQUIRE(clock.m_timeModel.Time == 0.f);
    REQUIRE(clock.m_timeModel.Day == model.Day + 1);
}

TEST_CASE("hours slept past midnight move the date", "[sleep]")
{
    TimeModel model{};
    model.Time = 22.f;
    model.Day = 10;
    model.Month = 3;
    model.Year = 201;

    DateTime clock(model);
    clock.AdvanceHours(8.f);

    REQUIRE(clock.m_timeModel.Time == Approx(6.f));
    REQUIRE(clock.m_timeModel.Day == 11);
    REQUIRE(clock.m_timeModel.Month == 3);
    REQUIRE(clock.m_timeModel.Year == 201);
}

TEST_CASE("a personal calendar and days passed advance by the hours moved, before the resync", "[sleep]")
{
    const uint32_t cMonth = GENERATE(0u, 1u, 11u);
    const float cHours = GENERATE(8.f, 24.f);
    TimeModel personal{};
    personal.Year = 201;
    personal.Month = cMonth;
    personal.Day = DateTime::GetNumberOfDaysByMonthIndex(cMonth);
    personal.Time = 22.f;
    DateTime clock(personal);
    float daysPassed = 90.f;

    // An unrelated player's calendar can be years ahead. Only the hour is shared in this setting.
    TimeModel server = personal;
    server.Year = 250;
    server.Month = 3;
    server.Day = 10;
    SleepTimeSync::ApplyServerTime(clock, server, personal, false);

    const auto cDays = SleepTimeSync::AdvancePersonalClock(clock, cHours);
    REQUIRE(cDays.has_value());
    daysPassed += *cDays;
    REQUIRE(clock.m_timeModel.Time == Approx(cHours == 8.f ? 6.f : 22.f));
    REQUIRE(clock.m_timeModel.Day == 1);
    REQUIRE(clock.m_timeModel.Month == (cMonth + 1) % 12);
    REQUIRE(clock.m_timeModel.Year == (cMonth == 11 ? 202 : 201));
    REQUIRE(daysPassed == Approx(90.f + cHours / 24.f));

    DateTime serverClock(server);
    serverClock.AdvanceHours(cHours);
    const auto cCommitted = clock.m_timeModel;
    // The service publishes the committed clock before the resync snapshots the game's globals.
    // Repeated normal resyncs must neither undo that date nor add the sleep twice.
    for (int i = 0; i < 2; ++i)
    {
        const TimeModel localGlobals = clock.m_timeModel;
        SleepTimeSync::ApplyServerTime(clock, serverClock.m_timeModel, localGlobals, false);
        REQUIRE(clock.m_timeModel == cCommitted);
        REQUIRE(daysPassed == Approx(90.f + cHours / 24.f));
    }
}

TEST_CASE("a move of no hours changes no calendar or elapsed days", "[sleep]")
{
    TimeModel start{};
    start.Time = 22.f;
    DateTime clock(start);
    const auto cDays = SleepTimeSync::AdvancePersonalClock(clock, 0.f);
    REQUIRE_FALSE(cDays);
    REQUIRE(clock.m_timeModel == start);
}

TEST_CASE("invalid hours leave the personal calendar untouched", "[sleep]")
{
    const float cHours = GENERATE(-1.f, 25.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    TimeModel start{};
    DateTime clock(start);
    REQUIRE_FALSE(SleepTimeSync::AdvancePersonalClock(clock, cHours));
    REQUIRE(clock.m_timeModel == start);
}

TEST_CASE("a move respects full calendar sync without counting server date offsets as elapsed days", "[sleep]")
{
    TimeModel local{};
    local.Year = 201;
    local.Time = 22.f;
    DateTime clock(local);
    const auto cDays = SleepTimeSync::AdvancePersonalClock(clock, 8.f);
    REQUIRE(cDays.has_value());
    REQUIRE(*cDays == Approx(8.f / 24.f));
    TimeModel server = clock.m_timeModel;
    server.Year = 250;
    SleepTimeSync::ApplyServerTime(clock, server, clock.m_timeModel, true);
    REQUIRE(clock.m_timeModel == server);
}

TEST_CASE("an ordinary admin time resync does not invent a sleep duration", "[sleep]")
{
    TimeModel local{};
    local.Time = 22.f;
    DateTime clock(local);
    TimeModel server = local;
    server.Year = 250;
    server.Time = 6.f;
    SleepTimeSync::ApplyServerTime(clock, server, local, false);
    REQUIRE(clock.m_timeModel.Time == 6.f);
    REQUIRE(clock.m_timeModel.Day == local.Day);
    REQUIRE(clock.m_timeModel.Month == local.Month);
    REQUIRE(clock.m_timeModel.Year == local.Year);
}

TEST_CASE("server settings carry whether a plugin's hour move can be granted", "[sleep]")
{
    for (const bool cSleepEnabled : {false, true})
    {
        ServerSettings settings{};
        settings.AutoPartyJoin = true;
        settings.SleepEnabled = cSleepEnabled;

        Buffer buffer(32);
        Buffer::Writer writer(&buffer);
        settings.Serialize(writer);
        Buffer::Reader reader(&buffer);
        ServerSettings back{};
        back.Deserialize(reader);
        REQUIRE(back.SleepEnabled == cSleepEnabled);
        REQUIRE(back.AutoPartyJoin);
        REQUIRE(back == settings);
    }
}
