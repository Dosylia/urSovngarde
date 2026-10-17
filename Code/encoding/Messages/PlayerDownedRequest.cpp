#include <Messages/PlayerDownedRequest.h>

void PlayerDownedRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, IsDown);
}

void PlayerDownedRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    IsDown = Serialization::ReadBool(aReader);
}
