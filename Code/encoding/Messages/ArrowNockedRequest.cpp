#include <Messages/ArrowNockedRequest.h>

void ArrowNockedRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, Nocked);
}

void ArrowNockedRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Nocked = Serialization::ReadBool(aReader);
}
