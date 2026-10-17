#pragma once

#include "Message.h"

//! The sender held a hand on a downed party member long enough to revive them.
//! The server checks it (ReviveRules) and tells the party with NotifyDownedState.
struct ReviveRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kReviveRequest;

    ReviveRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ReviveRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && TargetPlayerId == acRhs.TargetPlayerId; }

    //! Player id of the downed player.
    uint32_t TargetPlayerId{};
};
