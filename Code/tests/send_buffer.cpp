#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/AuthenticationRequest.h>
#include <Messages/ClientMessageFactory.h>
#include <Services/SendBuffer.h>

#include <string>

using namespace TiltedPhoques;

namespace
{
// A connect request as a list the size of Mad God's Overhaul sends it: 1992 plugins, the last 190 of them light.
AuthenticationRequest LargeListRequest()
{
    AuthenticationRequest request{};
    request.Token = "token";
    request.Version = "v1.9.5";
    request.Protocol = "0a1b2c3d";
    for (uint16_t i = 0; i < 1992; ++i)
    {
        Mods::Entry entry{};
        const bool cLite = i >= 1802;
        entry.Id = cLite ? static_cast<uint16_t>(i - 1802) : static_cast<uint16_t>(i);
        entry.IsLite = cLite;
        entry.Filename = String("A Plugin Name As Long As Most In The List - ") + std::to_string(i).c_str() + ".esp";
        request.UserMods.ModList.push_back(entry);
    }
    request.Username = "Witch Queen";
    request.WorldSpaceId.ModId = 0;
    request.WorldSpaceId.BaseId = 0x3C;
    request.CellId.ModId = 0;
    request.CellId.BaseId = 0x982A;
    request.Level = 30;
    request.HideFromPublicPage = true;
    return request;
}
} // namespace

TEST_CASE("the connect request of a 2000-plugin list is sent whole, the way the client writes it", "[sendbuffer]")
{
    const AuthenticationRequest cSent = LargeListRequest();

    // The client writes every message from a per-thread scratch area (TransportService::Send); what it wrote is what is sent.
    std::vector<uint8_t> sent;
    bool fits = false;
    {
        ScratchAllocator scratch(SendBuffer::kScratchBytes);
        ScopedAllocator scope{scratch};

        Buffer buffer(SendBuffer::kBufferBytes);
        Buffer::Writer writer(&buffer);
        fits = SendBuffer::Write(cSent, buffer, writer);
        sent.assign(buffer.GetData(), buffer.GetData() + writer.Size());
    }
    const bool cFits = fits;

    // The server skips the packet layer's first byte, then reads the message.
    Buffer received(sent.data(), sent.size());
    Buffer::Reader reader(&received);
    uint64_t packetByte = 0;
    reader.ReadBits(packetByte, 8);
    const ClientMessageFactory cFactory;
    auto pMessage = cFactory.Extract(reader);
    REQUIRE(pMessage);
    REQUIRE(pMessage->GetOpcode() == cSent.GetOpcode());
    const auto pReceived = CastUnique<AuthenticationRequest>(std::move(pMessage));

    CHECK(pReceived->UserMods.ModList.size() == cSent.UserMods.ModList.size());
    CHECK(pReceived->UserMods.ModList.back().Filename == cSent.UserMods.ModList.back().Filename);
    CHECK(pReceived->Username == cSent.Username);
    CHECK(pReceived->CellId == cSent.CellId);
    CHECK(pReceived->Level == cSent.Level);
    CHECK(pReceived->HideFromPublicPage);
    REQUIRE(cFits);
}

TEST_CASE("a message larger than the send buffer is never taken as whole", "[sendbuffer]")
{
    // About 1.6 MB of plugin names: more than the buffer holds, and more than the scratch area lets it grow to.
    AuthenticationRequest request = LargeListRequest();
    request.UserMods.ModList.clear();
    for (uint16_t i = 0; i < 25000; ++i)
    {
        Mods::Entry entry{};
        entry.Id = i;
        entry.Filename = String("A Plugin Name As Long As Most In The List - ") + std::to_string(i).c_str() + ".esp";
        request.UserMods.ModList.push_back(entry);
    }

    bool fits = true;
    {
        ScratchAllocator scratch(SendBuffer::kScratchBytes);
        ScopedAllocator scope{scratch};

        Buffer buffer(SendBuffer::kBufferBytes);
        Buffer::Writer writer(&buffer);
        fits = SendBuffer::Write(request, buffer, writer);
    }
    REQUIRE_FALSE(fits);
}
