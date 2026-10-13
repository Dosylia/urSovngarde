#pragma once

#include "Message.h"

using TiltedPhoques::Vector;

//! A piece of this player's voice as compressed by Steam's GetVoice, sent unreliable so a loss is a gap, not a delay.
//! The server relays it as NotifyVoiceData.
struct VoiceDataRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kVoiceDataRequest;
    //! Far more than one GetVoice call returns; anything bigger is dropped when read.
    static constexpr uint32_t kMaxBytes = 4096;

    VoiceDataRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const VoiceDataRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Sequence == acRhs.Sequence && Data == acRhs.Data; }

    //! Counts up from 0 for each piece this player sends, so a listener drops a late one.
    uint32_t Sequence{};
    Vector<uint8_t> Data{};
};
