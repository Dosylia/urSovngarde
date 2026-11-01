#include <Messages/NotifyRemoveCharacter.h>

void NotifyRemoveCharacter::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteBool(aWriter, Gone);
}

void NotifyRemoveCharacter::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Gone = Serialization::ReadBool(aReader);
}
