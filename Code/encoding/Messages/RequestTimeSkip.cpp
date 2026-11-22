#include <Messages/RequestTimeSkip.h>

#include <Messages/ByteEnum.h>
#include <TiltedCore/Serialization.hpp>

/**
 * Write the step and the hours.
 *
 * @param aWriter Where the message goes.
 */
void RequestTimeSkip::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ByteEnum::Write(aWriter, SkipStep);
    Serialization::WriteFloat(aWriter, Hours);
}

/**
 * Read the step and the hours.
 *
 * @param aReader Where the message comes from.
 */
void RequestTimeSkip::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    SkipStep = ByteEnum::Read<Step>(aReader);
    Hours = Serialization::ReadFloat(aReader);
}
