#include "MagicTarget.h"
#include <Events/HealthChangeEvent.h>

#include <cmath>

#include <Actor.h>
#include <World.h>
#include "EffectItem.h"

#include <Structs/Skyrim/AnimationGraphDescriptor_WerewolfBehavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Events/AddTargetEvent.h>
#include <Events/HitEvent.h>

#include <Games/ActorExtension.h>
#include <Games/Overrides.h>
#include <PlayerCharacter.h>

#include <Effects/ActiveEffect.h>
#include <Forms/SpellItem.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/ActorValueInfo.h>
#include <Games/References.h>

TP_THIS_FUNCTION(TAddTarget, bool, MagicTarget, MagicTarget::AddTargetData& arData);
TP_THIS_FUNCTION(TCheckAddEffectTargetData, bool, MagicTarget::AddTargetData, void* arArgs, float afResistance);
TP_THIS_FUNCTION(TFindTargets, bool, MagicCaster, float afEffectivenessMult, int32_t* aruiTargetCount,
                 TESBoundObject* apSource, char abLoadCast, char abAdjust);
TP_THIS_FUNCTION(TAdjustForPerks, void, ActiveEffect, Actor* apCaster, MagicTarget* apTarget);
TP_THIS_FUNCTION(THasPerk, bool, Actor, TESForm* apPerk, void* apUnk1, double* afReturnValue);
TP_THIS_FUNCTION(TGetPerkRank, uint8_t, Actor, TESForm* apPerk);

static TAddTarget* RealAddTarget = nullptr;
static TCheckAddEffectTargetData* RealCheckAddEffectTargetData = nullptr;
static TFindTargets* RealFindTargets = nullptr;
static TAdjustForPerks* RealAdjustForPerks = nullptr;
static THasPerk* RealHasPerk = nullptr;
static TGetPerkRank* RealGetPerkRank = nullptr;

static thread_local bool s_autoSucceedEffectCheck = false;
static thread_local bool s_applyStaminaPerkBonus = false;
// While another game's heal or buff is applied here: how much its caster's perks multiplied it there.
static thread_local float s_casterPerkFactor = 1.f;
// While this game's player casts on another player's copy: how much the caster's perks multiplied the effect.
static thread_local bool s_measurePerks = false;
static thread_local float s_perkFactor = 1.f;
// A damaging spell on another player's copy is made only to measure its perks: the effect is left with no magnitude.
static thread_local bool s_measureOnly = false;

// The effect lowers health: a harmful value modifier whose first value is health.
// Fire is a single value modifier; frost and shock are dual ones that also take stamina or magicka.
// The damage keywords name the element (0x1CEAD is MagicDamageFire), so none of them marks health damage as a whole.
// Judged per effect, not per spell: a spell's other effects (a burn, a script) are not damage to send.
static bool DamagesHealth(EffectItem* apEffect) noexcept
{
    constexpr uint32_t cDetrimental = 0x4;

    if (!apEffect || !apEffect->pEffectSetting)
        return false;

    const EffectSetting* pSetting = apEffect->pEffectSetting;
    const bool cModifiesValue = pSetting->eArchetype == EffectArchetypes::ArchetypeID::kValueModifier ||
                                pSetting->eArchetype == EffectArchetypes::ArchetypeID::kDualValueModifier;
    return cModifiesValue && pSetting->primaryActorValue == ActorValueInfo::kHealth && (pSetting->flags & cDetrimental) != 0;
}

// The effect takes health from its target, whether it gives it to its caster or not.
static bool LowersHealth(EffectItem* apEffect) noexcept
{
    constexpr uint32_t cDetrimental = 0x4;

    if (DamagesHealth(apEffect))
        return true;

    if (!apEffect || !apEffect->pEffectSetting)
        return false;

    const EffectSetting* pSetting = apEffect->pEffectSetting;
    return pSetting->eArchetype == EffectArchetypes::ArchetypeID::kAbsorb && pSetting->primaryActorValue == ActorValueInfo::kHealth && (pSetting->flags & cDetrimental) != 0;
}

