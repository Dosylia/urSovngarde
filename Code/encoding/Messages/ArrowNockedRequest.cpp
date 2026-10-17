#include <Messages/ArrowNockedRequest.h>

void ArrowNockedRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, Nocked);
    Serialization::WriteBool(aWriter, InHand);
    if (!InHand)
        return;
    Serialization::WriteVarInt(aWriter, HandSide);
    for (int axis = 0; axis < 3; ++axis)
        Serialization::WriteFloat(aWriter, HandOffset[axis]);
    for (int axis = 0; axis < 4; ++axis)
        Serialization::WriteFloat(aWriter, HandRotation[axis]);
}

void ArrowNockedRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Nocked = Serialization::ReadBool(aReader);
    InHand = Serialization::ReadBool(aReader);
    if (!InHand)
        return;
    HandSide = static_cast<uint8_t>(Serialization::ReadVarInt(aReader) & 1);
    for (int axis = 0; axis < 3; ++axis)
        HandOffset[axis] = Serialization::ReadFloat(aReader);
    for (int axis = 0; axis < 4; ++axis)
        HandRotation[axis] = Serialization::ReadFloat(aReader);
}
