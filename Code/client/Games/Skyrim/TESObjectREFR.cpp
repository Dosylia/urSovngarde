#include <TiltedOnlinePCH.h>

#include <atomic>

#include <Games/References.h>
#include <Games/Overrides.h>

#include <World.h>
#ifdef SKYRIMVR
#include <Services/CalendarService.h>
#endif
#include <Services/PapyrusService.h>
#include <Events/ActivateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/DroppedItemEvents.h>
#include <PlayerCharacter.h>
#include <Events/ScriptAnimationEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ReferenceEnableEvent.h>

#include <ExtraData/ExtraDataList.h>
#include <ExtraData/ExtraCharge.h>
#include <ExtraData/ExtraCount.h>
#include <ExtraData/ExtraEnchantment.h>
#include <ExtraData/ExtraHealth.h>
#include <ExtraData/ExtraPoison.h>
#include <ExtraData/ExtraSoul.h>
#include <ExtraData/ExtraTextDisplayData.h>
#include <ExtraData/ExtraWorn.h>
#include <ExtraData/ExtraWornLeft.h>
#include <Forms/EnchantmentItem.h>
#include <Forms/AlchemyItem.h>
#include <EquipManager.h>
#include <DefaultObjectManager.h>
#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbVariableValueSet.h>
#include <Havok/hkbStateMachine.h>
#include <Forms/TESObjectCELL.h>
#include <AI/AIProcess.h>
#include <Misc/InventoryEntry.h>
#include <Misc/MiddleProcess.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESActorBase.h>

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/AnimationVariables.h>

extern const AnimationGraphDescriptor* BehaviorVarPatch(BSAnimationGraphManager* pManager, Actor* pActor);

namespace
{
void QueueReferenceInventoryChange(TESObjectREFR* apReference, InventoryChangeEvent aEvent, TESObjectREFR* apTransferReference)
{
    if (const auto* pActor = Cast<Actor>(apReference))
    {
        auto ownershipToken = Utils::GetLocalOwnershipToken(pActor->formID);
        if (!ownershipToken && apTransferReference == PlayerCharacter::Get())
            ownershipToken = Utils::GetRemoteOwnershipToken(pActor->formID);

        if (!ownershipToken)
            return;

        aEvent.ServerId = ownershipToken->ServerId;
        aEvent.OwnershipEpoch = ownershipToken->OwnershipEpoch;
    }

    World::Get().GetRunner().Trigger(std::move(aEvent));
}

// The synced variable indices are only valid for the behavior project they came from.
// An actor can change project (a leveled actor rolled again as another creature), so they are taken again when it does.
void UpdateGraphDescriptorHash(Actor* apActor, BSAnimationGraphManager* apManager, int aForceIndex) noexcept
{
    auto* pExtension = apActor->GetExtension();
    const uint64_t cProjectKey = apManager->GetProjectKey(aForceIndex);
    if (pExtension->GraphDescriptorHash != 0 && pExtension->GraphDescriptorProjectKey == cProjectKey)
        return;

    const auto cPreviousHash = pExtension->GraphDescriptorHash;
    pExtension->GraphDescriptorHash = apManager->GetDescriptorKey(aForceIndex);
    pExtension->GraphDescriptorProjectKey = cProjectKey;

    if (cPreviousHash != 0)
        spdlog::info("Animation graph project of actor {:X} changed, graph descriptor {} -> {}", apActor->formID, cPreviousHash, pExtension->GraphDescriptorHash);
}
}

TP_THIS_FUNCTION(TActivate, bool, TESObjectREFR, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing);
TP_THIS_FUNCTION(TAddInventoryItem, void, TESObjectREFR, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner);
TP_THIS_FUNCTION(
    TRemoveInventoryItem, BSPointerHandle<TESObjectREFR>*, TESObjectREFR, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate);
TP_THIS_FUNCTION(TPlayAnimationAndWait, bool, void, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName);
TP_THIS_FUNCTION(TPlayAnimation, bool, void, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName);
TP_THIS_FUNCTION(TRotate, void, TESObjectREFR, float aAngle);
TP_THIS_FUNCTION(TLockChange, void, TESObjectREFR);
TP_THIS_FUNCTION(TSetLeveledCreature, void, TESObjectREFR, TESActorBase* apOriginalBase, TESActorBase* apTemplateBase);

static TActivate* RealActivate = nullptr;
static TAddInventoryItem* RealAddInventoryItem = nullptr;
static TRemoveInventoryItem* RealRemoveInventoryItem = nullptr;
static TPlayAnimationAndWait* RealPlayAnimationAndWait = nullptr;
static TPlayAnimation* RealPlayAnimation = nullptr;
static TRotate* RealRotateX = nullptr;
static TRotate* RealRotateY = nullptr;
static TRotate* RealRotateZ = nullptr;
static TLockChange* RealLockChange = nullptr;
static TSetLeveledCreature* RealSetLeveledCreature = nullptr;

#ifdef SAVE_STUFF

#include <Games/Skyrim/SaveLoad.h>

void TESObjectREFR::Save_Reversed(const uint32_t aChangeFlags, Buffer::Writer& aWriter)
{
    TESForm::Save_Reversed(aChangeFlags, aWriter);

    if (aChangeFlags & CHANGE_REFR_BASEOBJECT)
    {
        // save baseForm->formID;
        // we don't because each player has it's own form id system
    }

    if (aChangeFlags & CHANGE_REFR_SCALE)
    {
        // So skyrim does some weird conversion shit here, we are going to do the same for now
        float fScale = scale;
        fScale /= 100.f;
        aWriter.WriteBytes((uint8_t*)&fScale, 4);
    }

    // So skyrim
    uint32_t extraFlags = 0xA6021C40;
    if (formType == Character)
        extraFlags = 0xA6061840;

    if (aChangeFlags & extraFlags)
    {
        // We have flags to save
    }

    if (aChangeFlags & (CHANGE_REFR_INVENTORY | CHANGE_REFR_LEVELED_INVENTORY))
    {
    }

    if (aChangeFlags & CHANGE_REFR_ANIMATION)
    {
        // do something with animations
        // get extradata 0x41
    }
}

#endif

TESObjectREFR* TESObjectREFR::GetByHandle(uint32_t aHandle) noexcept
{
    TESObjectREFR* pResult = nullptr;

    using TGetRefrByHandle = void(uint32_t & aHandle, TESObjectREFR * &apResult);

    POINTER_SKYRIMSE(TGetRefrByHandle, s_getRefrByHandle, 17201, 12204);

    s_getRefrByHandle.Get()(aHandle, pResult);

    if (pResult)
        pResult->handleRefObject.DecRefHandle();

    return pResult;
}

BSPointerHandle<TESObjectREFR> TESObjectREFR::GetHandle() const noexcept
{
    TP_THIS_FUNCTION(TGetHandle, BSPointerHandle<TESObjectREFR>, const TESObjectREFR, BSPointerHandle<TESObjectREFR>* apResult);
    POINTER_SKYRIMSE(TGetHandle, s_getHandle, 19846, 19846);

    BSPointerHandle<TESObjectREFR> result{};
    TiltedPhoques::ThisCall(s_getHandle, this, &result);

    return result;
}

uint32_t* TESObjectREFR::GetNullHandle() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_nullHandle, 400312, 400312);

    return s_nullHandle.Get();
}

void TESObjectREFR::SetRotation(float aX, float aY, float aZ) noexcept
{
    TiltedPhoques::ThisCall(RealRotateX, this, aX);
    TiltedPhoques::ThisCall(RealRotateY, this, aY);
    TiltedPhoques::ThisCall(RealRotateZ, this, aZ);
}

void TESObjectREFR::SetLeveledCreature(TESActorBase* apOriginalBase, TESActorBase* apTemplateA) noexcept
{
    TP_THIS_FUNCTION(TSetLeveledCreature, void, TESObjectREFR, TESActorBase*, TESActorBase*);
    // The VR address is in VRAddressOverrides under AE id 20231 (VR 0x1402b8f10, SE 0x1402a77a0, SE id 19826).
    // The null check stays: an unresolved id would otherwise be called as address zero.
    POINTER_SKYRIMSE(TSetLeveledCreature, s_SetLeveledCreature, 20231, 20231);
    if (!s_SetLeveledCreature.Get())
    {
        static bool s_said = false;
        if (!s_said)
        {
            s_said = true;
            spdlog::warn("SetLeveledCreature did not resolve; levelled creature reconciliation cannot run");
        }
        return;
    }
    TiltedPhoques::ThisCall(s_SetLeveledCreature, this, apOriginalBase, apTemplateA);
}

using TiltedPhoques::Serialization;

