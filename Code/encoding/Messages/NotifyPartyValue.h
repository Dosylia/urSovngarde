#pragma once

#include "Message.h"

// A number the party leader's game set (RequestPartyValue), sent to the other members, and to a player who joins later.
struct NotifyPartyValue final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyValue;

    NotifyPartyValue()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPartyValue() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyValue& achRhs) const noexcept { return Key == achRhs.Key && Value == achRhs.Value; }

    String Key{};
    float Value{};
};
