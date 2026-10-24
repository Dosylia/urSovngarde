#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <DateTime.h>
#include <Messages/RequestSleep.h>
#include <Messages/NotifySleep.h>
#include <Structs/ServerSettings.h>
#include <SleepSession.h>
#include <Services/SleepTimeSync.h>

#include <limits>

using namespace TiltedPhoques;

TEST_CASE("a sleep step keeps its step, hours and combat flag", "[sleep]")
{
    for (const auto step :
         {RequestSleep::Step::kAsk, RequestSleep::Step::kProgress, RequestSleep::Step::kFinish, RequestSleep::Step::kCancel, RequestSleep::Step::kCombat,
          RequestSleep::Step::kHeartbeat})
    {
        RequestSleep request{};
        request.SleepStep = step;
        request.HoursSlept = 7.75f;
        request.InCombat = step == RequestSleep::Step::kCombat;

        Buffer buffer(32);
        Buffer::Writer writer(&buffer);
        request.SerializeRaw(writer);
        Buffer::Reader reader(&buffer);
        RequestSleep back{};
        back.DeserializeRaw(reader);
        REQUIRE(back == request);
    }
}

TEST_CASE("a sleep notification keeps whose sleep it is", "[sleep]")
{
    const auto cEvent = GENERATE(
        NotifySleep::Event::kGranted, NotifySleep::Event::kRefusedDisabled, NotifySleep::Event::kRefusedNotAllowed, NotifySleep::Event::kRefusedBusy,
        NotifySleep::Event::kRefusedCombat, NotifySleep::Event::kProgress, NotifySleep::Event::kFinished, NotifySleep::Event::kRevoked, NotifySleep::Event::kRefusedPublic);
    NotifySleep notify{};
    notify.SleepEvent = cEvent;
    notify.PlayerId = 0xFFFFFFFFu;
    notify.HoursSlept = 23.5f;

    Buffer buffer(32);
    Buffer::Writer writer(&buffer);
    notify.SerializeRaw(writer);
    Buffer::Reader reader(&buffer);
    NotifySleep back{};
    back.DeserializeRaw(reader);
    REQUIRE(back == notify);
}

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

TEST_CASE("an open sleep chooser keeps its lease without inventing progress", "[sleep]")
{
    SleepSession session;
    session.Start(1, 1000);
    RequestSleep heartbeat{};
    heartbeat.SleepStep = RequestSleep::Step::kHeartbeat;
    for (uint64_t tick = 6000; tick <= 181000; tick += 5000)
    {
        REQUIRE_FALSE(session.HasExpired(tick));
        const auto cHours = session.Accept(1, heartbeat, tick);
        REQUIRE(cHours.has_value());
        REQUIRE(*cHours == 0.f);
    }
    REQUIRE_FALSE(session.HasExpired(240999));
    REQUIRE(session.HasExpired(241000));
    // A late packet cannot silently revive an expired grant.
    REQUIRE_FALSE(session.Accept(1, heartbeat, 241000));
    session.Reset();
    session.Start(2, 241000);
    REQUIRE_FALSE(session.Accept(1, heartbeat, 241001));
    REQUIRE(session.Accept(2, heartbeat, 241001).has_value());
}

TEST_CASE("non-finite sleep durations neither reach the calendar nor renew the lease", "[sleep]")
{
    const auto cStep = GENERATE(RequestSleep::Step::kProgress, RequestSleep::Step::kFinish, RequestSleep::Step::kCancel, RequestSleep::Step::kHeartbeat);
    const float cHours = GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
    SleepSession session;
    session.Start(1, 1000);
    RequestSleep request{};
    request.SleepStep = cStep;
    request.HoursSlept = cHours;
    REQUIRE_FALSE(session.Accept(1, request, 60000));
    REQUIRE(session.HasExpired(61000));
}

TEST_CASE("only recognized sleep packets from the owner renew a lease", "[sleep]")
{
    SleepSession session;
    session.Start(1, 1000);
    RequestSleep request{};
    request.SleepStep = RequestSleep::Step::kProgress;
    request.HoursSlept = 8.f;
    REQUIRE_FALSE(session.Accept(2, request, 60000));
    request.SleepStep = static_cast<RequestSleep::Step>(255);
    REQUIRE_FALSE(session.Accept(1, request, 60000));
    REQUIRE(session.HasExpired(61000));
    session.Reset();
    request.SleepStep = RequestSleep::Step::kFinish;
    REQUIRE_FALSE(session.Accept(1, request, 60000));
}

