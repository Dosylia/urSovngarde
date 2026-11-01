#pragma once

#include "Message.h"

struct NotifyRemoveCharacter final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyRemoveCharacter;

    NotifyRemoveCharacter()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyRemoveCharacter() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyRemoveCharacter& achRhs) const noexcept { return ServerId == achRhs.ServerId && Gone == achRhs.Gone && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t ServerId;
    //! The server no longer has the character at all; false when it only left this player's view.
    bool Gone{false};
};
