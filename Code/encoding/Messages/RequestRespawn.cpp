#include <Messages/RequestRespawn.h>

void RequestRespawn::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ActorId);
    Serialization::WriteString(aWriter, AppearanceBuffer);
    Serialization::WriteVarInt(aWriter, ChangeFlags);
    FaceTints.Serialize(aWriter);
    Serialization::WriteString(aWriter, Username);
}

void RequestRespawn::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ActorId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AppearanceBuffer = Serialization::ReadString(aReader);
    ChangeFlags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    FaceTints.Deserialize(aReader);
    Username = Serialization::ReadString(aReader);
}
