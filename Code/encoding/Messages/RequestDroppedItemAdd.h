#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

//! A player dropped an item, or announces an earlier drop, with where it lies.
struct RequestDroppedItemAdd final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestDroppedItemAdd;

    RequestDroppedItemAdd()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestDroppedItemAdd& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Announcement == acRhs.Announcement && Item == acRhs.Item && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Position == acRhs.Position && Rotation == acRhs.Rotation;
    }

    Inventory::Entry Item{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    //! Euler angles in radians, as the reference holds them.
    //! Plain floats: the packed position type is only about 1 unit precise, too coarse for an angle.
    glm::vec3 Rotation{};

    //! False for a fresh drop, which is always a new item.
    //! True for an earlier drop announced again; the server first matches it to a known item by base and place.
    bool Announcement{};
};
