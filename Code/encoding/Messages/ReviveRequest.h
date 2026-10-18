#pragma once

#include "Message.h"

//! The sender held a hand on a downed party member long enough to revive them, or began or stopped holding it there.
//! The server checks it (ReviveRules) and tells the party with NotifyDownedState; a hold only the downed player.
struct ReviveRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kReviveRequest;

    ReviveRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    enum Stage : uint8_t
    {
        kRevive,    //!< Held long enough: revive them.
        kHoldStart, //!< A hand is on them now.
        kHoldStop,  //!< The hand came off before the revive.
    };

    bool operator==(const ReviveRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && TargetPlayerId == acRhs.TargetPlayerId && HoldStage == acRhs.HoldStage;
    }

    //! Player id of the downed player.
    uint32_t TargetPlayerId{};
    Stage HoldStage{kRevive};
};
