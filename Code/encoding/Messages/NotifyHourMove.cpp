#include <Messages/NotifyHourMove.h>

#include <Messages/ByteEnum.h>
#include <TiltedCore/Serialization.hpp>

/**
 * Write the event, the player and the hours.
 *
 * @param aWriter Where the message goes.
 */
void NotifyHourMove::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ByteEnum::Write(aWriter, MoveEvent);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteFloat(aWriter, Hours);
}

/**
 * Read the event, the player and the hours.
 *
 * @param aReader Where the message comes from.
 */
void NotifyHourMove::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    MoveEvent = ByteEnum::Read<Event>(aReader);
    PlayerId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    Hours = Serialization::ReadFloat(aReader);
}