TEST_CASE("valid sleep progress is bounded by the game's duration limits", "[sleep]")
{
    SleepSession session;
    session.Start(1, 1000);
    RequestSleep request{};
    request.SleepStep = RequestSleep::Step::kProgress;
    request.HoursSlept = -1.f;
    REQUIRE(session.Accept(1, request, 2000) == 0.f);
    request.HoursSlept = 25.f;
    REQUIRE(session.Accept(1, request, 3000) == 24.f);
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

TEST_CASE("a non-party player's personal calendar and days passed advance before the sleep resync", "[sleep]")
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

    const auto cDays = SleepTimeSync::CommitSleep(clock, 3, 1, cHours, std::nullopt, 0.f);
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

TEST_CASE("a party member commits only sleep hours not already applied as progress", "[sleep]")
{
    const float cProgress = GENERATE(0.f, 3.f, 8.f, 10.f);
    const float cFinalHours = GENERATE(0.f, 8.f);
    TimeModel start{};
    start.Year = 201;
    start.Month = 11;
    start.Day = 31;
    start.Time = 22.f;
    DateTime clock(start);
    clock.AdvanceHours(cProgress);
    float daysPassed = 90.f + cProgress / 24.f;

    const auto cDays = SleepTimeSync::CommitSleep(clock, 2, 1, cFinalHours, 1, cProgress);
    REQUIRE(cDays.has_value());
    daysPassed += *cDays;
    REQUIRE(clock.m_timeModel.Time == Approx(cFinalHours == 0.f ? 22.f : 6.f));
    REQUIRE(clock.m_timeModel.Day == (cFinalHours == 0.f ? 31u : 1u));
    REQUIRE(clock.m_timeModel.Month == (cFinalHours == 0.f ? 11u : 0u));
    REQUIRE(clock.m_timeModel.Year == (cFinalHours == 0.f ? 201u : 202u));
    REQUIRE(daysPassed == Approx(90.f + cFinalHours / 24.f));
}

TEST_CASE("a native sleeper never receives its own elapsed sleep a second time", "[sleep]")
{
    TimeModel native{};
    native.Year = 202;
    native.Month = 0;
    native.Day = 1;
    native.Time = 6.f;
    DateTime clock(native);
    REQUIRE_FALSE(SleepTimeSync::CommitSleep(clock, 1, 1, 8.f, std::nullopt, 0.f));
    REQUIRE(clock.m_timeModel == native);
}

TEST_CASE("a cancelled or timed-out observer can accept a later committed sleep as a non-party player", "[sleep]")
{
    TimeModel start{};
    start.Time = 22.f;
    DateTime clock(start);
    clock.AdvanceHours(5.f);
    float daysPassed = 90.f + 5.f / 24.f;
    const auto cRollback = SleepTimeSync::CommitSleep(clock, 2, 1, 0.f, 1, 5.f);
    REQUIRE(cRollback.has_value());
    daysPassed += *cRollback;
    REQUIRE(clock.m_timeModel == start);
    REQUIRE(daysPassed == Approx(90.f));

    const auto cCommit = SleepTimeSync::CommitSleep(clock, 2, 1, 8.f, std::nullopt, 0.f);
    REQUIRE(cCommit.has_value());
    daysPassed += *cCommit;
    REQUIRE(clock.m_timeModel.Time == Approx(6.f));
    REQUIRE(clock.m_timeModel.Day == start.Day + 1);
    REQUIRE(daysPassed == Approx(90.f + 8.f / 24.f));
}

TEST_CASE("a non-party cancellation does not change its calendar or elapsed days", "[sleep]")
{
    TimeModel start{};
    start.Time = 22.f;
    DateTime clock(start);
    const auto cDays = SleepTimeSync::CommitSleep(clock, 3, 1, 0.f, std::nullopt, 0.f);
    REQUIRE_FALSE(cDays);
    REQUIRE(clock.m_timeModel == start);
}

TEST_CASE("invalid committed sleep durations leave the personal calendar untouched", "[sleep]")
{
    const float cHours = GENERATE(-1.f, 25.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    TimeModel start{};
    DateTime clock(start);
    REQUIRE_FALSE(SleepTimeSync::CommitSleep(clock, 3, 1, cHours, std::nullopt, 0.f));
    REQUIRE(clock.m_timeModel == start);
}

TEST_CASE("sleep commits respect full calendar sync without counting server date offsets as elapsed days", "[sleep]")
{
    TimeModel local{};
    local.Year = 201;
    local.Time = 22.f;
    DateTime clock(local);
    const auto cDays = SleepTimeSync::CommitSleep(clock, 3, 1, 8.f, std::nullopt, 0.f);
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

TEST_CASE("server settings carry whether a sleep can be granted", "[sleep]")
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
