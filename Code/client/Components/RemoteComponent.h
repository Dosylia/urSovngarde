#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>

struct RemoteComponent
{
    RemoteComponent(uint32_t aId, uint32_t aRefId, uint32_t aOwnershipEpoch) noexcept
        : Id(aId)
        , CachedRefId(aRefId)
        , OwnershipEpoch(aOwnershipEpoch)
    {
    }

    uint32_t Id;
    uint32_t CachedRefId;
    uint32_t OwnershipEpoch;
    //! The base form of the copy this client made for this character, or 0.
    //! The game reuses a deleted copy's form id; a changed base means CachedRefId now names another actor.
    //! See CharacterService::RunSpawnUpdates.
    uint32_t CachedBaseId{};
};
