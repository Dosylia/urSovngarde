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
        , OwnerSince(std::chrono::steady_clock::now())
    {
    }

    Player* GetOwner() const { return reinterpret_cast<Player*>(pOwner); }

    void SetOwner(Player* apPlayer)
    {
        pOwner = apPlayer;
        OwnerSince = std::chrono::steady_clock::now();
    }

    bool IsCurrentOwner(const Player* apPlayer, uint32_t aOwnershipEpoch) const noexcept
    {
        return apPlayer && GetOwner() == apPlayer && aOwnershipEpoch != 0 && OwnershipEpoch == aOwnershipEpoch;
    }

    Player* pOwner;
    uint32_t OwnershipEpoch;
    //! When the current owner was given it; its game's reports from before still show it as a copy.
    std::chrono::steady_clock::time_point OwnerSince;
    //! Id of the player whose follower this is (0: none).
    //! While that player is connected, nobody else claims or runs it, and the others show it as a copy.
    uint32_t FollowerOfPlayerId{};
    Vector<const Player*> InvalidOwners{};
};
