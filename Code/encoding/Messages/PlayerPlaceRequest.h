#pragma once

#include "Message.h"

//! The name of the player's room indoors, or town, dungeon or hold outdoors; sent when it changes.
//! Shown on the public server page and passed on in NotifyPlayerWhereabouts.
struct PlayerPlaceRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayerPlaceRequest;

    PlayerPlaceRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerPlaceRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Place == acRhs.Place; }

    String Place{};
};
