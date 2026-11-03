#include <Messages/RequestPartyValue.h>

void RequestPartyValue::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, Key);
    Serialization::WriteFloat(aWriter, Value);
}

void RequestPartyValue::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Key = Serialization::ReadString(aReader);
    Value = Serialization::ReadFloat(aReader);
}
