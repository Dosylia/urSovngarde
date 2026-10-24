#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <catch2/catch.hpp>

#include <MovementPacker.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientMessageFactory.h>

#include <set>
#include <utility>
#include <vector>

using TiltedPhoques::Buffer;

namespace
{
// An NPC's update: a place and a behavior graph's worth of variables.
ReferenceUpdate Npc(const float aX, const size_t aFloats = 30)
{
    ReferenceUpdate update;
    update.UpdatedMovement.CellId = GameId(0, 0x1234);
    update.UpdatedMovement.WorldSpaceId = GameId(0, 0x3C);
    update.UpdatedMovement.Position = glm::vec3(aX, 2000.f, -300.f);
    update.UpdatedMovement.Direction = 0.25f;
    update.UpdatedMovement.Variables.Booleans.assign(40, true);
    update.UpdatedMovement.Variables.Integers.assign(12, 7);
    update.UpdatedMovement.Variables.Floats.assign(aFloats, 0.5f);
    return update;
}

// A VR player's update: the NPC's, with a full upper-body pose, fingers and weapons.
ReferenceUpdate VrPlayer()
{
    ReferenceUpdate update = Npc(0.f);
    VRPose& pose = update.UpdatedVRPose;
    pose.HasData = true;
    pose.HasFingers = true;
    pose.HasWeapons = true;
    pose.WeaponHeld = {true, true};
    return update;
}

struct Sent
{
    ServerReferencesMoveRequest Message;
    bool Last;
};

// Packs the entries as the server's relay does: full messages as they fill, the last one at the end.
std::vector<Sent> Pack(const std::vector<std::pair<uint32_t, ReferenceUpdate>>& acEntries, Buffer& aScratch)
{
    using Packer = MovementPacker<ServerReferencesMoveRequest>;
    Packer packer(1234, aScratch);
    std::vector<Sent> sent;
    for (const auto& [serverId, update] : acEntries)
    {
        ServerReferencesMoveRequest full;
        if (packer.Add(serverId, update, Packer::EntryBits(aScratch, serverId, update), full))
            sent.push_back({std::move(full), false});
    }
    sent.push_back({std::move(packer.Current()), true});
    return sent;
}

size_t RealBits(Buffer& aScratch, const ServerReferencesMoveRequest& acMessage)
{
    return MovementPacker<ServerReferencesMoveRequest>::WrittenBits(aScratch, acMessage);
}
} // namespace

TEST_CASE("Movement messages fit in one packet", "[encoding.movementpacker]")
{
    Buffer scratch(1 << 16);
    constexpr size_t cMaxBits = MovementPacker<ServerReferencesMoveRequest>::kMaxBits;

    SECTION("No actors: one empty message, its header only")
    {
        const auto sent = Pack({}, scratch);
        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].Last);
        REQUIRE(sent[0].Message.Updates.empty());
        REQUIRE(RealBits(scratch, sent[0].Message) < 64);
    }

    SECTION("Many NPCs and a VR player: each message within the limit, each actor once, the last one marked")
    {
        std::vector<std::pair<uint32_t, ReferenceUpdate>> entries;
        entries.emplace_back(14, VrPlayer());
        for (uint32_t i = 0; i < 60; ++i)
            entries.emplace_back(100000 + i, Npc(static_cast<float>(i) * 100.f));

        const auto sent = Pack(entries, scratch);
        REQUIRE(sent.size() > 1);
        std::set<uint32_t> seen;
        for (size_t i = 0; i < sent.size(); ++i)
        {
            REQUIRE(RealBits(scratch, sent[i].Message) <= cMaxBits);
            REQUIRE(sent[i].Last == (i + 1 == sent.size()));
            REQUIRE(sent[i].Message.Tick == 1234);
            for (const auto& [serverId, update] : sent[i].Message.Updates)
                REQUIRE(seen.insert(serverId).second);
        }
        REQUIRE(seen.size() == entries.size());
    }

    SECTION("An actor alone over the limit goes in a message of its own")
    {
        std::vector<std::pair<uint32_t, ReferenceUpdate>> entries;
        entries.emplace_back(1, Npc(0.f));
        entries.emplace_back(2, Npc(1.f, 600));
        entries.emplace_back(3, Npc(2.f));

        const auto sent = Pack(entries, scratch);
        REQUIRE(sent.size() == 3);
        REQUIRE(sent[1].Message.Updates.size() == 1);
        REQUIRE(sent[1].Message.Updates.find(2) != sent[1].Message.Updates.end());
        REQUIRE(RealBits(scratch, sent[1].Message) > cMaxBits);
    }

    SECTION("The counted size is never below the real one")
    {
        using Packer = MovementPacker<ServerReferencesMoveRequest>;
        Packer packer(99999999, scratch);
        for (uint32_t i = 0; i < 12; ++i)
        {
            const ReferenceUpdate update = i % 4 ? Npc(static_cast<float>(i)) : VrPlayer();
            ServerReferencesMoveRequest full;
            if (packer.Add(i, update, Packer::EntryBits(scratch, i, update), full))
                REQUIRE(RealBits(scratch, full) <= cMaxBits);
            REQUIRE(packer.CountedBits() >= packer.MessageBits());
        }
    }

    SECTION("A packed client message reads back whole")
    {
        using Packer = MovementPacker<ClientReferencesMoveRequest>;
        Packer packer(42, scratch);
        ClientReferencesMoveRequest full;
        REQUIRE(!packer.Add(14, VrPlayer(), Packer::EntryBits(scratch, 14, VrPlayer()), full));
        REQUIRE(!packer.Add(7, Npc(5.f), Packer::EntryBits(scratch, 7, Npc(5.f)), full));

        Buffer buff(1 << 14);
        Buffer::Writer writer(&buff);
        packer.Current().Serialize(writer);
        Buffer::Reader reader(&buff);
        auto pMessage = ClientMessageFactory{}.Extract(reader);
        REQUIRE(pMessage);
        const auto pRead = TiltedPhoques::CastUnique<ClientReferencesMoveRequest>(std::move(pMessage));
        REQUIRE(*pRead == packer.Current());
    }
}
