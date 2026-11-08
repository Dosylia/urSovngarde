#pragma once

#include <Structs/ActorValues.h>
#include <Structs/Inventory.h>

struct ActorData
{
    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorData& acRhs) const noexcept
    {
        return InitialActorValues == acRhs.InitialActorValues &&
               InitialInventory == acRhs.InitialInventory && IsDead == acRhs.IsDead &&
               IsWeaponDrawn == acRhs.IsWeaponDrawn && RevivedSinceDeath == acRhs.RevivedSinceDeath;
    }

    ActorValues InitialActorValues{};
    Inventory InitialInventory{};
    bool IsDead{};
    bool IsWeaponDrawn{};
    //! Its owner's game revived it since its last death, so a body of it in another game's save is no death to keep.
    bool RevivedSinceDeath{};
};
