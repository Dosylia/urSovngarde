#pragma once

#include "Message.h"

// A player renamed their character in play (character creation, a quest): the same player, under a new name.
struct NotifyPlayerName final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerName;

    NotifyPlayerName()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPlayerName() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerName& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && PlayerId == achRhs.PlayerId && Username == achRhs.Username; }

    uint32_t PlayerId{};
    String Username{};
};
