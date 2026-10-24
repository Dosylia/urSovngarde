#pragma once

#include "Message.h"

#include <Structs/Inventory.h>

struct NotifyInventoryChanges final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyInventoryChanges;

    NotifyInventoryChanges()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Item == acRhs.Item && Drop == acRhs.Drop && PickedUp == acRhs.PickedUp;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory::Entry Item{};
    bool Drop = false;
    //! The item came from a reference its owner picked up in the world, a pick-up the receiver replays on the copy as well.
    bool PickedUp = false;
};
