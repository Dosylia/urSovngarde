#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <DateTime.h>
#include <Messages/RequestSleep.h>
#include <Messages/NotifySleep.h>

using namespace TiltedPhoques;

TEST_CASE("a sleep step keeps its step, hours and combat flag", "[sleep]")
{
    for (const auto step : {RequestSleep::Step::kAsk, RequestSleep::Step::kProgress, RequestSleep::Step::kFinish, RequestSleep::Step::kCancel, RequestSleep::Step::kCombat})
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
    NotifySleep notify{};
    notify.SleepEvent = NotifySleep::Event::kRefusedCombat;
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