void TESObjectREFR::SaveAnimationVariables(AnimationVariables& aVariables) const noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (!animationGraphHolder.GetBSAnimationGraph(&pManager))
        return;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};

        {
            auto* pActor = Cast<Actor>(this);
            if (!pActor)
            {
                pManager->Release();
                return;
            }

            // The player always reads graph 0, so the check uses the index actually read, not the manager's own.
            // The manager's index is garbage on VR; checking it would drop every player read and make the body slide.
            const bool cIsPlayer = pActor->formID == 0x14;
            const uint32_t cGraphIndex = pManager->ResolveGraphIndex(cIsPlayer ? 0 : -1);

            if (cGraphIndex >= pManager->animationGraphs.size)
            {
                pManager->Release();
                return;
            }

            const BShkbAnimationGraph* pGraph = pManager->animationGraphs.Get(cGraphIndex);

            if (!pGraph)
            {
                pManager->Release();
                return;
            }

            if (!pGraph->behaviorGraph || !pGraph->behaviorGraph->stateMachine || !pGraph->behaviorGraph->stateMachine->name)
            {
                pManager->Release();
                return;
            }

            // Force third person graph to be used on player
            UpdateGraphDescriptorHash(pActor, pManager, pActor->formID == 0x14 ? 0 : -1);
            auto* pExtendedActor = pActor->GetExtension();

            auto pDescriptor = AnimationGraphDescriptorManager::Get().GetDescriptor(pExtendedActor->GraphDescriptorHash);

            // Modded behavior check if descriptor wasn't found
            if (!pDescriptor)
                pDescriptor = BehaviorVarPatch(pManager, pActor);

            if (!pDescriptor)
            {
                pManager->Release();
                return;
            }

            const auto* pVariableSet = pGraph->behaviorGraph->animationVariables;

            if (!pVariableSet)
            {
                pManager->Release();
                return;
            }

            aVariables.Booleans.assign(pDescriptor->BooleanLookUpTable.size(), false);
            aVariables.Floats.assign(pDescriptor->FloatLookupTable.size(), 0.f);
            aVariables.Integers.assign(pDescriptor->IntegerLookupTable.size(), 0);

            for (size_t i = 0; i < pDescriptor->BooleanLookUpTable.size(); ++i)
            {
                const auto idx = pDescriptor->BooleanLookUpTable[i];

                if (pVariableSet->size > idx && pVariableSet->data[idx] != 0)
                    aVariables.Booleans[i] = true;
            }

            for (size_t i = 0; i < pDescriptor->FloatLookupTable.size(); ++i)
            {
                const auto idx = pDescriptor->FloatLookupTable[i];

                if (pVariableSet->size > idx)
                    aVariables.Floats[i] = *reinterpret_cast<float*>(&pVariableSet->data[idx]);
            }

            for (size_t i = 0; i < pDescriptor->IntegerLookupTable.size(); ++i)
            {
                const auto idx = pDescriptor->IntegerLookupTable[i];

                if (pVariableSet->size > idx)
                    aVariables.Integers[i] = *reinterpret_cast<uint32_t*>(&pVariableSet->data[idx]);
            }
        }

        pManager->Release();
    }
}

void TESObjectREFR::LoadAnimationVariables(const AnimationVariables& aVariables) const noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (animationGraphHolder.GetBSAnimationGraph(&pManager))
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};

        // The receiving half uses the resolved index too: the raw one is garbage on VR and would leave the copy's legs still.
        if (const uint32_t index = pManager->ResolveGraphIndex(); index < pManager->animationGraphs.size)
        {
            const auto* pGraph = pManager->animationGraphs.Get(index);

            if (!pGraph)
                return;

            if (!pGraph->behaviorGraph || !pGraph->behaviorGraph->stateMachine || !pGraph->behaviorGraph->stateMachine->name)
                return;

            auto* pActor = Cast<Actor>(this);
            if (!pActor)
                return;

            UpdateGraphDescriptorHash(pActor, pManager, -1);
            auto* pExtendedActor = pActor->GetExtension();

            auto pDescriptor = AnimationGraphDescriptorManager::Get().GetDescriptor(pExtendedActor->GraphDescriptorHash);

            // Modded behavior check if descriptor wasn't found
            if (!pDescriptor)
                pDescriptor = BehaviorVarPatch(pManager, pActor);

            if (!pDescriptor)
                return;

            const auto* pBehaviorGraph = pGraph->behaviorGraph;
            const auto* pVariableSet = pBehaviorGraph->animationVariables;

            if (!pVariableSet)
                return;

            // Never write into a variable whose word is an index: a wrong descriptor would make a synced value an index Havok follows later.
            for (size_t i = 0; i < pDescriptor->BooleanLookUpTable.size(); ++i)
            {
                const auto idx = pDescriptor->BooleanLookUpTable[i];

                if (pVariableSet->size > idx && pBehaviorGraph->IsWordVariable(idx))
                {
                    pVariableSet->data[idx] = aVariables.Booleans.size() > i ? aVariables.Booleans[i] : false;
                }
            }

            for (size_t i = 0; i < pDescriptor->FloatLookupTable.size(); ++i)
            {
                const auto idx = pDescriptor->FloatLookupTable[i];

                if (pVariableSet->size > idx && pBehaviorGraph->IsWordVariable(idx))
                    *reinterpret_cast<float*>(&pVariableSet->data[idx]) = aVariables.Floats.size() > i ? aVariables.Floats[i] : 0.f;
            }

            for (size_t i = 0; i < pDescriptor->IntegerLookupTable.size(); ++i)
            {
                const auto idx = pDescriptor->IntegerLookupTable[i];

                if (pVariableSet->size > idx && pBehaviorGraph->IsWordVariable(idx))
                    *reinterpret_cast<uint32_t*>(&pVariableSet->data[idx]) = aVariables.Integers.size() > i ? aVariables.Integers[i] : 0;
            }
        }

        pManager->Release();
    }
}

uint32_t TESObjectREFR::GetCellId() const noexcept
{
    if (!parentCell)
        return 0;

    const auto* pWorldSpace = parentCell->worldspace;

    return pWorldSpace != nullptr ? pWorldSpace->formID : parentCell->formID;
}

TESWorldSpace* TESObjectREFR::GetWorldSpace() const noexcept
{
    auto* pParentCell = GetParentCellEx();
    if (pParentCell && !(pParentCell->cellFlags & 1))
        return pParentCell->worldspace;

    return nullptr;
}

ExtraDataList* TESObjectREFR::GetExtraDataList() noexcept
{
    return &extraData;
}

// Delete() should only be used on temporaries
void TESObjectREFR::Delete() const noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, Delete);

    s_pDelete(this);
}

// The game's own disable, not Papyrus's: that one queues a task in the script engine, and a save made before it runs keeps it.
// Loaded later, the task names a form id the game may have given to something else since, as it does a temporary copy's.
void TESObjectREFR::Disable() const noexcept
{
    const_cast<TESObjectREFR*>(this)->DisableImpl();
}

void TESObjectREFR::SetMotionType(const int32_t aMotionType) const noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(void, ObjectReference, SetMotionType, int, bool);

    // Activation stays allowed: a held item still has to be takeable.
    s_pSetMotionType(this, aMotionType, true);
}

// Skyrim: MoveTo() can fail, causing the object to be deleted
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    ScopedReferencesOverride recursionGuard;

    TP_THIS_FUNCTION(TInternalMoveTo, bool, const TESObjectREFR, uint32_t*&, TESObjectCELL*, TESWorldSpace*, const NiPoint3&, const NiPoint3&);

    POINTER_SKYRIMSE(TInternalMoveTo, s_internalMoveTo, 56626, 56227);

    TiltedPhoques::ThisCall(s_internalMoveTo, this, GetNullHandle(), apCell, apCell->worldspace, acPosition, rotation);
}

void TESObjectREFR::SetPositionInPlace(const NiPoint3& acPosition) noexcept
{
    // TESObjectREFR::SetPosition (SE 19363, AE 19790), the function Actor.cpp hooks for actors.
    TP_THIS_FUNCTION(TSetPosition, char, TESObjectREFR, NiPoint3&);
    POINTER_SKYRIMSE(TSetPosition, s_setPosition, 19790, 19790);
    NiPoint3 position = acPosition;
    TiltedPhoques::ThisCall(s_setPosition, this, position);
}

void TESObjectREFR::PayGold(int32_t aAmount) noexcept
{
    ScopedInventoryOverride _;
    PayGoldToContainer(nullptr, aAmount);
}

