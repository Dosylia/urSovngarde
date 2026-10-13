#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

struct Player;
struct OwnerComponent
{
    OwnerComponent(Player* apPlayer, uint32_t aOwnershipEpoch = 1)
        : pOwner(apPlayer)
        , OwnershipEpoch(aOwnershipEpoch)
    {
    }

    Player* GetOwner() const { return reinterpret_cast<Player*>(pOwner); }

    void SetOwner(Player* apPlayer) { pOwner = apPlayer; }

    bool IsCurrentOwner(const Player* apPlayer, uint32_t aOwnershipEpoch) const noexcept
    {
        return apPlayer && GetOwner() == apPlayer && aOwnershipEpoch != 0 && OwnershipEpoch == aOwnershipEpoch;
    }

    Player* pOwner;
    uint32_t OwnershipEpoch;
    //! Id of the player whose follower this is (0: none); others cannot claim it while that player is connected.
    uint32_t FollowerOfPlayerId{};
    Vector<const Player*> InvalidOwners{};
};
