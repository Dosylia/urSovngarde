#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

//! Where the sender's game would have saved last: after a loading screen (a load door, fast travel, a loaded save) or a sleep or wait, out of combat.
//! The server keeps it; a party respawns at its leader's (NotifyDownedState).
struct SafePointRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kSafePointRequest;

    SafePointRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SafePointRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position;
    }

    GameId CellId{};
    //! Empty in an interior.
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
};