bool MagicTarget::IsSaneCasterFactor(const float aFactor) noexcept
{
    constexpr float cMostPerkFactor = 10.f;
    return std::isfinite(aFactor) && aFactor >= 0.f && aFactor <= cMostPerkFactor;
}

// A concentration spell or staff is decided and done by the beam replayed in the other game, so its caster has nothing to report.
static bool IsConcentration(MagicItem* apSpell) noexcept
{
    if (const SpellItem* pSpell = Cast<SpellItem>(apSpell))
        return pSpell->eCastingType == MagicSystem::CastingType::CONCENTRATION;

    const EnchantmentItem* pEnchantment = Cast<EnchantmentItem>(apSpell);
    return pEnchantment && pEnchantment->eCastingType == MagicSystem::CastingType::CONCENTRATION;
}

// How a spell or an enchantment reaches its target.
static MagicSystem::Delivery DeliveryOf(MagicItem* apSpell) noexcept
{
    if (const SpellItem* pSpell = Cast<SpellItem>(apSpell))
        return pSpell->eDelivery;

    const EnchantmentItem* pEnchantment = Cast<EnchantmentItem>(apSpell);
    return pEnchantment ? pEnchantment->eDelivery : MagicSystem::DELIVERY_COUNT;
}

// A power is cast by the copy in the other game too, so unless it is aimed it lands there by itself, and its caster has nothing to report.
// An aimed power goes across by its projectile, as any aimed spell does.
static bool LandsByItsCast(MagicItem* apSpell) noexcept
{
    using ST = MagicSystem::SpellType;

    if (IsConcentration(apSpell))
        return true;

    const SpellItem* pSpell = Cast<SpellItem>(apSpell);
    const bool cPower = pSpell && (pSpell->eSpellType == ST::POWER || pSpell->eSpellType == ST::LESSER_POWER || pSpell->eSpellType == ST::VOICE_POWER);
    return cPower && pSpell->eDelivery != MagicSystem::AIMED && pSpell->eDelivery != MagicSystem::TARGET_LOCATION;
}

bool MagicTarget::IsCast(MagicItem* apSpell) noexcept
{
    const MagicSystem::Delivery cDelivery = DeliveryOf(apSpell);
    return Cast<SpellItem>(apSpell) || cDelivery == MagicSystem::AIMED || cDelivery == MagicSystem::TARGET_LOCATION;
}

bool MagicTarget::IsReplayedHit(EffectItem* apEffect, MagicItem* apSpell) noexcept
{
    if (!DamagesHealth(apEffect) || apEffect->data.iDuration != 0 || IsConcentration(apSpell))
        return false;

    // A form made at run time means nothing in another game, so no projectile of it is replayed there.
    if (apSpell->IsTemporary() || apEffect->pEffectSetting->IsTemporary())
        return false;

    const MagicSystem::Delivery cDelivery = DeliveryOf(apSpell);
    return cDelivery == MagicSystem::AIMED || cDelivery == MagicSystem::TARGET_LOCATION;
}

void MagicTarget::DispelAllSpells(bool aNow) noexcept
{
    TP_THIS_FUNCTION(TDispelAllSpells, void, MagicTarget, bool aNow);
    POINTER_SKYRIMSE(TDispelAllSpells, dispelAllSpells, 34512, 34512);
    TiltedPhoques::ThisCall(dispelAllSpells, this, aNow);
}

bool MagicTarget::AddTarget(AddTargetData& arData, float aCasterPerkFactor, bool aApplyStaminaPerkBonus) noexcept
{
    // The factor comes from another game: one that no perk could give (not a number, below 0, over 10 times) is not applied.
    const bool cSane = IsSaneCasterFactor(aCasterPerkFactor);
    if (!cSane)
    {
        static bool s_said = false;
        if (!s_said)
        {
            s_said = true;
            spdlog::warn("MagicTarget: a caster perk factor of {} from another game is not applied", aCasterPerkFactor);
        }
    }

    s_autoSucceedEffectCheck = true;
    s_casterPerkFactor = cSane ? aCasterPerkFactor : 1.f;
    s_applyStaminaPerkBonus = aApplyStaminaPerkBonus;

    bool result = TiltedPhoques::ThisCall(RealAddTarget, this, arData);

    s_autoSucceedEffectCheck = false;
    s_casterPerkFactor = 1.f;
    s_applyStaminaPerkBonus = false;

    return result;
}