void TESObjectREFR::PayGoldToContainer(TESObjectREFR* pContainer, int32_t aAmount) noexcept
{
    TP_THIS_FUNCTION(TPayGoldToContainer, void, TESObjectREFR, TESObjectREFR*, int32_t);
    POINTER_SKYRIMSE(TPayGoldToContainer, s_payGoldToContainer, 37511, 37511);
    TiltedPhoques::ThisCall(s_payGoldToContainer, this, pContainer, aAmount);
}

Lock* TESObjectREFR::GetLock() const noexcept
{
    TP_THIS_FUNCTION(TGetLock, Lock*, const TESObjectREFR);
    POINTER_SKYRIMSE(TGetLock, realGetLock, 20223, 19818);

    return TiltedPhoques::ThisCall(realGetLock, this);
}

Lock* TESObjectREFR::CreateLock() noexcept
{
    TP_THIS_FUNCTION(TCreateLock, Lock*, TESObjectREFR);
    POINTER_SKYRIMSE(TCreateLock, realCreateLock, 20221, 20221);

    return TiltedPhoques::ThisCall(realCreateLock, this);
}

void TESObjectREFR::LockChange() noexcept
{
    TiltedPhoques::ThisCall(RealLockChange, this);
}

const float TESObjectREFR::GetHeight() noexcept
{
    auto boundMax = GetBoundMax();
    return boundMax.z - GetBoundMin().z;
}

TESObjectREFR::OpenState TESObjectREFR::GetOpenState() noexcept
{
    using ObjectReference = TESObjectREFR;

    // Missing, it answers kNone (PapyrusLookup), and the door is synced without its open state.
    PAPYRUS_FUNCTION(OpenState, ObjectReference, GetOpenState);

    return s_pGetOpenState(this);
}

void TESObjectREFR::SetOpen(bool aOpen) noexcept
{
    using ObjectReference = TESObjectREFR;

    // Papyrus SetOpen: the door opens or closes by itself, with no actor using it.
    PAPYRUS_FUNCTION(void, ObjectReference, SetOpen, bool);

    s_pSetOpen(this, aOpen);
}

bool TESObjectREFR::IsActivationBlocked() noexcept
{
    using ObjectReference = TESObjectREFR;

    // Papyrus IsActivationBlocked: a script (BlockActivation) takes the activations, and the game's own does not happen.
    // Missing, it answers false.
    PAPYRUS_FUNCTION(bool, ObjectReference, IsActivationBlocked);

    return s_pIsActivationBlocked(this);
}

ExtraContainerChanges::Data* TESObjectREFR::GetContainerChanges() const noexcept
{
    TP_THIS_FUNCTION(TGetContainterChanges, ExtraContainerChanges::Data*, const TESObjectREFR);

    POINTER_SKYRIMSE(TGetContainterChanges, s_getContainerChangs, 16040, 15802);

    return TiltedPhoques::ThisCall(s_getContainerChangs, this);
}

namespace
{
// The base container's count of an item, made positive as the game reads it.
int32_t BaseCount(const TESContainer* apBase, const TESForm* apItem) noexcept
{
    int32_t count = 0;
    if (apBase)
    {
        for (uint32_t i = 0; i < apBase->count; ++i)
        {
            const TESContainer::Entry* pBaseEntry = apBase->entries[i];
            if (pBaseEntry && pBaseEntry->form == apItem)
                count += static_cast<int32_t>(pBaseEntry->count);
        }
    }
    return std::abs(count);
}

// A list's count as the game reads it: its ExtraCount, or 1.
int32_t ListCount(const ExtraDataList* apList) noexcept
{
    const auto* pCount = Cast<ExtraCount>(apList->GetByType(ExtraDataType::Count));
    return pCount ? pCount->count : 1;
}

// The first change entry of an item, the one the game's RemoveItem works on.
const ExtraContainerChanges::Entry* FindEntry(const ExtraContainerChanges::Data* apChanges, const TESForm* apItem) noexcept
{
    if (!apChanges || !apChanges->entries)
        return nullptr;
    for (const ExtraContainerChanges::Entry* pEntry : *apChanges->entries)
        if (pEntry && pEntry->form == apItem)
            return pEntry;
    return nullptr;
}

bool HoldsList(const ExtraContainerChanges::Entry* apEntry, const ExtraDataList* apList) noexcept
{
    if (!apEntry || !apEntry->dataList)
        return false;
    for (const ExtraDataList* pList : *apEntry->dataList)
        if (pList == apList)
            return true;
    return false;
}

// An actor's hands and ammunition point at extra data of their items.
// A hand that points at extra data that is not its item's, freed or given to another item since, is still read there by the game and other plugins.
constexpr const char* kHandNames[] = {"left hand", "right hand", "ammunition"};

// The slots of the hands in an actor's AI process; false for a reference without one.
bool GetHandSlots(TESObjectREFR* apRef, InventoryEntry** (&aSlots)[3]) noexcept
{
    Actor* pActor = Cast<Actor>(apRef);
    MiddleProcess* pMiddle = pActor && pActor->currentProcess ? pActor->currentProcess->middleProcess : nullptr;
    if (!pMiddle)
        return false;
    aSlots[0] = &pMiddle->leftEquippedObject;
    aSlots[1] = &pMiddle->rightEquippedObject;
    aSlots[2] = &pMiddle->ammoEquippedObject;
    return true;
}

// Whether any change entry of the item holds this list.
bool ItemHoldsList(const ExtraContainerChanges::Data* apChanges, const TESForm* apItem, const ExtraDataList* apList) noexcept
{
    for (const ExtraContainerChanges::Entry* pEntry : *apChanges->entries)
        if (pEntry && pEntry->form == apItem && HoldsList(pEntry, apList))
            return true;
    return false;
}

// Whether a hand's list names extra data that is not its item's.
// The game gives each hand entry a list of its own (the rig never saw one shared with an item), so walking it is safe.
// The extra data it names may be freed or reused, so only addresses are compared.
bool NamesStrayExtraData(const ExtraContainerChanges::Data* apChanges, const InventoryEntry* apHand) noexcept
{
    if (!apChanges || !apChanges->entries)
        return false;
    size_t seen = 0;
    for (const ExtraDataList* pList : *apHand->pExtraLists)
    {
        // A hand holds a list or two; the bound keeps a broken list from running on.
        if (++seen > 8)
            break;
        if (pList && !ItemHoldsList(apChanges, apHand->pObject, pList))
            return true;
    }
    return false;
}

void LetHandGo(TESObjectREFR* apRef, InventoryEntry* apEntry, const size_t aHand, const char* acpWhen) noexcept
{
    const Actor* pActor = Cast<Actor>(apRef);
    spdlog::info("HandWatch: {:X} (dead: {}) pointed its {} ({:X}) at extra data {}; it lets go of it", apRef->formID, pActor && pActor->IsDead(), kHandNames[aHand],
                 apEntry->pObject ? apEntry->pObject->formID : 0, acpWhen);
    apEntry->pExtraLists = nullptr;
}

// A removal is watched: what the hands point at is noted before it, and a hand the game left as it was is checked after.
class HandWatch
{
public:
    explicit HandWatch(TESObjectREFR* apRef) noexcept
        : m_pRef(apRef)
    {
        InventoryEntry** slots[3];
        if (!GetHandSlots(apRef, slots))
            return;
        for (size_t i = 0; i < std::size(slots); ++i)
        {
            m_hands[i].pEntry = *slots[i];
            m_hands[i].pLists = m_hands[i].pEntry ? m_hands[i].pEntry->pExtraLists : nullptr;
        }
    }

    void LetGoOfRemoved() noexcept
    {
        InventoryEntry** slots[3];
        if (!GetHandSlots(m_pRef, slots))
            return;
        const ExtraContainerChanges::Data* pChanges = m_pRef->GetContainerChanges();
        for (size_t i = 0; i < std::size(slots); ++i)
        {
            const Hand& hand = m_hands[i];
            // The game set this hand anew, or it pointed at nothing.
            if (!hand.pLists || *slots[i] != hand.pEntry || hand.pEntry->pExtraLists != hand.pLists)
                continue;
            if (NamesStrayExtraData(pChanges, hand.pEntry))
                LetHandGo(m_pRef, hand.pEntry, i, "its item does not hold, after a removal");
        }
    }

private:
    struct Hand
    {
        InventoryEntry* pEntry = nullptr;
        GameList<ExtraDataList>* pLists = nullptr;
    };

    TESObjectREFR* m_pRef;
    Hand m_hands[3];
};
} // namespace

