#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <Messages/ArrowNockedRequest.h>
#include <Messages/NotifyArrowNocked.h>

using namespace TiltedPhoques;

TEST_CASE("an arrow on the string keeps whose it is and whether it is there", "[arrow]")
{
    for (const bool nocked : {true, false})
    {
        ArrowNockedRequest request{};
        request.Nocked = nocked;

        Buffer buffer(32);
        Buffer::Writer writer(&buffer);
        request.SerializeRaw(writer);
        Buffer::Reader reader(&buffer);
        ArrowNockedRequest back{};
        back.DeserializeRaw(reader);
        REQUIRE(back == request);

        NotifyArrowNocked notify{};
        notify.ServerId = 0xFFFFFFFFu;
        notify.Nocked = nocked;

        Buffer notifyBuffer(32);
        Buffer::Writer notifyWriter(&notifyBuffer);
        notify.SerializeRaw(notifyWriter);
        Buffer::Reader notifyReader(&notifyBuffer);
        NotifyArrowNocked notifyBack{};
        notifyBack.DeserializeRaw(notifyReader);
        REQUIRE(notifyBack == notify);
    }
}

TEST_CASE("an arrow in the hand keeps which hand and where it sits", "[arrow]")
{
    ArrowNockedRequest request{};
    request.InHand = true;
    request.HandSide = 1;
    request.HandOffset = glm::vec3{-3.5f, 66.25f, 1.f / 3.f};
    request.HandRotation = glm::normalize(glm::quat{0.9f, 0.1f, -0.3f, 0.2f});

    Buffer buffer(64);
    Buffer::Writer writer(&buffer);
    request.SerializeRaw(writer);
    Buffer::Reader reader(&buffer);
    ArrowNockedRequest back{};
    back.DeserializeRaw(reader);
    REQUIRE(back == request);

    NotifyArrowNocked notify{};
    notify.ServerId = 0x12345u;
    notify.InHand = true;
    notify.HandSide = 0;
    notify.HandOffset = request.HandOffset;
    notify.HandRotation = request.HandRotation;

    Buffer notifyBuffer(64);
    Buffer::Writer notifyWriter(&notifyBuffer);
    notify.SerializeRaw(notifyWriter);
    Buffer::Reader notifyReader(&notifyBuffer);
    NotifyArrowNocked notifyBack{};
    notifyBack.DeserializeRaw(notifyReader);
    REQUIRE(notifyBack == notify);
    REQUIRE(notifyBack.HandSide == 0);
}