bool MagicTarget::AddTargetData::ShouldSync()
{
    // Constant effect slow time is an ability each client's scripts add and remove on their own player, so a synced
    // copy would stay on the other players for good. Fire and forget slow time (shouts) still syncs.
    if (pEffectItem->IsSlowEffect())
    {
        SpellItem* pSpellItem = Cast<SpellItem>(pSpell);
        if (pSpellItem && pSpellItem->eCastingType == MagicSystem::CastingType::CONSTANT_EFFECT)
            return false;
    }

    return !pEffectItem->IsSummonEffect() && !pSpell->IsInvisibilitySpell() && !pSpell->IsWardSpell();
}

// Some effects are player specific, and do not need to be synced.
bool MagicTarget::AddTargetData::IsForbiddenEffect(Actor* apTarget)
{
    if (apTarget != PlayerCharacter::Get())
        return false;

    return pEffectItem->IsNightVisionEffect();
}

Actor* MagicTarget::GetTargetAsActor()
{
    TP_THIS_FUNCTION(TGetTargetAsActor, Actor*, MagicTarget);
    POINTER_SKYRIMSE(TGetTargetAsActor, getTargetAsActor, 34529, 34529);
    return TiltedPhoques::ThisCall(getTargetAsActor, this);
}

bool TP_MAKE_THISCALL(HookAddTarget, MagicTarget, MagicTarget::AddTargetData& arData)
{
    Actor* pTargetActor = apThis->GetTargetAsActor();
    if (!pTargetActor || arData.IsForbiddenEffect(pTargetActor))
        return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);

    ActorExtension* pTargetActorEx = pTargetActor->GetExtension();

    if (arData.pEffectItem->IsWerewolfEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_WerewolfBehavior::m_key;

    if (arData.pEffectItem->IsVampireLordEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_VampireLordBehavior::m_key;

    if (ScopedSpellCastOverride::IsOverriden())
        return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);

    AddTargetEvent addTargetEvent{};
    addTargetEvent.TargetID = pTargetActor->formID;
    addTargetEvent.CasterID = arData.pCaster ? arData.pCaster->formID : 0;
    addTargetEvent.SpellID = arData.pSpell->formID;
    addTargetEvent.EffectID = arData.pEffectItem->pEffectSetting->formID;
    addTargetEvent.Magnitude = arData.fMagnitude;
    addTargetEvent.IsDualCasting = arData.bDualCast;

    if (pTargetActorEx->IsRemotePlayer())
    {
        if (!arData.pCaster)
            return false;

        ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
        if (!pCasterExtension || !pCasterExtension->IsLocalPlayer())
            return false;

        if (!arData.pSpell->IsHealingSpell() && !arData.pSpell->IsBuffSpell())
        {
            // A damaging spell on the other player is reported to that player's game, like a sword hit (Actor::HookDamageActor).
            // It is not applied locally: the other side owns that body.
            // Only an instant effect that damages health, not the spell's other effects: damage-over-time and concentration effects tick on the other side.
            if (World::Get().GetServerSettings().PvpEnabled && DamagesHealth(arData.pEffectItem) && arData.pEffectItem->data.iDuration == 0 && !LandsByItsCast(arData.pSpell) &&
                arData.fMagnitude > 0.f)
            {
                // The caster's perks (Augmented Flames and the like) adjust an effect only as it is made (AdjustForPerks), as on an NPC.
                // So the effect is made on the copy to take their factor, and left with no magnitude there (live-pvp-spell-perks).
                const float cBase = arData.fMagnitude;
                s_measurePerks = true;
                s_measureOnly = true;
                s_perkFactor = 1.f;
                const bool cMade = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
                s_measurePerks = false;
                s_measureOnly = false;

                // An effect the game would not put on the copy (resisted, absorbed) does no damage, as on an NPC.
                if (!cMade)
                {
                    spdlog::info("PvP: spell {:X} on remote player {:X} was not taken here (resisted or absorbed); no damage sent", arData.pSpell->formID, pTargetActor->formID);
                    return false;
                }

                // As on an NPC, the caster's difficulty counts too.
                const float cFactor = GameplayFormulas::CalculateRealDamage(pTargetActor, s_perkFactor, false);
                spdlog::info("PvP: spell {:X} hit remote player {:X} for {:.0f} (base {:.0f}, the caster's perks {:.2f})", arData.pSpell->formID, pTargetActor->formID, cBase * cFactor,
                             cBase, s_perkFactor);

                // An aimed spell is reported as itself: the other game's replayed projectile decides whether it hit, and this says how strong it was.
                // Anything else, a touch or a weapon's strike, is a plain health change, weighed by the defender's rule.
                if (MagicTarget::IsReplayedHit(arData.pEffectItem, arData.pSpell))
                {
                    addTargetEvent.CasterPerkFactor = cFactor;
                    World::Get().GetRunner().Trigger(addTargetEvent);
                }
                else
                    World::Get().GetRunner().Trigger(HealthChangeEvent(pTargetActor->formID, -cBase * cFactor));
            }

            return false;
        }

        if (arData.pSpell->IsHealingSpell())
            addTargetEvent.ApplyStaminaPerkBonus = arData.pCaster->HasPerk(0x581f9);

        s_measurePerks = true;
        s_perkFactor = 1.f;
        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        s_measurePerks = false;

        // The other player's game applies this with its copy of our player, which has none of our player's perks.
        addTargetEvent.CasterPerkFactor = s_perkFactor;
        if (s_perkFactor != 1.f)
            spdlog::info("Magic: spell {:X} on remote player {:X}: the caster's perks make it {:.2f} times as strong", arData.pSpell->formID, pTargetActor->formID, s_perkFactor);

        if (result)
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }

    if (pTargetActorEx->IsLocalPlayer())
    {
        if (arData.pCaster)
        {
            ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
            if (pCasterExtension->IsRemotePlayer())
            {
                if (!World::Get().GetServerSettings().PvpEnabled)
                    return false;

                // Heal and buff spells are already synced by the caster.
                if (arData.pSpell->IsHealingSpell() || arData.pSpell->IsBuffSpell())
                    return false;

                // The projectile or beam replayed here reached this player, so the spell landed.
                // Its damage counts here (HookDamageActor), so its caster's game is not told of it, or it would report the hit again.
                // Their report adds their perks to an aimed spell (MagicService).
                if (MagicTarget::IsCast(arData.pSpell) && LowersHealth(arData.pEffectItem))
                {
                    const float cMagnitude = arData.fMagnitude;
                    const bool cLanded = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);

                    if (cLanded && cMagnitude > 0.f && MagicTarget::IsReplayedHit(arData.pEffectItem, arData.pSpell))
                        World::Get().GetRunner().Queue(
                            [cCopy = arData.pCaster->formID, cSpell = arData.pSpell->formID, cEffect = arData.pEffectItem->pEffectSetting->formID,
                             cAmount = GameplayFormulas::CalculateRealDamage(arData.pCaster, cMagnitude, false)]()
                            { World::Get().GetMagicService().OnPvpSpellLanded(cCopy, cSpell, cEffect, cAmount); });
                    return cLanded;
                }
            }
        }

        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        if (result && arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }

    if (arData.pCaster)
    {
        ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
        if (pCasterExtension->IsLocalPlayer())
        {
            bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
            if (result && arData.ShouldSync())
                World::Get().GetRunner().Trigger(addTargetEvent);
            return result;
        }
        else if (pCasterExtension->IsRemotePlayer())
        {
            // Send out a HitEvent because TakeDamage is never triggered.
            World::Get().GetRunner().Trigger(HitEvent(arData.pCaster->formID, pTargetActor->formID));
            return false;
        }
    }

    if (pTargetActorEx->IsLocal())
    {
        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        if (result && arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }
    else
    {
        return false;
    }
}

bool TP_MAKE_THISCALL(HookCheckAddEffectTargetData, MagicTarget::AddTargetData, void* arArgs, float afResistance)
{
    if (s_autoSucceedEffectCheck)
        return true;

    return TiltedPhoques::ThisCall(RealCheckAddEffectTargetData, apThis, arArgs, afResistance);
}

bool TP_MAKE_THISCALL(HookFindTargets, MagicCaster, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust)
{
    return TiltedPhoques::ThisCall(RealFindTargets, apThis, afEffectivenessMult, aruiTargetCount, apSource, abLoadCast, abAdjust);
}

void TP_MAKE_THISCALL(HookAdjustForPerks, ActiveEffect, Actor* apCaster, MagicTarget* apTarget)
{
    const float cBefore = apThis->fMagnitude;
    TiltedPhoques::ThisCall(RealAdjustForPerks, apThis, apCaster, apTarget);

    if (s_measurePerks && cBefore != 0.f)
        s_perkFactor = apThis->fMagnitude / cBefore;

    // The other player's game owns that body: the damage goes to it as a health change, and nothing lands on the copy here.
    if (s_measureOnly)
    {
        apThis->fMagnitude = 0.f;
        return;
    }

    // The magnitude another game sends is not what the effect uses, so its caster's perks are applied here.
    apThis->fMagnitude *= s_casterPerkFactor;
}

bool TP_MAKE_THISCALL(HookHasPerk, Actor, TESForm* apPerk, void* apUnk1, double* afReturnValue)
{
    ActorExtension* pExtension = apThis ? apThis->GetExtension() : nullptr;

    if (pExtension && pExtension->IsRemotePlayer())
    {
        if (apPerk && apPerk->formID == 0x581f9)
        {
            if (s_applyStaminaPerkBonus)
            {
                if (afReturnValue)
                    *afReturnValue = 1.f;
                return true;
            }
        }
    }

    return TiltedPhoques::ThisCall(RealHasPerk, apThis, apPerk, apUnk1, afReturnValue);
}

uint8_t TP_MAKE_THISCALL(HookGetPerkRank, Actor, TESForm* apPerk)
{
    return TiltedPhoques::ThisCall(RealGetPerkRank, apThis, apPerk);
}

static TiltedPhoques::Initializer s_magicTargetHooks([]() {
    POINTER_SKYRIMSE(TAddTarget, addTarget, 34526, 34526);
    POINTER_SKYRIMSE(TCheckAddEffectTargetData, checkAddEffectTargetData, 34525, 34525);
    POINTER_SKYRIMSE(TFindTargets, findTargets, 34410, 33632);
    POINTER_SKYRIMSE(TAdjustForPerks, adjustForPerks, 34053, 34053);
    POINTER_SKYRIMSE(THasPerk, hasPerk, 21622, 21622);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698, 36690);

    RealAddTarget = addTarget.Get();
    RealCheckAddEffectTargetData = checkAddEffectTargetData.Get();
    RealFindTargets = findTargets.Get();
    RealAdjustForPerks = adjustForPerks.Get();
    RealHasPerk = hasPerk.Get();
    RealGetPerkRank = getPerkRank.Get();

    TP_HOOK(&RealAddTarget, HookAddTarget);
    TP_HOOK(&RealCheckAddEffectTargetData, HookCheckAddEffectTargetData);
    TP_HOOK(&RealFindTargets, HookFindTargets);
    TP_HOOK(&RealAdjustForPerks, HookAdjustForPerks);
    TP_HOOK(&RealHasPerk, HookHasPerk);
    //TP_HOOK(&RealGetPerkRank, HookGetPerkRank);
});
