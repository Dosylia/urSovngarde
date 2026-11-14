#pragma once

#include <Misc/IMovementState.h>

struct ActorState : IMovementState
{
    //! WEAPON_STATE, bits 5-7 of flags2.
    enum class WeaponState : uint32_t
    {
        Sheathed = 0,
        WantToDraw = 1,
        Drawing = 2,
        Drawn = 3,
        WantToSheathe = 4,
        Sheathing = 5,
    };

    virtual ~ActorState();

    uint32_t flags1;
    uint32_t flags2;

    WeaponState GetWeaponState() const noexcept { return static_cast<WeaponState>(flags2 >> 5 & 7); }
    static const char* WeaponStateName(WeaponState aState) noexcept
    {
        static constexpr const char* kNames[8] = {"sheathed", "wantToDraw", "drawing", "drawn", "wantToSheathe", "sheathing", "6", "7"};
        return kNames[static_cast<uint32_t>(aState) & 7];
    }

    bool IsWeaponDrawn() const noexcept { return GetWeaponState() >= WeaponState::Drawn; }

    bool IsWeaponFullyDrawn() const noexcept { return GetWeaponState() == WeaponState::Drawn; }

    //! Drawn, or on its way back: the weapon is in the hand.
    bool IsWeaponInHand() const noexcept { return GetWeaponState() >= WeaponState::Drawn && GetWeaponState() <= WeaponState::Sheathing; }

    bool IsBleedingOut() const noexcept { return (flags1 & 0x1E00000) == 0x1000000 || (flags1 & 0x1E00000) == 0xE00000; }

    // Life state (bits 21-24): 1 dying, 2 dead.
    bool IsDying() const noexcept { return (flags1 & 0x1E00000) == 0x200000; }
    bool IsDead() const noexcept { return (flags1 & 0x1E00000) == 0x400000; }
    //! FLY_STATE, bits 18-20 of flags1 (CommonLibVR-NG ActorState1, just below the life state).
    //! In the air unless none (0) or perching (5).
    bool IsFlying() const noexcept
    {
        const uint32_t cFly = (flags1 >> 18) & 0x7;
        return cFly != 0 && cFly != 5;
    }
    bool IsDeadOrDying() const noexcept { return IsDying() || IsDead(); }

    //! Bit 10 of flags1 (CommonLibVR ActorState1 swimming).
    bool IsSwimming() const noexcept { return ((flags1 >> 10) & 1) != 0; }

    //! KNOCK_STATE_ENUM, bits 25-27 of flags1 (CommonLibVR ActorState1).
    //! Anything but normal (0) is a body thrown down, lying in its ragdoll, or getting up.
    bool IsKnockedDown() const noexcept { return (flags1 & 0xE000000) != 0; }
    //! A living body thrown down and lying in its ragdoll, which a hand can carry like a corpse.
    //! Not normal (0), and not getting up (6, kGetUp), which an animation plays.
    bool IsInKnockRagdoll() const noexcept
    {
        const uint32_t cKnock = (flags1 >> 25) & 0x7;
        return !IsDeadOrDying() && cKnock != 0 && cKnock != 6;
    }

    //! ATTACK_STATE_ENUM, bits 28-31 of flags1.
    //! Bow values (CommonLibVR): 8 kBowDraw, 9 kBowAttached (arrow on the string), 10 kBowDrawn (pulled), 11 kBowReleasing, 12 kBowReleased.
    uint32_t AttackState() const noexcept { return (flags1 >> 28) & 0xF; }

    bool SetWeaponDrawn(bool aDraw) noexcept;
};
