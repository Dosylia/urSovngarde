#pragma once

#include "Message.h"

//! The sender's own bow has an arrow on its string, or no longer has one. In VR the arrow sitting on the string is the
//! game's own attachment, made when the attack state reaches 10 (kBowAttached), and nothing else carries it: the
//! other player saw a bow drawn with no arrow (Seen, 2026-10-10 and 2026-10-11). The server passes it on as
//! NotifyArrowNocked, with the sender's character.
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
