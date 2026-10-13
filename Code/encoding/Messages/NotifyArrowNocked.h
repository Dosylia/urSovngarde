#pragma once

#include "Message.h"

//! Another player's bow gained or lost its nocked arrow (a relayed ArrowNockedRequest); the receiver updates the copy.
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
