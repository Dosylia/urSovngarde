#include <Messages/NotifySleep.h>

void NotifySleep::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint64_t>(SleepEvent), 8);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteFloat(aWriter, HoursSlept);
}

void NotifySleep::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t event = 0;
    aReader.ReadBits(event, 8);
    SleepEvent = static_cast<Event>(event & 0xFF);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HoursSlept = Serialization::ReadFloat(aReader);
}
