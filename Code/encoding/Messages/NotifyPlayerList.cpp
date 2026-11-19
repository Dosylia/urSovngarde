#include <Messages/NotifyPlayerList.h>
#include <TiltedCore/Serialization.hpp>

void NotifyPlayerList::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Players.size());

    for (auto& player : Players)
    {
        Serialization::WriteVarInt(aWriter, player.first);
        Serialization::WriteString(aWriter, player.second);
    }

    Serialization::WriteVarInt(aWriter, FlatPlayers.size());
    for (const uint32_t cId : FlatPlayers)
        Serialization::WriteVarInt(aWriter, cId);
}

void NotifyPlayerList::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;

    for (auto i = 0u; i < count; ++i)
    {
        uint32_t id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        Players[id] = Serialization::ReadString(aReader);
    }

    // No more flat players than players: a damaged count cannot make it read on and on.
    const auto cFlatCount = Serialization::ReadVarInt(aReader) & 0xFFFF;
    for (auto i = 0u; i < cFlatCount && i < count; ++i)
        FlatPlayers.push_back(Serialization::ReadVarInt(aReader) & 0xFFFFFFFF);
}
