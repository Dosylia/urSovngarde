#include <Messages/RequestSleep.h>

void RequestSleep::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint64_t>(SleepStep), 8);
    Serialization::WriteFloat(aWriter, HoursSlept);
    Serialization::WriteBool(aWriter, InCombat);
}

void RequestSleep::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t step = 0;
    aReader.ReadBits(step, 8);
    SleepStep = static_cast<Step>(step & 0xFF);
    HoursSlept = Serialization::ReadFloat(aReader);
    InCombat = Serialization::ReadBool(aReader);
}