// Other plugins read a living actor's hands on every equip (Enchantment Art Extender reads the right hand's extra data).
// Items also leave an actor by the game's own hand (looting, the dead-body clean-up), which no watch here sees.
// So before a revival and before each equip from its owner, a copy's hands are checked against its items once more.
void TESObjectREFR::LetHandsGoOfStrayExtraData(const char* acpWhen) noexcept
{
    InventoryEntry** slots[3];
    if (!GetHandSlots(this, slots))
        return;

    const ExtraContainerChanges::Data* pChanges = GetContainerChanges();
    for (size_t i = 0; i < std::size(slots); ++i)
    {
        InventoryEntry* pEntry = *slots[i];
        if (pEntry && pEntry->pExtraLists && NamesStrayExtraData(pChanges, pEntry))
            LetHandGo(this, pEntry, i, acpWhen);
    }
}

// The game removes an item's extra data lists one at a time, and a removal does nothing once the item's count is used up.
// So each item gets at least the count its lists hold, and a list counting 0 or less, which holds no item as the game counts it, is set to 1.
void TESObjectREFR::MatchCountsToExtraData() noexcept
{
    ExtraContainerChanges::Data* pChanges = GetContainerChanges();
    if (!pChanges || !pChanges->entries)
        return;

    const TESContainer* pBaseContainer = GetContainer();
    for (ExtraContainerChanges::Entry* pEntry : *pChanges->entries)
    {
        if (!pEntry || !pEntry->form || !pEntry->dataList)
            continue;

        int32_t inLists = 0;
        for (ExtraDataList* pList : *pEntry->dataList)
        {
            // A list without extra data is dropped by the game, not removed.
            if (!pList || !pList->data)
                continue;

            if (auto* pCount = Cast<ExtraCount>(pList->GetByType(ExtraDataType::Count)); pCount && pCount->count <= 0)
            {
                spdlog::warn("RemoveAllItems: {:X} holds item {:X} with extra data counting {}; counted 1 so that it can be removed", formID, pEntry->form->formID,
                             pCount->count);
                pCount->count = 1;
            }
            inLists += ListCount(pList);
        }

        const int32_t cCount = BaseCount(pBaseContainer, pEntry->form) + pEntry->count;
        if (inLists > cCount)
        {
            spdlog::info("RemoveAllItems: {:X} holds {} of item {:X} in extra data but counts {}; counted {} so that removing them ends", formID, inLists,
                         pEntry->form->formID, cCount, inLists);
            pEntry->count += inLists - cCount;
        }
    }
}

// Not the game's RemoveAllItems: after each removal it goes back to an item's first list, and loops forever when a removal leaves that list in place.
// Here every removal must take its list away or lower the item's count; a list or an item that does neither is left, logged, and the rest still go.
// The changes are read again after each removal, as unequipping can merge or free an item's other lists.
void TESObjectREFR::RemoveAllItems() noexcept
{
    ScopedEquipOverride equipOverride;
    ScopedInventoryOverride inventoryOverride;

    MatchCountsToExtraData();

    const TESContainer* pBaseContainer = GetContainer();
    Vector<const TESForm*> stuckItems;
    Vector<const ExtraDataList*> stuckLists;
    const auto isStuckItem = [&stuckItems](const TESForm* apItem) { return std::find(stuckItems.begin(), stuckItems.end(), apItem) != stuckItems.end(); };
    const auto isStuckList = [&stuckLists](const ExtraDataList* apList) { return std::find(stuckLists.begin(), stuckLists.end(), apList) != stuckLists.end(); };

    // One removal per list and per item, with room for lists an unequip splits off.
    size_t budget = 16;
    if (const ExtraContainerChanges::Data* pChanges = GetContainerChanges(); pChanges && pChanges->entries)
    {
        for (const ExtraContainerChanges::Entry* pEntry : *pChanges->entries)
        {
            budget += 2;
            if (pEntry && pEntry->dataList)
                for (const ExtraDataList* pList : *pEntry->dataList)
                    budget += pList ? 2 : 0;
        }
    }
    if (pBaseContainer)
        budget += pBaseContainer->count;

    while (true)
    {
        TESBoundObject* pItem = nullptr;
        ExtraDataList* pList = nullptr;
        int32_t count = 0;
        int32_t countBefore = 0;

        // Levelled lists given to a copy as items are among the changes, and go as the items do.
        const ExtraContainerChanges::Data* pChanges = GetContainerChanges();
        if (pChanges && pChanges->entries)
        {
            for (const ExtraContainerChanges::Entry* pEntry : *pChanges->entries)
            {
                TESBoundObject* pCandidate = pEntry ? Cast<TESBoundObject>(pEntry->form) : nullptr;
                if (!pCandidate || isStuckItem(pCandidate) || FindEntry(pChanges, pCandidate) != pEntry)
                    continue;

                const int32_t cTotal = BaseCount(pBaseContainer, pCandidate) + pEntry->count;
                int32_t loose = cTotal;
                ExtraDataList* pFirstList = nullptr;
                int32_t firstListCount = 0;
                if (pEntry->dataList)
                {
                    for (ExtraDataList* pCandidateList : *pEntry->dataList)
                    {
                        if (!pCandidateList || !pCandidateList->data)
                            continue;
                        loose -= ListCount(pCandidateList);
                        if (!pFirstList && !isStuckList(pCandidateList))
                        {
                            pFirstList = pCandidateList;
                            firstListCount = std::max(ListCount(pCandidateList), 1);
                        }
                    }
                }
                if (pFirstList || loose > 0)
                {
                    pItem = pCandidate;
                    pList = pFirstList;
                    count = pFirstList ? firstListCount : loose;
                    countBefore = cTotal;
                    break;
                }
            }
        }

        // Items of the base container this reference never changed.
        // A levelled list there is no item: the game rolls it when the reference is made.
        if (!pItem && pBaseContainer)
        {
            for (uint32_t i = 0; i < pBaseContainer->count; ++i)
            {
                const TESContainer::Entry* pBaseEntry = pBaseContainer->entries[i];
                TESForm* pForm = pBaseEntry ? pBaseEntry->form : nullptr;
                if (!pForm || pForm->formType == FormType::LeveledItem)
                    continue;
                TESBoundObject* pCandidate = Cast<TESBoundObject>(pForm);
                if (!pCandidate || isStuckItem(pCandidate) || FindEntry(pChanges, pCandidate))
                    continue;
                if (const int32_t cCount = BaseCount(pBaseContainer, pCandidate); cCount > 0)
                {
                    pItem = pCandidate;
                    count = cCount;
                    countBefore = cCount;
                    break;
                }
            }
        }

        if (!pItem)
            return;

        if (budget == 0)
        {
            spdlog::warn("RemoveAllItems: {:X} still held items after its removals ran out; what is left stays", formID);
            return;
        }
        --budget;

        HandWatch hands(this);
        RemoveItem(pItem, count, ITEM_REMOVE_REASON::kRemove, pList, nullptr);
        hands.LetGoOfRemoved();

        // Progress: the list is gone, or the item's count went down.
        const ExtraContainerChanges::Entry* pAfter = FindEntry(GetContainerChanges(), pItem);
        const int32_t cCountAfter = BaseCount(pBaseContainer, pItem) + (pAfter ? pAfter->count : 0);
        const bool cListGone = pList && !HoldsList(pAfter, pList);
        if (!cListGone && cCountAfter >= countBefore)
        {
            spdlog::warn("RemoveAllItems: {:X} kept item {:X} (count {}) after removing {} {}; it stays, the other items still go", formID, pItem->formID, cCountAfter, count,
                         pList ? "with one of its lists" : "loose");
            if (pList)
                stuckLists.push_back(pList);
            else
                stuckItems.push_back(pItem);
        }
    }
}

TESContainer* TESObjectREFR::GetContainer() const noexcept
{
    TP_THIS_FUNCTION(TGetContainer, TESContainer*, const TESObjectREFR);

    POINTER_SKYRIMSE(TGetContainer, s_getContainer, 19702, 19702);

    return TiltedPhoques::ThisCall(s_getContainer, this);
}

int64_t TESObjectREFR::GetItemCountInInventory(TESForm* apItem) const noexcept
{
    int64_t count = GetContainer()->GetItemCount(apItem);

    auto* pContainerChanges = GetContainerChanges()->entries;
    for (auto pChange : *pContainerChanges)
    {
        if (pChange && pChange->form)
        {
            if (pChange->form->formID == apItem->formID)
            {
                count += pChange->count;
                break;
            }
        }
    }

    return count;
}

TESObjectCELL* TESObjectREFR::GetParentCellEx() const noexcept
{
    return parentCell ? parentCell : GetSaveParentCell();
}

