#pragma once

#include "Message.h"

//! Voice turned on or off in the urSovngarde menu; with it off (the default) the player neither speaks nor hears.
struct VoiceStateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kVoiceStateRequest;

    VoiceStateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const VoiceStateRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Enabled == acRhs.Enabled; }

    bool Enabled{};
};
