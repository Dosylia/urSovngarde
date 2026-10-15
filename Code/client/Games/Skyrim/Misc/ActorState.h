#pragma once

#include <Misc/IMovementState.h>

struct ActorState : IMovementState
{
    virtual ~ActorState();

    uint32_t flags1;
    uint32_t flags2;

    bool IsWeaponDrawn() const noexcept { return (flags2 >> 5 & 7) >= 3; }

    bool IsWeaponFullyDrawn() const noexcept { return (flags2 >> 5 & 7) == 3; }

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

    //! ATTACK_STATE_ENUM, bits 28-31 of flags1.
    //! Bow values (CommonLibVR): 8 kBowDraw, 9 kBowAttached (arrow on the string), 10 kBowDrawn (pulled), 11 kBowReleasing, 12 kBowReleased.
    uint32_t AttackState() const noexcept { return (flags1 >> 28) & 0xF; }

    bool SetWeaponDrawn(bool aDraw) noexcept;
};