void TESObjectREFR::GetItemFromExtraData(Inventory::Entry& arEntry, ExtraDataList* apExtraDataList) noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    if (ExtraCount* pExtraCount = Cast<ExtraCount>(apExtraDataList->GetByType(ExtraDataType::Count)))
    {
        arEntry.Count = pExtraCount->count;
    }

    if (ExtraCharge* pExtraCharge = Cast<ExtraCharge>(apExtraDataList->GetByType(ExtraDataType::Charge)))
    {
        arEntry.ExtraCharge = pExtraCharge->fCharge;
    }

    if (ExtraEnchantment* pExtraEnchantment = Cast<ExtraEnchantment>(apExtraDataList->GetByType(ExtraDataType::Enchantment)))
    {
        TP_ASSERT(pExtraEnchantment->pEnchantment, "Null enchantment in ExtraEnchantment");

        modSystem.GetServerModId(pExtraEnchantment->pEnchantment->formID, arEntry.ExtraEnchantId);

        if (pExtraEnchantment->pEnchantment->formID & 0xFF000000)
        {
            for (EffectItem* pEffectItem : pExtraEnchantment->pEnchantment->listOfEffects)
            {
                TP_ASSERT(pEffectItem, "pEffectItem is null.");
                if (!pEffectItem)
                    continue;

                Inventory::EffectItem effect;
                effect.Magnitude = pEffectItem->data.fMagnitude;
                effect.Area = pEffectItem->data.iArea;
                effect.Duration = pEffectItem->data.iDuration;
                effect.RawCost = pEffectItem->fRawCost;
                modSystem.GetServerModId(pEffectItem->pEffectSetting->formID, effect.EffectId);
                arEntry.EnchantData.Effects.push_back(effect);
            }

            uint32_t objectId = modSystem.GetGameId(arEntry.BaseId);
            TESForm* pObject = TESForm::GetById(objectId);
            if (pObject)
                arEntry.EnchantData.IsWeapon = pObject->formType == FormType::Weapon;
        }

        arEntry.ExtraEnchantCharge = pExtraEnchantment->usCharge;
        arEntry.ExtraEnchantRemoveUnequip = pExtraEnchantment->bRemoveOnUnequip;
    }

    if (ExtraHealth* pExtraHealth = Cast<ExtraHealth>(apExtraDataList->GetByType(ExtraDataType::Health)))
    {
        arEntry.ExtraHealth = pExtraHealth->fHealth;
    }

    if (ExtraPoison* pExtraPoison = Cast<ExtraPoison>(apExtraDataList->GetByType(ExtraDataType::Poison)))
    {
        TP_ASSERT(pExtraPoison->pPoison, "Null poison in ExtraPoison");
        if (pExtraPoison && pExtraPoison->pPoison)
        {
            modSystem.GetServerModId(pExtraPoison->pPoison->formID, arEntry.ExtraPoisonId);
            arEntry.ExtraPoisonCount = pExtraPoison->uiCount;
        }
    }

    if (ExtraSoul* pExtraSoul = Cast<ExtraSoul>(apExtraDataList->GetByType(ExtraDataType::Soul)))
    {
        arEntry.ExtraSoulLevel = (int32_t)pExtraSoul->cSoul;
    }

    /*
    if (ExtraTextDisplayData* pExtraTextDisplayData = Cast<ExtraTextDisplayData>(apExtraDataList->GetByType(ExtraDataType::TextDisplayData)))
    {
        if (pExtraTextDisplayData->DisplayName)
            arEntry.ExtraTextDisplayName = pExtraTextDisplayData->DisplayName;
        else
            arEntry.ExtraTextDisplayName = "";
    }
    */

    arEntry.ExtraWorn = apExtraDataList->Contains(ExtraDataType::Worn);
    arEntry.ExtraWornLeft = apExtraDataList->Contains(ExtraDataType::WornLeft);

    arEntry.IsQuestItem = apExtraDataList->HasQuestObjectAlias();
}

ExtraDataList* TESObjectREFR::GetExtraDataFromItem(const Inventory::Entry& arEntry) noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    ExtraDataList* pExtraDataList = nullptr;

    if (!arEntry.ContainsExtraData())
        return pExtraDataList;

    pExtraDataList = ExtraDataList::New();

    if (arEntry.ExtraCharge > 0.f)
    {
        pExtraDataList->SetChargeData(arEntry.ExtraCharge);
    }

    if (arEntry.ExtraEnchantId != 0)
    {
        EnchantmentItem* pEnchantment = nullptr;
        if (arEntry.ExtraEnchantId.ModId == 0xFFFFFFFF)
        {
            pEnchantment = EnchantmentItem::Create(arEntry.EnchantData);
        }
        else
        {
            uint32_t enchantId = modSystem.GetGameId(arEntry.ExtraEnchantId);
            pEnchantment = Cast<EnchantmentItem>(TESForm::GetById(enchantId));
        }

        // An enchantment this game cannot find or make is left out: an item enchanted with nothing breaks the game's inventory code.
        if (pEnchantment)
            pExtraDataList->SetEnchantmentData(pEnchantment, arEntry.ExtraEnchantCharge, arEntry.ExtraEnchantRemoveUnequip);
        else
            spdlog::error("Item {:X}:{:X}: its enchantment {:X}:{:X} was not found or made here; the item is given without it", arEntry.BaseId.ModId, arEntry.BaseId.BaseId,
                          arEntry.ExtraEnchantId.ModId, arEntry.ExtraEnchantId.BaseId);
    }

    if (arEntry.ExtraPoisonId != 0)
    {
        // TODO: does poison have the same temp problem as enchants?
        // doesn't seem to be the case, there are only like 3 poisons, and no custom ones
        TP_ASSERT(arEntry.ExtraPoisonId.ModId != 0xFFFFFFFF, "Poison is sent as temp!");

        uint32_t poisonId = modSystem.GetGameId(arEntry.ExtraPoisonId);
        if (AlchemyItem* pPoison = Cast<AlchemyItem>(TESForm::GetById(poisonId)))
        {
            pExtraDataList->SetPoison(pPoison, arEntry.ExtraPoisonCount);
        }
    }

    if (arEntry.ExtraHealth > 0.f)
    {
        pExtraDataList->SetHealth(arEntry.ExtraHealth);
    }

    if (arEntry.ExtraSoulLevel > 0 && arEntry.ExtraSoulLevel <= 5)
    {
        pExtraDataList->SetSoulData(static_cast<SOUL_LEVEL>(arEntry.ExtraSoulLevel));
    }

    if (arEntry.ExtraWorn)
    {
        pExtraDataList->SetWorn(false);
    }

    if (arEntry.ExtraWornLeft)
    {
        pExtraDataList->SetWorn(true);
    }

    // TODO: this is causing crashes
    /*
    if (!arEntry.ExtraTextDisplayName.empty())
    {
        ExtraTextDisplayData* pExtraText = Memory::Allocate<ExtraTextDisplayData>();
        *((uint64_t*)pExtraText) = 0x1416244D0;
        pExtraText->next = nullptr;
        pExtraText->DisplayName = arEntry.ExtraTextDisplayName.c_str();
        pExtraText->usCustomNameLength = arEntry.ExtraTextDisplayName.length();
        pExtraText->iOwnerInstance = -2;
        pExtraText->fTemperFactor = 1.0F;
        pExtraDataList->Add(ExtraDataType::TextDisplayData, pExtraText);
    }
    */

    if (pExtraDataList->data == nullptr)
    {
        Memory::Delete(pExtraDataList->bitfield);
        Memory::Delete(pExtraDataList);
        pExtraDataList = nullptr;
    }

    return pExtraDataList;
}

Inventory TESObjectREFR::GetInventory() const noexcept
{
    return GetInventory([](TESForm& aForm) { return true; });
}

