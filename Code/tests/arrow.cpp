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
