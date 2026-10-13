#pragma once

#include "Message.h"

#include <Structs/Vector3_NetQuantize.h>

//! A remembered item moved on another player's side: put it where they have it.
struct NotifyDroppedItemMove final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDroppedItemMove;

    NotifyDroppedItemMove()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDroppedItemMove& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Id == acRhs.Id && Position == acRhs.Position && Rotation == acRhs.Rotation && AtRest == acRhs.AtRest;
    }

    //! The server's id for the item, as NotifyDroppedItem gave it.
    uint32_t Id{};
    Vector3_NetQuantize Position{};
    glm::vec3 Rotation{};
    //! The item has stopped: the server keeps this place, and the receiver hands it back to physics.
    bool AtRest{};
};