Inventory TESObjectREFR::GetInventory(std::function<bool(TESForm&)> aFilter) const noexcept
{
    auto& modSystem = World::Get().GetModSystem();
    Inventory inventory{};

    if (TESContainer* pBaseContainer = GetContainer())
    {
        for (int i = 0; i < pBaseContainer->count; i++)
        {
            TESContainer::Entry* pGameEntry = pBaseContainer->entries[i];
            if (!pGameEntry || !pGameEntry->form)
            {
                spdlog::warn("Entry or form for inventory item is null.");
                continue;
            }

            if (!aFilter(*pGameEntry->form))
                continue;

            Inventory::Entry entry;
            modSystem.GetServerModId(pGameEntry->form->formID, entry.BaseId);
            entry.Count = pGameEntry->count;

            inventory.Entries.push_back(std::move(entry));
        }
    }

    Inventory extraInventory{};

    auto pExtraContChangesEntries = GetContainerChanges()->entries;
    for (auto pGameEntry : *pExtraContChangesEntries)
    {
        if (!pGameEntry)
            continue;

        if (!aFilter(*pGameEntry->form))
            continue;

        Inventory::Entry entry{};
        modSystem.GetServerModId(pGameEntry->form->formID, entry.BaseId);
        entry.Count = pGameEntry->count;

        for (ExtraDataList* pExtraDataList : *pGameEntry->dataList)
        {
            if (!pExtraDataList)
                continue;

            Inventory::Entry innerEntry;
            innerEntry.BaseId = entry.BaseId;
            innerEntry.Count = 1;

            GetItemFromExtraData(innerEntry, pExtraDataList);

            entry.Count -= innerEntry.Count;

            extraInventory.Entries.push_back(std::move(innerEntry));
        }

        if (entry.Count != 0)
            extraInventory.Entries.push_back(std::move(entry));
    }

    spdlog::debug("ExtraInventory count: {}", extraInventory.Entries.size());

    Inventory minimizedExtraInventory{};

    for (auto& entry : extraInventory.Entries)
    {
        auto duplicate = std::find_if(minimizedExtraInventory.Entries.begin(), minimizedExtraInventory.Entries.end(), [entry](const Inventory::Entry& newEntry) { return newEntry.CanBeMerged(entry); });

        if (duplicate == std::end(minimizedExtraInventory.Entries))
        {
            minimizedExtraInventory.Entries.push_back(entry);
            continue;
        }

        duplicate->Count += entry.Count;
    }

    spdlog::debug("MinExtraInventory count: {}", minimizedExtraInventory.Entries.size());

    for (auto& entry : minimizedExtraInventory.Entries)
    {
        if (entry.ContainsExtraData())
            continue;

        auto duplicate = std::find_if(inventory.Entries.begin(), inventory.Entries.end(), [entry](const Inventory::Entry& newEntry) { return newEntry.CanBeMerged(entry); });

        if (duplicate == std::end(inventory.Entries))
            continue;

        entry.Count += duplicate->Count;
        duplicate->Count = 0;
    }

    spdlog::debug("MinExtraInventory count after: {}", minimizedExtraInventory.Entries.size());

    inventory.Entries.insert(inventory.Entries.end(), minimizedExtraInventory.Entries.begin(), minimizedExtraInventory.Entries.end());

    spdlog::debug("Inventory count before: {}", inventory.Entries.size());

    inventory.RemoveByFilter([](const auto& entry) { return entry.Count == 0; });

    spdlog::debug("Inventory count after: {}", inventory.Entries.size());

    return inventory;
}

Inventory TESObjectREFR::GetArmor() const noexcept
{
    return GetInventory([](TESForm& aForm) { return aForm.formType == FormType::Armor; });
}

Inventory TESObjectREFR::GetWornArmor() const noexcept
{
    Inventory wornArmor = GetArmor();
    wornArmor.RemoveByFilter([](const auto& entry) { return !entry.IsWorn(); });
    return wornArmor;
}

bool TESObjectREFR::IsItemInInventory(uint32_t aFormID) const noexcept
{
    Inventory inventory = GetInventory([aFormID](TESForm& aForm) { return aForm.formID == aFormID; });
    return !inventory.Entries.empty();
}

void TESObjectREFR::SetInventory(const Inventory& aInventory) noexcept
{
    spdlog::debug("Setting inventory for {:X}", formID);

    ScopedInventoryOverride _;

    RemoveAllItems();

    for (const Inventory::Entry& entry : aInventory.Entries)
    {
        if (entry.Count != 0)
            AddOrRemoveItem(entry, true);
    }
}

Vector<uint32_t> TESObjectREFR::RemoveNonQuestItems(Inventory& aCurrentInventory) noexcept
{
    ScopedEquipOverride equipOverride_;

    Vector<uint32_t> questEntries{};

    auto& modSystem = World::Get().GetModSystem();

    for (auto& entry : aCurrentInventory.Entries)
    {
        if (entry.IsQuestItem)
        {
            uint32_t gameId = modSystem.GetGameId(entry.BaseId);
            questEntries.emplace_back(gameId);
            continue;
        }

        if (entry.Count <= 0)
            continue;

        entry.Count = -entry.Count;
        AddOrRemoveItem(entry, true);
    }

    return questEntries;
}

void TESObjectREFR::SetInventoryRetainingQuestItems(Inventory& aCurrentInventory, const Inventory& acSourceInventory) noexcept
{
    spdlog::debug("Setting inventory for {:X}", formID);

    ScopedInventoryOverride _;

    // Remove all non-quest items first
    Vector<uint32_t> questItemIds = RemoveNonQuestItems(aCurrentInventory);
    auto& modSystem = World::Get().GetModSystem();

    for (const auto& entry : acSourceInventory.Entries)
    {
        uint32_t gameId = modSystem.GetGameId(entry.BaseId);

        // If the item is not in the list of quest items, add it
        if (std::find(questItemIds.begin(), questItemIds.end(), gameId) == questItemIds.end())
        {
            if (entry.Count != 0)
                AddOrRemoveItem(entry, true);
        }
    }
}

void TESObjectREFR::AddOrRemoveItem(const Inventory::Entry& arEntry, bool aIsSettingInventory, bool aPickedUp) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();

    uint32_t objectId = modSystem.GetGameId(arEntry.BaseId);
    TESBoundObject* pObject = Cast<TESBoundObject>(TESForm::GetById(objectId));
    if (!pObject)
    {
        spdlog::warn("{}: Object to add not found, {:X}:{:X}.", __FUNCTION__, arEntry.BaseId.ModId, arEntry.BaseId.BaseId);
        return;
    }

    // An inventory carries its base container's levelled lists, which are no items: added, they would stay as lists in this one.
    if (pObject->formType == FormType::LeveledItem)
        return;

    ExtraDataList* pExtraDataList = GetExtraDataFromItem(arEntry);

    // A quest object another player picked up from the world already reached their copy here, through the replayed pick-up.
    // Only that pick-up's own change is skipped: a quest item taken from a container or given by a script is added as usual.
    // This game's player still gets theirs below.
    Actor* pCopy = Cast<Actor>(this);
    const bool cAlreadyOnCopy = aPickedUp && arEntry.IsQuestItem && arEntry.Count > 0 && !aIsSettingInventory && pCopy && pCopy->GetExtension()->IsRemotePlayer() &&
                                IsItemInInventory(objectId);
    if (cAlreadyOnCopy)
        spdlog::info("QuestItem: {:X} already reached copy {:X} through its owner's pick-up; not added again", objectId, formID);

    if (arEntry.Count > 0 && !cAlreadyOnCopy)
    {
        bool isWorn = false;
        bool isWornLeft = false;
        if (pExtraDataList)
        {
            isWorn = pExtraDataList->Contains(ExtraDataType::Worn);
            isWornLeft = pExtraDataList->Contains(ExtraDataType::WornLeft);
        }

        spdlog::debug("Adding item {:X}, count {}", pObject->formID, arEntry.Count);
        AddObjectToContainer(pObject, pExtraDataList, arEntry.Count, nullptr);

        // The game's equip code reads the actor's AI process, and an actor not loaded here has none.
        if (Actor* pWearer = Cast<Actor>(this); pWearer && pWearer->HasLoadedProcess())
        {
            if (isWorn)
                EquipManager::Get()->Equip(pWearer, pObject, nullptr, arEntry.Count, DefaultObjectManager::Get().rightEquipSlot, false, true, false, false);
            else if (isWornLeft)
                EquipManager::Get()->Equip(pWearer, pObject, nullptr, arEntry.Count, DefaultObjectManager::Get().leftEquipSlot, false, true, false, false);
        }
    }
    else if (arEntry.Count < 0)
    {
        spdlog::debug("Removing item {:X}, count {}", pObject->formID, -arEntry.Count);
        HandWatch hands(this);
        RemoveItem(pObject, -arEntry.Count, ITEM_REMOVE_REASON::kRemove, pExtraDataList, nullptr);
        hands.LetGoOfRemoved();
    }

    // TODO(cosideci): this is still flawed. Adding the refr to the quest leader is hard.
    // It is still recommended that the quest leader loots all quest items.
    if (arEntry.IsQuestItem && arEntry.Count > 0 && !aIsSettingInventory)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();

        if (!pPlayer->IsItemInInventory(objectId))
        {
            Actor* pActor = Cast<Actor>(this);
            if (pActor && pActor->GetExtension()->IsRemotePlayer())
                pPlayer->AddOrRemoveItem(arEntry);
        }
    }

    UpdateItemList(nullptr);
}

void TESObjectREFR::UpdateItemList(TESForm* pUnkForm) noexcept
{
    TP_THIS_FUNCTION(TUpdateItemList, void, TESObjectREFR, TESForm*);
    POINTER_SKYRIMSE(TUpdateItemList, updateItemList, 52849, 51911);
    TiltedPhoques::ThisCall(updateItemList, this, pUnkForm);
}

