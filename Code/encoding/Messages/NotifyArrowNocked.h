#pragma once

#include "Message.h"

//! Another player's bow has an arrow on its string, or no longer has one (ArrowNockedRequest, passed on by the server
//! to the players near them). The receiver attaches or detaches the arrow on that player's copy.
struct NotifyArrowNocked final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyArrowNocked;

    NotifyArrowNocked()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyArrowNocked& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Nocked == acRhs.Nocked; }

    //! Server id of the archer's character.
    uint32_t ServerId{};
    bool Nocked{};
};
