#include <Messages/ClientReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>
#include <DecodeGuard.h>

void ClientReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ClientReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    // A game reports at most a few hundred actors at once; a larger count, or an actor named twice, is a damaged message.
    constexpr uint64_t cMaxUpdates = 1024;
    if (!DecodeGuard::Within(count, cMaxUpdates))
        return;

    for (uint64_t i = 0; i < count; ++i)
    {
        uint32_t serverId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        if (Updates.find(serverId) != Updates.end())
        {
            DecodeGuard::Fail();
            Updates.clear();
            return;
        }
        Updates[serverId].Deserialize(aReader);
    }
}