bool TESObjectREFR::Activate(TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* aObjectToGet, int32_t aCount, char aDefaultProcessing) noexcept
{
    ScopedActivateOverride _;

    return TiltedPhoques::ThisCall(RealActivate, this, apActivator, aUnk1, aObjectToGet, aCount, aDefaultProcessing);
}

void TESObjectREFR::EnableImpl() noexcept
{
    TP_THIS_FUNCTION(TEnableImpl, void, TESObjectREFR, bool aResetInventory);

    POINTER_SKYRIMSE(TEnableImpl, s_enable, 19800, 19373);

    TiltedPhoques::ThisCall(s_enable, this, false);
}

uint32_t TESObjectREFR::GetAnimationVariableInt(BSFixedString* apVariableName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(uint32_t, ObjectReference, GetAnimationVariableInt, BSFixedString*);

    return s_pGetAnimationVariableInt(this, apVariableName);
}

static thread_local bool s_cancelAnimationWaitEvent = false;

bool TESObjectREFR::PlayAnimationAndWait(BSFixedString* apAnimation, BSFixedString* apEventName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(bool, ObjectReference, PlayAnimationAndWait, BSFixedString*, BSFixedString*);

    s_cancelAnimationWaitEvent = true;
    bool result = s_pPlayAnimationAndWait(this, apAnimation, apEventName);
    s_cancelAnimationWaitEvent = false;
    return result;
}

#define OBJECT_ANIM_SYNC 0

bool TP_MAKE_THISCALL(HookPlayAnimationAndWait, void, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apAnimation, BSFixedString* apEventName)
{
    spdlog::debug("Animation: {}, EventName: {}", apAnimation->AsAscii(), apEventName->AsAscii());

#if OBJECT_ANIM_SYNC
    if (!s_cancelAnimationWaitEvent && (apSelf->formID < 0xFF000000))
        World::Get().GetRunner().Trigger(ScriptAnimationEvent(apSelf->formID, apAnimation->AsAscii(), apEventName->AsAscii()));
#endif

    return TiltedPhoques::ThisCall(RealPlayAnimationAndWait, apThis, auiStackID, apSelf, apAnimation, apEventName);
}

static thread_local bool s_cancelAnimationEvent = false;

bool TESObjectREFR::PlayAnimation(BSFixedString* apEventName) noexcept
{
    using ObjectReference = TESObjectREFR;

    PAPYRUS_FUNCTION(bool, ObjectReference, PlayAnimation, BSFixedString*);

    s_cancelAnimationEvent = true;
    bool result = s_pPlayAnimation(this, apEventName);
    s_cancelAnimationEvent = false;
    return result;
}

bool TESObjectREFR::SendAnimationEvent(BSFixedString* apEventName) noexcept
{
    return animationGraphHolder.SendAnimationEvent(apEventName);
}

bool TP_MAKE_THISCALL(HookPlayAnimation, void, uint32_t auiStackID, TESObjectREFR* apSelf, BSFixedString* apEventName)
{
    spdlog::debug("EventName: {}", apEventName->AsAscii());

#if OBJECT_ANIM_SYNC
    if (!s_cancelAnimationEvent && (apSelf->formID < 0xFF000000))
        World::Get().GetRunner().Trigger(ScriptAnimationEvent(apSelf->formID, String{}, apEventName->AsAscii()));
#endif

    return TiltedPhoques::ThisCall(RealPlayAnimation, apThis, auiStackID, apSelf, apEventName);
}

bool TP_MAKE_THISCALL(HookActivate, TESObjectREFR, TESObjectREFR* apActivator, uint8_t aUnk1, TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing)
{
#ifdef SKYRIMVR
    // Also guard activations issued directly by VR interactions, outside the native input handlers.
    if (CalendarService::AreSleepControlsLocked() && apActivator == PlayerCharacter::Get() && !ScopedActivateOverride::IsOverriden())
        return false;
#endif
    Actor* pActivator = Cast<Actor>(apActivator);

    // A copy of another game's actor never goes through a load door on its own here: its owner's game reports where it went.
    // Otherwise this game's AI takes the copy to a cell its owner never entered, where it is removed and spawned again.
    // The owner's own use of the door is replayed with TESObjectREFR::Activate, and stops here too when the game carries it out (live-door-copy-replay).
    const ActorExtension* pActivatorExtension = pActivator ? pActivator->GetExtension() : nullptr;
    if (pActivatorExtension && pActivatorExtension->IsRemote() && apThis->baseForm && apThis->baseForm->formType == FormType::Door &&
        apThis->extraData.Contains(ExtraDataType::Teleport))
    {
        // The copy's AI tries again while its package lasts, so this is logged at most every 5 s.
        static std::atomic<int64_t> s_lastRefusalLogMs{0};
        const int64_t cNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t lastMs = s_lastRefusalLogMs.load();
        if (cNowMs - lastMs >= 5000 && s_lastRefusalLogMs.compare_exchange_strong(lastMs, cNowMs))
            spdlog::info("DoorCopy: load door {:X} not used by {:X}, a copy of another game's actor; its owner's game reports where it goes", apThis->formID, pActivator->formID);
        return false;
    }

    // A living copy of another player's follower is not this player's to talk to, recruit or steal from: her own game runs her.
    if (Actor* pTarget = apActivator == PlayerCharacter::Get() ? Cast<Actor>(apThis) : nullptr)
    {
        const ActorExtension* pTargetExtension = pTarget->GetExtension();
        if (pTargetExtension && pTargetExtension->IsFollowerCopy() && !pTarget->IsDead())
        {
            spdlog::info("Follower: copy {:X} is another player's follower; activating her does nothing here", pTarget->formID);
            return false;
        }
    }

    // Exclude books from activation since only reading them removes them from the cell
    // Note: Books are now unsynced 
    if (pActivator && apThis->baseForm->formType != FormType::Book)
    {
        auto openState = TESObjectREFR::kNone;
        bool blocked = false;
        if (apThis->baseForm->formType == FormType::Door)
        {
            openState = apThis->GetOpenState();
            // Read now: a script can block the door in its own OnActivate, before the queued event is handled.
            blocked = apThis->IsActivationBlocked();
        }

        World::Get().GetRunner().Trigger(
            ActivateEvent(apThis, pActivator, apObjectToGet, aCount, aDefaultProcessing, aUnk1, openState, false, blocked)
        );
    }

    return TiltedPhoques::ThisCall(RealActivate, apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
}

void TP_MAKE_THISCALL(HookAddInventoryItem, TESObjectREFR, TESBoundObject* apItem, ExtraDataList* apExtraData, int32_t aCount, TESObjectREFR* apOldOwner)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        auto& modSystem = World::Get().GetModSystem();

        Inventory::Entry item{};
        modSystem.GetServerModId(apItem->formID, item.BaseId);
        item.Count = aCount;

        if (apExtraData)
            apThis->GetItemFromExtraData(item, apExtraData);

        QueueReferenceInventoryChange(apThis, InventoryChangeEvent(apThis->formID, std::move(item)), apOldOwner);
    }

    spdlog::debug("Adding inventory item {:X} to {:X}", apItem->formID, apThis->formID);

    TiltedPhoques::ThisCall(RealAddInventoryItem, apThis, apItem, apExtraData, aCount, apOldOwner);
}

