#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

struct RequestInventoryChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Item == acRhs.Item && Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients && PickedUp == acRhs.PickedUp;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    //! The item came from a reference this player picked up in the world; the others replay that pick-up on its copy too.
    bool PickedUp = false;
};
