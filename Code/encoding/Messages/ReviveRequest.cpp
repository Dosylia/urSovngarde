#include <Messages/ReviveRequest.h>

void ReviveRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetPlayerId);
    aWriter.WriteBits(HoldStage, 8);
}

void ReviveRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    TargetPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    uint64_t stage = 0;
    aReader.ReadBits(stage, 8);
    HoldStage = static_cast<Stage>(stage);
}
