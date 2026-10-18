#include <Messages/NotifyArrowNocked.h>

void NotifyArrowNocked::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
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

void NotifyArrowNocked::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
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
