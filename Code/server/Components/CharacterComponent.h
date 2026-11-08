#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Tints.h>
#include <Structs/Factions.h>

struct CharacterComponent
{
    enum
    {
        kIsDirtyFactions = 1 << 0,
        kIsDead = 1 << 1,
        kIsPlayer = 1 << 2,
        kIsWeaponDrawn = 1 << 3,
        kIsDragon = 1 << 4,
        kIsMount = 1 << 5,
        kIsPlayerSummon = 1 << 6,
        //! Its owner's game revived it since its last death: from that game's own report, cleared by the next death.
        kRevivedSinceDeath = 1 << 7
    };

    [[nodiscard]] bool IsDirtyFactions() const { return Flags & kIsDirtyFactions; }
    [[nodiscard]] bool IsDead() const { return Flags & kIsDead; }
    [[nodiscard]] bool IsPlayer() const { return Flags & kIsPlayer; }
    [[nodiscard]] bool IsWeaponDrawn() const { return Flags & kIsWeaponDrawn; }
    [[nodiscard]] bool IsDragon() const { return Flags & kIsDragon; }
    [[nodiscard]] bool IsMount() const { return Flags & kIsMount; }
    [[nodiscard]] bool IsPlayerSummon() const { return Flags & kIsPlayerSummon; }
    [[nodiscard]] bool IsRevivedSinceDeath() const { return Flags & kRevivedSinceDeath; }

    void SetDirtyFactions(bool aSet)
    {
        if (aSet)
            Flags |= kIsDirtyFactions;
        else
            Flags &= ~kIsDirtyFactions;
    }
    void SetDead(bool aSet)
    {
        if (aSet)
            Flags |= kIsDead;
        else
            Flags &= ~kIsDead;
    }
    void SetPlayer(bool aSet)
    {
        if (aSet)
            Flags |= kIsPlayer;
        else
            Flags &= ~kIsPlayer;
    }
    void SetWeaponDrawn(bool aSet)
    {
        if (aSet)
            Flags |= kIsWeaponDrawn;
        else
            Flags &= ~kIsWeaponDrawn;
    }
    void SetDragon(bool aSet)
    {
        if (aSet)
            Flags |= kIsDragon;
        else
            Flags &= ~kIsDragon;
    }
    void SetMount(bool aSet)
    {
        if (aSet)
            Flags |= kIsMount;
        else
            Flags &= ~kIsMount;
    }
    void SetPlayerSummon(bool aSet)
    {
        if (aSet)
            Flags |= kIsPlayerSummon;
        else
            Flags &= ~kIsPlayerSummon;
    }
    void SetRevivedSinceDeath(bool aSet)
    {
        if (aSet)
            Flags |= kRevivedSinceDeath;
        else
            Flags &= ~kRevivedSinceDeath;
    }

    uint32_t ChangeFlags{0};
    String SaveBuffer{};
    FormIdComponent BaseId{};
    FormIdComponent LeveledNpcPickId{};
    Tints FaceTints{};
    Factions FactionsContent{};
    uint16_t Flags{};
    int32_t PlayerId{};
};
