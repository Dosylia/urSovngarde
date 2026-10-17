#pragma once

#include "Message.h"

//! The sender's player went down (bleedout at zero health) and waits for a revive.
//! The server answers its party with NotifyDownedState, saying how long it has.
//! With IsDown false, the player is up again without a revive or a respawn: a save was loaded while down.
struct PlayerDownedRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayerDownedRequest;

    PlayerDownedRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerDownedRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && IsDown == acRhs.IsDown; }

    bool IsDown{true};
};
