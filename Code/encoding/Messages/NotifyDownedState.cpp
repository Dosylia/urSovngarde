#include <Messages/NotifyDownedState.h>

void NotifyDownedState::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    aWriter.WriteBits(Change, 8);
    Serialization::WriteVarInt(aWriter, ReviverId);
    Serialization::WriteVarInt(aWriter, SecondsLeft);
    aWriter.WriteBits(static_cast<uint8_t>(Refusal), 8);
    Serialization::WriteBool(aWriter, HasRespawnPoint);
    if (HasRespawnPoint)
    {
        RespawnCellId.Serialize(aWriter);
        RespawnWorldSpaceId.Serialize(aWriter);
        RespawnPosition.Serialize(aWriter);
    }
}

void NotifyDownedState::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t change = 0;
    aReader.ReadBits(change, 8);
    Change = static_cast<State>(change);
    ReviverId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SecondsLeft = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t refusal = 0;
    aReader.ReadBits(refusal, 8);
    Refusal = static_cast<ReviveRefusal>(refusal);
    HasRespawnPoint = Serialization::ReadBool(aReader);
    if (HasRespawnPoint)
    {
        RespawnCellId.Deserialize(aReader);
        RespawnWorldSpaceId.Deserialize(aReader);
        RespawnPosition.Deserialize(aReader);
    }
}
