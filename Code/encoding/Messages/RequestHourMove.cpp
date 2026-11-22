#include <Messages/RequestHourMove.h>

#include <TiltedCore/Serialization.hpp>

/**
 * Write the hour asked for.
 *
 * @param aWriter Where the message goes.
 */
void RequestHourMove::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteFloat(aWriter, TargetHour);
}

/**
 * Read the hour asked for.
 *
 * @param aReader Where the message comes from.
 */
void RequestHourMove::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    TargetHour = Serialization::ReadFloat(aReader);
}
