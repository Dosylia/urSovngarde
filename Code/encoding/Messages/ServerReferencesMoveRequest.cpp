#include <Messages/ServerReferencesMoveRequest.h>
#include <TiltedCore/Serialization.hpp>
#include <DecodeGuard.h>

void ServerReferencesMoveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Tick);
    Serialization::WriteVarInt(aWriter, Updates.size());

    for (const auto& kvp : Updates)
    {
        Serialization::WriteVarInt(aWriter, kvp.first);
        kvp.second.Serialize(aWriter);
    }
}

void ServerReferencesMoveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Tick = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    // A relay carries at most a few hundred actors at once; a larger count, or an actor named twice, is a damaged message.
    constexpr uint64_t cMaxUpdates = 1024;
    if (!DecodeGuard::Within(count, cMaxUpdates))
        return;

    for (uint64_t i = 0; i < count; ++i)
    {
        const uint32_t cServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        if (Updates.find(cServerId) != Updates.end())
        {
            DecodeGuard::Fail();
            Updates.clear();
            return;
        }
        Updates[cServerId].Deserialize(aReader);
    }
}
