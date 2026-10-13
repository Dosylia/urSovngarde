#include <TiltedOnlinePCH.h>

#include <Systems/LeveledNpcSystem.h>
#include <Actor.h>
#include <ExtraData/ExtraLeveledCreature.h>
#include <Forms/TESNPC.h>
#include <Misc/GarbageCollector.h>

bool LeveledNpcSystem::IsLeveledNpcBase(const TESNPC* apBase) noexcept
{
    if (!apBase || apBase->IsTemporary())
        return false;

    const TESForm* pTemplate = apBase->actorData.baseTemplateForm;
    return pTemplate && pTemplate->formType == FormType::LeveledCharacter;
}

TESNPC* LeveledNpcSystem::GetOriginalBase(const Actor* apActor) noexcept
{
    if (!apActor)
        return nullptr;

    const auto* pExtra = static_cast<ExtraLeveledCreature*>(apActor->extraData.GetByType(ExtraDataType::LeveledCreature));
    if (pExtra && pExtra->originalBase)
        return Cast<TESNPC>(pExtra->originalBase);

    auto* pBase = Cast<TESNPC>(apActor->baseForm);
    return IsLeveledNpcBase(pBase) ? pBase : nullptr;
}

bool LeveledNpcSystem::ApplyPick(Actor* apActor, TESNPC* apPick) noexcept
{
    if (!apPick)
        return false;



    // Skyrim resolves a leveled NPC by copying the original base, then applying the pick according to its template flags.
    // Using the pick itself would discard data such as a hold guard's name and outfit.
    auto* pOriginalBase = GetOriginalBase(apActor);
    auto* pResolvedBase = pOriginalBase ? TESActorBaseData::CreateTemplateActorBase(pOriginalBase, apPick) : nullptr;
    if (!pResolvedBase)
        return false;

    auto* pOldBase = Cast<TESNPC>(apActor->baseForm);
    apActor->SetLeveledCreature(pOriginalBase, apPick);
    apActor->SetObjectReference(pResolvedBase);

    // Match RecalcLeveledActor's disposal policy, but never dispose of the original base or the pick itself.
    if (pOldBase && pOldBase->IsTemporary() && pOldBase != pOriginalBase && pOldBase != apPick)
    {
#ifdef SKYRIMVR
        // Not on VR: upstream calls AE 36460, the TESBoundObject overload, whose VR address is not known yet.
        // VR id 35492 is the TESObjectREFR overload (CommonLibVR: takes TESObjectREFR*, bool) and crashes with a base form.
        // Skipping the disposal leaks one temporary base form per reconciliation.
        (void)pOldBase;
#else
        GarbageCollector::Get()->Add(pOldBase);
#endif
    }

    spdlog::info("Applied leveled NPC pick for actor {:X}, original base: {:X}, base: {:X}, pick: {:X}",
        apActor->formID, pOriginalBase->formID, pResolvedBase->formID, apPick->formID);
    return true;
}
