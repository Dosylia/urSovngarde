#pragma once

#include "Message.h"
#include <Structs/Tints.h>

struct RequestRespawn final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRespawn;

    RequestRespawn()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestRespawn& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && AppearanceBuffer == acRhs.AppearanceBuffer && ChangeFlags == acRhs.ChangeFlags && FaceTints == acRhs.FaceTints && Username == acRhs.Username; }

    uint32_t ActorId{};
    String AppearanceBuffer{};
    uint32_t ChangeFlags{0};
    // A player's own skin and warpaint colours, sent with its look; empty for an NPC.
    Tints FaceTints{};
    // A player's new name, when its character is renamed in play; empty otherwise.
    String Username{};
};
