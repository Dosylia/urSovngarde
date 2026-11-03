#include <Messages/NotifyPartyValue.h>

void NotifyPartyValue::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Key);
    Serialization::WriteFloat(aWriter, Value);
}

void NotifyPartyValue::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    Key = Serialization::ReadString(aReader);
    Value = Serialization::ReadFloat(aReader);
}
