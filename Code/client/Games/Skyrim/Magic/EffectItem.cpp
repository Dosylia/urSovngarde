#include "EffectItem.h"

#include <Games/Magic/MagicSystem.h>
#include <Forms/EffectSetting.h>

bool EffectItem::IsHealingEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kValueModifier && data.fMagnitude > 0.0f;
}

bool EffectItem::IsSummonEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kSummonCreature;
}

bool EffectItem::IsSlowEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kSlowTime;
}

bool EffectItem::IsInivisibilityEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kInvisibility;
}

bool EffectItem::IsWerewolfEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kWerewolf;
}

bool EffectItem::IsVampireLordEffect() const noexcept
{
    return pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kVampireLord;
}

/**
 * Whether this effect raises a dead body to fight for its caster (Raise Zombie, Dead Thrall, Revenant).
 *
 * @return True for the reanimate archetype.
 */
bool EffectItem::IsReanimateEffect() const noexcept
{
    return pEffectSetting && pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kReanimate;
}

bool EffectItem::IsNightVisionEffect() const noexcept
{
    BGSKeyword* pMagicNightEye = Cast<BGSKeyword>(TESForm::GetById(0xad7c6));
    if (pEffectSetting->keywordForm.count == 0)
        spdlog::debug(__FUNCTION__ ": correcting BGSKeywordForm::Contains() bug for zero-keyword {:x}, {}", pEffectSetting->formID, pEffectSetting->fullName.value.AsAscii());
    return pEffectSetting->keywordForm.count > 0 && pEffectSetting->keywordForm.Contains(pMagicNightEye);
}

