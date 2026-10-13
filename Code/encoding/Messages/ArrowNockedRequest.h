#pragma once

#include "Message.h"

//! The sender's bow gained or lost its nocked arrow; the server relays it as NotifyArrowNocked.
//! In VR that arrow is a game attachment made at attack state 10 (kBowAttached), which nothing else syncs.
struct ArrowNockedRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kArrowNockedRequest;

    ArrowNockedRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ArrowNockedRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Nocked == acRhs.Nocked; }

    bool Nocked{};
};
