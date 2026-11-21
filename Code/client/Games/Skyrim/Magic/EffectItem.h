#pragma once

#include <Components/TESCondition.h>

struct EffectSetting;

struct EffectItemData
{
    float fMagnitude;
    int32_t iArea;
    int32_t iDuration;
};

struct EffectItem
{
    bool IsHealingEffect() const noexcept;
    bool IsSummonEffect() const noexcept;
    bool IsSlowEffect() const noexcept;
    bool IsInivisibilityEffect() const noexcept;
    bool IsWerewolfEffect() const noexcept;
    bool IsVampireLordEffect() const noexcept;
    bool IsNightVisionEffect() const noexcept;
    /**
     * Whether this effect raises a dead body to fight for its caster (Raise Zombie, Dead Thrall, Revenant).
     *
     * @return True for the reanimate archetype.
     */
    bool IsReanimateEffect() const noexcept;

    EffectItemData data;
    uint32_t padC;
    EffectSetting* pEffectSetting;
    float fRawCost;
    TESCondition Condition{};
};