BSPointerHandle<TESObjectREFR>*
TP_MAKE_THISCALL(HookRemoveInventoryItem, TESObjectREFR, BSPointerHandle<TESObjectREFR>* apResult, TESBoundObject* apItem, int32_t aCount, ITEM_REMOVE_REASON aReason, ExtraDataList* apExtraList, TESObjectREFR* apMoveToRef, const NiPoint3* apDropLoc, const NiPoint3* apRotate)
{
    // The player dropping an item from the inventory comes here, as RemoveItem with the "dropping" reason.
    // It does not pass through Actor::DropObject, the only other place drops are announced to the other players.
    const bool cOwnDrop = aReason == ITEM_REMOVE_REASON::kDropping && !ScopedInventoryOverride::IsOverriden() && apThis == PlayerCharacter::Get();
    bool takenOffBody = false;
    Inventory::Entry dropped{};

    if (!ScopedInventoryOverride::IsOverriden())
    {
        auto& modSystem = World::Get().GetModSystem();

        Inventory::Entry item{};
        modSystem.GetServerModId(apItem->formID, item.BaseId);

        if (apExtraList)
        {
            ScopedExtraDataOverride _;
            apThis->GetItemFromExtraData(item, apExtraList);
        }

        item.Count = -aCount;
        dropped = item;

        // Out of a dead body another player's game runs, into the world next to this player: a hand took it.
        // That game's body loses it too, or its owner could still take it there and one item would be two.
        const auto cTaken = aReason == ITEM_REMOVE_REASON::kDropping && !apMoveToRef ? Utils::GetBodyTakeToken(Cast<Actor>(apThis), apDropLoc) : std::nullopt;
        if (cTaken)
        {
            spdlog::info("BodyTake: {:X} x{} taken off {:X}, a body another game runs; its owner's body loses it too", apItem->formID, aCount, apThis->formID);
            InventoryChangeEvent event(apThis->formID, std::move(item));
            event.ServerId = cTaken->ServerId;
            event.OwnershipEpoch = cTaken->OwnershipEpoch;
            World::Get().GetRunner().Trigger(std::move(event));
            takenOffBody = true;
            // What lies in the world is worn by nobody.
            dropped.ExtraWorn = false;
            dropped.ExtraWornLeft = false;
        }
        else
            QueueReferenceInventoryChange(apThis, InventoryChangeEvent(apThis->formID, std::move(item)), apMoveToRef);
    }

    spdlog::debug("Removing inventory item {:X} from {:X}", apItem->formID, apThis->formID);

    ScopedEquipOverride _;

    if (!cOwnDrop && !takenOffBody)
        return TiltedPhoques::ThisCall(RealRemoveInventoryItem, apThis, apResult, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apRotate);

    // Held while the game drops it, so a DropObject hook on the way neither re-announces the drop nor queues the change twice.
    BSPointerHandle<TESObjectREFR>* pResult;
    {
        ScopedInventoryOverride __;
        pResult = TiltedPhoques::ThisCall(RealRemoveInventoryItem, apThis, apResult, apItem, aCount, aReason, apExtraList, apMoveToRef, apDropLoc, apRotate);
    }

    // The reference exists now; where it lies is what the other players need.
    if (pResult)
    {
        if (TESObjectREFR* pDropped = TESObjectREFR::GetByHandle(pResult->handle.iBits))
            World::Get().GetRunner().Trigger(ItemDroppedEvent{pDropped->formID, dropped});
    }

    return pResult;
}

void TP_MAKE_THISCALL(HookRotateX, TESObjectREFR, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move, except a corpse falling with its own ragdoll (see HookSetPosition).
        const ActorExtension* pExtension = pActor->GetExtension();
    if (pExtension && pExtension->IsRemote() && !pActor->actorState.IsDeadOrDying())
            return;
    }

    return TiltedPhoques::ThisCall(RealRotateX, apThis, aAngle);
}

void TP_MAKE_THISCALL(HookRotateY, TESObjectREFR, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move, except a corpse falling with its own ragdoll (see HookSetPosition).
        const ActorExtension* pExtension = pActor->GetExtension();
    if (pExtension && pExtension->IsRemote() && !pActor->actorState.IsDeadOrDying())
            return;
    }

    return TiltedPhoques::ThisCall(RealRotateY, apThis, aAngle);
}

void TP_MAKE_THISCALL(HookRotateZ, TESObjectREFR, float aAngle)
{
    if (apThis->formType == Actor::Type)
    {
        const auto pActor = static_cast<Actor*>(apThis);
        // We don't allow remotes to move, except a corpse falling with its own ragdoll (see HookSetPosition).
        const ActorExtension* pExtension = pActor->GetExtension();
    if (pExtension && pExtension->IsRemote() && !pActor->actorState.IsDeadOrDying())
            return;
    }

    return TiltedPhoques::ThisCall(RealRotateZ, apThis, aAngle);
}

void TP_MAKE_THISCALL(HookLockChange, TESObjectREFR)
{
    TiltedPhoques::ThisCall(RealLockChange, apThis);
    const auto* pLock = apThis->GetLock();
    if(pLock)
        World::Get().GetRunner().Trigger(LockChangeEvent(apThis->formID, pLock->IsLocked(), pLock->lockLevel));
    else
        World::Get().GetRunner().Trigger(LockChangeEvent(apThis->formID, false, 0));
}

#ifdef SKYRIMVR
TP_THIS_FUNCTION(TReferenceEnable, void, TESObjectREFR, bool aResetInventory);
TP_THIS_FUNCTION(TReferenceDisable, void, TESObjectREFR);
static TReferenceEnable* RealReferenceEnable = nullptr;
static TReferenceDisable* RealReferenceDisable = nullptr;

// TESObjectREFR's Disable, after the slot VR adds for AttachWeapon (SE 0x89).
constexpr size_t kDisableSlot = 0x8A;

// A switch that changed the disabled flag is reported; ObjectService decides whether the party gets it.
void ReportEnableChange(const TESObjectREFR* apRef, bool aWasDisabled) noexcept
{
    if (apRef && aWasDisabled != apRef->IsDisabled() && apRef->formID < 0xFF000000 && !IsProcessExiting())
        World::Get().GetRunner().Trigger(ReferenceEnableEvent(apRef->formID, !apRef->IsDisabled()));
}

void TP_MAKE_THISCALL(HookReferenceEnable, TESObjectREFR, bool aResetInventory)
{
    const bool cWasDisabled = apThis->IsDisabled();
    TiltedPhoques::ThisCall(RealReferenceEnable, apThis, aResetInventory);
    ReportEnableChange(apThis, cWasDisabled);
}

void TP_MAKE_THISCALL(HookReferenceDisable, TESObjectREFR)
{
    const bool cWasDisabled = apThis->IsDisabled();
    TiltedPhoques::ThisCall(RealReferenceDisable, apThis);
    ReportEnableChange(apThis, cWasDisabled);
}

static TiltedPhoques::Initializer s_referenceEnableHooks(
    []()
    {
        POINTER_SKYRIMSE(TReferenceEnable, s_referenceEnable, 19800, 19373);
        VersionDbPtr<void*> refrVtable(235511);
        RealReferenceEnable = s_referenceEnable.Get();
        if (auto* pVtable = reinterpret_cast<void* const*>(refrVtable.Get()))
            RealReferenceDisable = static_cast<TReferenceDisable*>(pVtable[kDisableSlot]);
        TP_HOOK(&RealReferenceEnable, HookReferenceEnable);
        TP_HOOK(&RealReferenceDisable, HookReferenceDisable);
    });
#endif

static TiltedPhoques::Initializer s_objectReferencesHooks(
    []()
    {
        POINTER_SKYRIMSE(TLockChange, s_lockChange, 19512, 19512);
        // POINTER_SKYRIMSE(TSetLeveledCreature, s_SetLeveledCreature, 20231);
        POINTER_SKYRIMSE(TRotate, s_rotateX, 19787, 19787);
        POINTER_SKYRIMSE(TRotate, s_rotateY, 19788, 19788);
        POINTER_SKYRIMSE(TRotate, s_rotateZ, 19789, 19362);
        POINTER_SKYRIMSE(TActivate, s_activate, 19796, 19369);
        POINTER_SKYRIMSE(TAddInventoryItem, s_addInventoryItem, 19708, 19708);
        POINTER_SKYRIMSE(TRemoveInventoryItem, s_removeInventoryItem, 19689, 19689);
        POINTER_SKYRIMSE(TPlayAnimationAndWait, s_playAnimationAndWait, 56206, 56206);
        POINTER_SKYRIMSE(TPlayAnimation, s_playAnimation, 56205, 56205);

        RealLockChange = s_lockChange.Get();
        // RealSetLeveledCreature = s_SetLeveledCreature.Get();
        RealRotateX = s_rotateX.Get();
        RealRotateY = s_rotateY.Get();
        RealRotateZ = s_rotateZ.Get();
        RealActivate = s_activate.Get();
        RealAddInventoryItem = s_addInventoryItem.Get();
        RealRemoveInventoryItem = s_removeInventoryItem.Get();
        RealPlayAnimationAndWait = s_playAnimationAndWait.Get();
        RealPlayAnimation = s_playAnimation.Get();

        TP_HOOK(&RealLockChange, HookLockChange);
        // TP_HOOK(&RealSetLeveledCreature, HookSetLeveledCreature);
        TP_HOOK(&RealRotateX, HookRotateX);
        TP_HOOK(&RealRotateY, HookRotateY);
        TP_HOOK(&RealRotateZ, HookRotateZ);
        TP_HOOK(&RealActivate, HookActivate);
        TP_HOOK(&RealAddInventoryItem, HookAddInventoryItem);
        TP_HOOK(&RealRemoveInventoryItem, HookRemoveInventoryItem);
        TP_HOOK(&RealPlayAnimationAndWait, HookPlayAnimationAndWait);
        TP_HOOK(&RealPlayAnimation, HookPlayAnimation);
    });
