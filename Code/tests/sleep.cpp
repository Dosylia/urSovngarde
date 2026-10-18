#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <DateTime.h>
#include <Messages/RequestSleep.h>
#include <Messages/NotifySleep.h>
#include <SleepSession.h>

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
        NotifySleep::Event::kRefusedCombat, NotifySleep::Event::kProgress, NotifySleep::Event::kFinished, NotifySleep::Event::kRevoked);
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
