#include <Messages/NotifyArrowNocked.h>

void NotifyArrowNocked::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteBool(aWriter, Nocked);
}

void NotifyArrowNocked::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Nocked = Serialization::ReadBool(aReader);
}
