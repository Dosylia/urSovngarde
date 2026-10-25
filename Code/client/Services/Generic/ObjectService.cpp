#include <Services/ObjectService.h>

#include <World.h>
#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/RequestReferenceEnable.h>
#include <Messages/NotifyReferenceEnable.h>
#include <Events/ReferenceEnableEvent.h>
#include <Services/PartyService.h>
#include <Structs/DoorState.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/BGSEncounterZone.h>
#include <Forms/TESFaction.h>
#include <Games/TES.h>

#include <inttypes.h>

// The states travel as the game's own numbers.
static_assert(DoorState::kOpen == TESObjectREFR::kOpen && DoorState::kOpening == TESObjectREFR::kOpening && DoorState::kClosed == TESObjectREFR::kClosed &&
              DoorState::kClosing == TESObjectREFR::kClosing && DoorState::kNone == TESObjectREFR::kNone);

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&ObjectService::OnDisconnected>(this);
    m_cellChangeConnection = aDispatcher.sink<CellChangeEvent>().connect<&ObjectService::OnCellChange>(this);
    m_onActivateConnection = aDispatcher.sink<ActivateEvent>().connect<&ObjectService::OnActivate>(this);
    m_activateConnection = aDispatcher.sink<NotifyActivate>().connect<&ObjectService::OnActivateNotify>(this);
    m_lockChangeConnection = aDispatcher.sink<LockChangeEvent>().connect<&ObjectService::OnLockChange>(this);
    m_lockChangeNotifyConnection = aDispatcher.sink<NotifyLockChange>().connect<&ObjectService::OnLockChangeNotify>(this);
    m_assignObjectConnection = aDispatcher.sink<AssignObjectsResponse>().connect<&ObjectService::OnAssignObjectsResponse>(this);
    m_scriptAnimationConnection = aDispatcher.sink<ScriptAnimationEvent>().connect<&ObjectService::OnScriptAnimationEvent>(this);
    m_scriptAnimationNotifyConnection = aDispatcher.sink<NotifyScriptAnimation>().connect<&ObjectService::OnNotifyScriptAnimation>(this);
    m_referenceEnableConnection = aDispatcher.sink<ReferenceEnableEvent>().connect<&ObjectService::OnReferenceEnableEvent>(this);
    m_referenceEnableNotifyConnection = aDispatcher.sink<NotifyReferenceEnable>().connect<&ObjectService::OnNotifyReferenceEnable>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&ObjectService::OnUpdate>(this);

    EventDispatcherManager::Get()->activateEvent.RegisterSink(this);
}

bool IsPlayerHome(const TESObjectCELL* pCell) noexcept
{
    if (pCell && pCell->loadedCellData && pCell->loadedCellData->encounterZone)
    {
        // Only return true if cell has the NoResetZone encounter zone
        if (pCell->loadedCellData->encounterZone->formID == 0xf90b1)
        {
            switch (pCell->formID)
            {
            case 0xeec55: // one known exception: Sinderion's Field Lab
                return false;
            default: return true;
            }
        }
    }

    return false;
}

// Find each loaded faction's containers for a jailed player's belongings and stolen items.
// Do not sync them: each player's items must stay separate (#700).
// Only compare the container pointers; never read through them.
Set<const TESObjectREFR*> GetPlayerStashContainers() noexcept
{
    Set<const TESObjectREFR*> containers{};

    ModManager* pModManager = ModManager::Get();
    if (!pModManager)
        return containers;

    for (const TESFaction* pFaction : pModManager->factions)
    {
        if (!pFaction)
            continue;

        if (pFaction->crimeData.playerInventoryContainer)
            containers.insert(pFaction->crimeData.playerInventoryContainer);

        if (pFaction->crimeData.stolenGoodsContainer)
            containers.insert(pFaction->crimeData.stolenGoodsContainer);
    }

    return containers;
}

bool ShouldSyncObject(const TESObjectREFR* apObject, const Set<const TESObjectREFR*>& acPlayerStashContainers) noexcept
{
    if (!apObject)
        return false;

    if (acPlayerStashContainers.contains(apObject))
        return false;

    // Quest chests that take the player's whole inventory without going through faction crime data.
    switch (apObject->formID)
    {
    case 0x39CF1: // Don't sync the chest in the "Diplomatic Immunity" quest
        return false;
    case 0x3EF03: // ...as well as in the "No One Escapes Cidhna Mine" quest
        return false;
    default:
        return true;
    }
}

void ObjectService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // TODO(cosideci): clear object components
    m_enableChanges.clear();
    m_enableWaiting.clear();
}

void ObjectService::OnCellChange(const CellChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer->parentCell;

    // Player homes should not be synced, so that chest contents,
    // which are often used as storage, are never accidentally wiped.
    if (!World::Get().GetServerSettings().SyncPlayerHomes && IsPlayerHome(pCell))
        return;

    GameId cellId{};
    if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", pCell->formID);
        return;
    }

    GameId worldSpaceId{};
    if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
    {
        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, worldSpaceId))
        {
            spdlog::error("Server world space id not found for world space form id {:X}", pWorldSpace->formID);
            return;
        }
    }

    Vector<FormType> formTypes = {FormType::Container, FormType::Door};
    // Door seemed to be at the wrong form id (29, now 32), verify this.
    Vector<TESObjectREFR*> objects = pCell->GetRefsByFormTypes(formTypes);

    AssignObjectsRequest request{};

    const Set<const TESObjectREFR*> playerStashContainers = GetPlayerStashContainers();

    for (TESObjectREFR* pObject : objects)
    {
        if (!ShouldSyncObject(pObject, playerStashContainers))
        {
            spdlog::warn("Excluding sync for {:X}", pObject->formID);
            continue;
        }

        ObjectData objectData{};
        objectData.CellId = cellId;
        objectData.WorldSpaceId = worldSpaceId;
        objectData.CurrentCoords = GridCellCoords::CalculateGridCellCoords(pObject->position.x, pObject->position.y);

        if (!m_world.GetModSystem().GetServerModId(pObject->formID, objectData.Id))
        {
            spdlog::error("Server form id not found for object with form id {:X}", pObject->formID);
            continue;
        }

        if (Lock* pLock = pObject->GetLock())
        {
            objectData.CurrentLockData.IsLocked = pLock->IsLocked();
            objectData.CurrentLockData.LockLevel = pLock->lockLevel;
        }

        if (pObject->baseForm->formType == FormType::Container)
            objectData.CurrentInventory = pObject->GetInventory();

        request.Objects.push_back(objectData);
    }

    m_transport.Send(request);
}

void ObjectService::OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept
{
    for (const ObjectData& objectData : acMessage.Objects)
    {
        const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(objectData.Id);
        TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
        if (!pObject)
        {
            spdlog::error("Object not found for form id {:X} (server mod {:X}, base {:X})", cObjectId, objectData.Id.ModId, objectData.Id.BaseId);
            continue;
        }

        CreateObjectEntity(pObject->formID, objectData.ServerId);

        if (!objectData.IsSenderFirst && objectData.CurrentLockData != LockData{})
        {
            Lock* pLock = pObject->GetLock();
            if (!pLock)
                pLock = pObject->CreateLock();
            if (pLock)
            {
                pLock->lockLevel = objectData.CurrentLockData.LockLevel;
                pLock->SetLock(objectData.CurrentLockData.IsLocked);
                pObject->LockChange();
            }
        }

        // A door used in another game while this one was away is set to the state the server last passed on.
        // It comes even when this game is the first in the cell: the door may have been used from the next cell over.
        // Setting it is not an activation, so nothing is sent back.
        const auto cShared = static_cast<TESObjectREFR::OpenState>(objectData.DoorOpenState);
        if (cShared != TESObjectREFR::kNone && pObject->baseForm->formType == FormType::Door && !pObject->extraData.Contains(ExtraDataType::Teleport))
        {
            const bool cOpen = cShared == TESObjectREFR::kOpen;
            const TESObjectREFR::OpenState cLocal = pObject->GetOpenState();
            const Lock* pLock = pObject->GetLock();
            // A door locked here stays shut: opening it would unlock it, as the game's SetOpen does.
            const bool cLockedHere = pLock && pLock->IsLocked();
            if (cLocal == TESObjectREFR::kNone || cLocal == cShared || cLocal == (cOpen ? TESObjectREFR::kOpening : TESObjectREFR::kClosing) || (cOpen && cLockedHere))
            {
                spdlog::info("Door {:X}: {} in the other games when this game came in; {} here (state {})", pObject->formID, cOpen ? "open" : "closed",
                             cLocal == TESObjectREFR::kNone ? "no state known" : cOpen && cLockedHere ? "left shut, it is locked" : "already so", static_cast<int>(cLocal));
            }
            else
            {
                pObject->SetOpen(cOpen);
                spdlog::info("Door {:X}: {} in the other games when this game came in; set {} here from state {}", pObject->formID, cOpen ? "open" : "closed",
                             cOpen ? "open" : "closed", static_cast<int>(cLocal));
            }
        }

        if (objectData.IsSenderFirst)
            continue;

        if (pObject->baseForm->formType == FormType::Container)
        {
            Inventory currentInventory = pObject->GetInventory();

            if (currentInventory.ContainsQuestItems())
                pObject->SetInventoryRetainingQuestItems(currentInventory, objectData.CurrentInventory);
            else
                pObject->SetInventory(objectData.CurrentInventory);
        }
    }
}

entt::entity ObjectService::CreateObjectEntity(const uint32_t acFormId, const uint32_t acServerId) noexcept
{
    const auto view = m_world.view<FormIdComponent, ObjectComponent>();

    auto it = std::find_if(view.begin(), view.end(), [acServerId, view](entt::entity entity) { return view.get<ObjectComponent>(entity).Id == acServerId; });

    if (it != view.end())
        return *it;

    entt::entity entity = m_world.create();
    spdlog::debug("Created object entity, server id: {:X}, form id {:X}", acServerId, acFormId);

    m_world.emplace<FormIdComponent>(entity, acFormId);
    m_world.emplace<ObjectComponent>(entity, acServerId);

    return entity;
}

void ObjectService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (acEvent.ActivateFlag)
    {
        acEvent.pObject->Activate(acEvent.pActivator, acEvent.Unk1, acEvent.pObjectToGet, acEvent.Count, acEvent.DefaultProcessing);
    }

    if (!m_transport.IsConnected())
        return;

    // A locked object opens the lockpicking menu, not itself, so its activation is not sent.
    // Only the locked bit counts: a leveled lock keeps its flag once unlocked, and that door's openings were never sent.
    if (Lock* pLock = acEvent.pObject->GetLock())
    {
        if (pLock->IsLocked())
            return;
    }

    ActivateRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.pObject->formID, request.Id))
    {
        spdlog::error("Server form id not found for object form id {:X}", acEvent.pObject->formID);
        return;
    }

    TESObjectCELL* pCell = acEvent.pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", acEvent.pObject->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, request.CellId))
    {
        spdlog::error("Server cell id not found for cell form id {:X}", pCell->formID);
        return;
    }

    auto view = m_world.view<FormIdComponent>();
    const auto pEntity = std::find_if(std::begin(view), std::end(view), [id = acEvent.pActivator->formID, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (pEntity == std::end(view))
    {
        // spdlog::error("Activator entity not found for form id {:X}", acEvent.pActivator->formID);
        return;
    }

    // Only this game's own actors: a remote copy's activation replays its owner's, and sending it back repeats it there.
    if (!m_world.all_of<LocalComponent>(*pEntity))
    {
        spdlog::info("Activation of {:X} by {:X}, another game's actor, not sent: its owner reports its own", acEvent.pObject->formID, acEvent.pActivator->formID);
        return;
    }

    // A door whose activation a script blocks (BlockActivation) does not move here, so the other games must not open it either.
    // The script's own Activate with default processing only does move it, block or not, so that one is sent.
    const bool cDoor = acEvent.pObject->baseForm && acEvent.pObject->baseForm->formType == FormType::Door;
    if (cDoor && !acEvent.DefaultProcessing && acEvent.ActivationBlocked)
    {
        // A script can turn its door away again and again, so each door says so at most every 5 s.
        static Map<uint32_t, std::chrono::steady_clock::time_point> s_lastBlockedLog;
        const auto cNow = std::chrono::steady_clock::now();
        auto& last = s_lastBlockedLog[acEvent.pObject->formID];
        if (cNow - last >= std::chrono::seconds(5))
        {
            last = cNow;
            spdlog::info("Door {:X} activated by {:X}, but a script blocks its activation; not sent", acEvent.pObject->formID, acEvent.pActivator->formID);
        }
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*pEntity);
    if (!serverIdRes.has_value())
        return;

    request.ActivatorId = serverIdRes.value();
    request.PreActivationOpenState = acEvent.PreActivationOpenState;

    m_transport.Send(request);

    if (acEvent.pObject->baseForm && acEvent.pObject->baseForm->formType == FormType::Door)
        spdlog::info("Door {:X} ({:X}:{:X}) activated by {:X} from state {}; sent to the players in cell {:X}:{:X}", acEvent.pObject->formID, request.Id.ModId, request.Id.BaseId,
                     acEvent.pActivator->formID, static_cast<int>(acEvent.PreActivationOpenState), request.CellId.ModId, request.CellId.BaseId);
}

void ObjectService::OnActivateNotify(const NotifyActivate& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ActivatorId);

    // An activation by one of this game's actors is our own coming back from another game.
    // Replaying it would do it twice, putting the player back through a load door.
    if (const auto entity = Utils::FindEntityByServerId(acMessage.ActivatorId); pActor && (pActor == PlayerCharacter::Get() || (entity && m_world.all_of<LocalComponent>(*entity))))
    {
        spdlog::warn("Activation of {:X}:{:X} by {:X}, an actor of this game, came back from another game; not done again", acMessage.Id.ModId, acMessage.Id.BaseId, pActor->formID);
        return;
    }

    const uint32_t cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    TESObjectREFR* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("Failed to retrieve object to activate: {:X}:{:X} (form {:X} here), activated by server id {:X}", acMessage.Id.ModId, acMessage.Id.BaseId, cObjectId,
                      acMessage.ActivatorId);
        return;
    }

    // Only an object in a cell this game has loaded is used here: a script's use of a far or indoor reference must not run its scripts here.
    // A door's state still comes when this game enters its cell, with the cell's objects.
    const TESObjectCELL* pCell = pObject->GetParentCellEx();
    if (!pCell || !pCell->IsAttached())
    {
        spdlog::info("Activation of {:X}, activated by server id {:X}: its cell is not loaded here; not done", pObject->formID, acMessage.ActivatorId);
        return;
    }

    // A door that does not lead elsewhere is set to the state it was left in over there, not used again here.
    // It needs no actor, so a door opened by an NPC this game has no copy of opens too.
    // Setting it is not an activation here, so nothing is sent back and two games cannot pass it to and fro.
    const auto cRemoteBefore = static_cast<TESObjectREFR::OpenState>(acMessage.PreActivationOpenState);
    const auto cRemoteAfter = static_cast<TESObjectREFR::OpenState>(DoorState::AfterActivation(cRemoteBefore));
    if (pObject->baseForm->formType == FormType::Door && !pObject->extraData.Contains(ExtraDataType::Teleport) && cRemoteAfter != TESObjectREFR::kNone)
    {
        const bool cOpen = cRemoteAfter == TESObjectREFR::kOpen;
        const TESObjectREFR::OpenState cLocal = pObject->GetOpenState();
        const Lock* pLock = pObject->GetLock();
        const bool cLockedHere = pLock && pLock->IsLocked();
        const std::string cBy = pActor ? fmt::format("{:X}", pActor->formID) : fmt::format("server id {:X}, not here", acMessage.ActivatorId);

        // Already there, or on its way.
        if (cLocal == cRemoteAfter || cLocal == (cOpen ? TESObjectREFR::kOpening : TESObjectREFR::kClosing))
        {
            spdlog::info("Door {:X}: {} in another game (by {}); already {} here", pObject->formID, cOpen ? "opened" : "closed", cBy, static_cast<int>(cLocal));
            return;
        }

        // Opening a door locked here unlocks it too (the game's SetOpen does): it was unlocked over there, since a locked door's use is not sent.
        pObject->SetOpen(cOpen);
        spdlog::info("Door {:X}: {} in another game (by {}, from state {}); set {} here from state {}{}", pObject->formID, cOpen ? "opened" : "closed", cBy,
                     static_cast<int>(cRemoteBefore), cOpen ? "open" : "closed", static_cast<int>(cLocal), cLockedHere && cOpen ? ", and unlocked: it was locked here" : "");
        return;
    }

    if (!pActor)
    {
        spdlog::error("{}: could not find actor server id {:X} (object {:X})", __FUNCTION__, acMessage.ActivatorId, pObject->formID);
        return;
    }

    if (pObject->baseForm->formType == FormType::Door)
    {
        auto remotePreActivationState = static_cast<TESObjectREFR::OpenState>(acMessage.PreActivationOpenState);
        TESObjectREFR::OpenState localState = pObject->GetOpenState();
        const Lock* pLock = pObject->GetLock();
        const bool cLockedHere = pLock && pLock->IsLocked();

        if (remotePreActivationState != localState)
        {
            // The doors are unsynced at this point. If we'll Activate the one on our side
            // it'll just continue to be unsynced (open remotely, closed locally and vice versa)
            spdlog::info("Door {:X}: {:X} activated it in another game from state {}, here it is in state {}{}; left as it is", pObject->formID, pActor->formID,
                         static_cast<int>(remotePreActivationState), static_cast<int>(localState), cLockedHere ? " and locked" : "");
            return;
        }

        // Activated by the other player's copy, which has no key: a door locked here stays shut.
        spdlog::info("Door {:X}: {:X} activated it in another game from state {}; done here too{}", pObject->formID, pActor->formID,
                     static_cast<int>(remotePreActivationState), cLockedHere ? ", but it is locked here and will not open" : "");
    }

    // unsure if these flags are the best, but these are passed with the papyrus Activate fn
    // might be an idea to have the client send the flags through NotifyActivate
    pObject->Activate(pActor, 0, nullptr, 1, 0);
}

void ObjectService::OnLockChange(const LockChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    LockChangeRequest request;

    if (!m_world.GetModSystem().GetServerModId(acEvent.FormId, request.Id))
    {
        spdlog::error("Server form id for lock object not found, form id: {:X}", acEvent.FormId);
        return;
    }

    const auto* const pObject = Cast<TESObjectREFR>(TESForm::GetById(acEvent.FormId));

    TESObjectCELL* pCell = pObject->GetParentCellEx();
    if (!pCell)
    {
        spdlog::error("Activated object has no parent cell: {:X}", pObject->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, request.CellId))
    {
        spdlog::error("Server cell id for cell not found, cell form id: {:X}", pCell->formID);
        return;
    }

    request.IsLocked = acEvent.IsLocked;
    request.LockLevel = acEvent.LockLevel;

    m_transport.Send(request);
}

void ObjectService::OnLockChangeNotify(const NotifyLockChange& acMessage) noexcept
{
    const auto cObjectId = World::Get().GetModSystem().GetGameId(acMessage.Id);
    if (cObjectId == 0)
    {
        spdlog::error("Failed to retrieve object id to (un)lock.");
        return;
    }

    auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cObjectId));
    if (!pObject)
    {
        spdlog::error("Failed to retrieve object to (un)lock.");
        return;
    }

    auto* pLock = pObject->GetLock();

    if(!acMessage.IsLocked)
    {
        if (!pLock || !pLock->IsLocked())
            return;
    }

    if (!pLock && acMessage.IsLocked)
    {
        pLock = pObject->CreateLock();
        if (!pLock)
        {
            spdlog::error("Failed to create lock for object form id {:X}", pObject->formID);
            return;
        }
    }

    pLock->lockLevel = acMessage.LockLevel;
    pLock->SetLock(acMessage.IsLocked);
    pObject->LockChange();
}

void ObjectService::OnScriptAnimationEvent(const ScriptAnimationEvent& acEvent) noexcept
{
    ScriptAnimationRequest request{};
    request.FormID = acEvent.FormID;
    request.Animation = acEvent.Animation;
    request.EventName = acEvent.EventName;

    m_transport.Send(request);
}

void ObjectService::OnNotifyScriptAnimation(const NotifyScriptAnimation& acMessage) noexcept
{
    if (acMessage.FormID == 0)
        return;

    auto* pForm = TESForm::GetById(acMessage.FormID);
    auto* pObject = Cast<TESObjectREFR>(pForm);

    if (!pObject)
    {
        spdlog::error("Failed to fetch notify script animation object, form id: {:X}", acMessage.FormID);
        return;
    }

    BSFixedString eventName(acMessage.EventName.c_str());
    if (acMessage.Animation == String{})
    {
        pObject->PlayAnimation(&eventName);
    }
    else
    {
        BSFixedString animation(acMessage.Animation.c_str());
        pObject->PlayAnimationAndWait(&animation, &eventName);
    }
}

BSTEventResult ObjectService::OnEvent(const TESActivateEvent* acEvent, const EventDispatcher<TESActivateEvent>* aDispatcher)
{
#if ENVIRONMENT_DEBUG
    auto view = m_world.view<ObjectComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acEvent->object->formID, view](entt::entity entity) { return view.get<ObjectComponent>(entity).Id == id; });

    if (itor == std::end(view))
    {
        AddObjectComponent(acEvent->object);
    }
#endif

    return BSTEventResult::kOk;
}

namespace
{
// The kinds of reference a quest's scripts switch for the whole party: markers and other statics, activators (a siege's
// catapults), containers, doors, trees, plants and furniture.
// Actors, items, lights and temporary references are synced their own way, or not at all.
// SkyrimTogether.esp's references, the players' map markers, are each game's own.
// The same form in another game marks someone else, and moving one switches it off and on.
bool IsOwnPluginReference(uint32_t aFormId) noexcept
{
    static const Mod* s_pPlugin = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!s_pPlugin || !s_pPlugin->IsLoaded())
        return false;
    if (s_pPlugin->IsLite())
        return (aFormId >> 24) == 0xFE && ((aFormId >> 12) & 0xFFF) == s_pPlugin->liteId;
    return (aFormId >> 24) == s_pPlugin->standardId;
}

bool IsSharedSwitchKind(const TESObjectREFR* apRef) noexcept
{
    if (!apRef || !apRef->baseForm || apRef->formID >= 0xFF000000 || IsOwnPluginReference(apRef->formID))
        return false;
    switch (static_cast<uint8_t>(apRef->baseForm->formType))
    {
    case 24: // activator
    case 25: // talking activator
    case 28: // container
    case 29: // door
    case 34: // static, which the markers a quest's references hang from are
    case 36: // movable static
    case 38: // tree
    case 39: // flora
    case 40: // furniture
        return true;
    default: return false;
    }
}

// At most one line per kind every 10 s, with how many switches went by since the last one.
bool LogDue(std::chrono::steady_clock::time_point& aNext) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < aNext)
        return false;
    aNext = cNow + std::chrono::seconds(10);
    return true;
}
} // namespace

void ObjectService::OnReferenceEnableEvent(const ReferenceEnableEvent& acEvent) noexcept
{
    // Only the leader's game runs the party's quests (a member cannot start a siege), so only its switches are shared.
    // A member's own switches, including the ones it applies from the leader, stay its own.
    if (!m_transport.IsConnected() || !m_world.GetPartyService().IsLeader())
        return;
    if (IsSharedSwitchKind(Cast<TESObjectREFR>(TESForm::GetById(acEvent.FormId))))
        m_enableChanges.emplace_back(acEvent.FormId, acEvent.Enabled);
}

bool ObjectService::ApplyReferenceEnable(uint32_t aFormId, bool aEnabled) noexcept
{
    auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(aFormId));
    if (!pRef)
        return false;
    // The leader sends only these kinds; another plugin at the same id here is left alone.
    if (!IsSharedSwitchKind(pRef) || pRef->IsDeleted())
        return true;
    if (aEnabled && pRef->IsDisabled())
        pRef->EnableImpl();
    else if (!aEnabled && !pRef->IsDisabled())
        pRef->Disable();
    return true;
}

void ObjectService::OnNotifyReferenceEnable(const NotifyReferenceEnable& acMessage) noexcept
{
    size_t applied = 0;
    for (const auto& change : acMessage.Changes)
    {
        const uint32_t cFormId = m_world.GetModSystem().GetGameId(change.Id);
        if (!cFormId)
            continue;
        if (ApplyReferenceEnable(cFormId, change.Enabled))
        {
            ++applied;
            m_enableWaiting.erase(cFormId);
        }
        else
            m_enableWaiting[cFormId] = change.Enabled;
    }

    static std::chrono::steady_clock::time_point s_nextLog{};
    static size_t s_since = 0;
    s_since += acMessage.Changes.size();
    if (LogDue(s_nextLog))
    {
        spdlog::info("ReferenceEnable: {} reference(s) the leader's scripts switched (first {:X}:{:X} {}), {} applied here now, {} waiting for their cell", s_since,
                     acMessage.Changes.empty() ? 0 : acMessage.Changes[0].Id.ModId, acMessage.Changes.empty() ? 0 : acMessage.Changes[0].Id.BaseId,
                     !acMessage.Changes.empty() && acMessage.Changes[0].Enabled ? "on" : "off", applied, m_enableWaiting.size());
        s_since = 0;
    }
}

void ObjectService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_enableChanges.empty())
    {
        RequestReferenceEnable request{};
        for (const auto& [formId, enabled] : m_enableChanges)
        {
            ReferenceEnableState state{};
            if (m_world.GetModSystem().GetServerModId(formId, state.Id.ModId, state.Id.BaseId))
            {
                state.Enabled = enabled;
                request.Changes.push_back(state);
            }
        }
        m_enableChanges.clear();

        if (!request.Changes.empty())
        {
            static std::chrono::steady_clock::time_point s_nextLog{};
            static size_t s_since = 0;
            s_since += request.Changes.size();
            if (LogDue(s_nextLog))
            {
                spdlog::info("ReferenceEnable: {} reference(s) switched by this game's scripts sent to the party (first {:X}:{:X} {})", s_since, request.Changes[0].Id.ModId,
                             request.Changes[0].Id.BaseId, request.Changes[0].Enabled ? "on" : "off");
                s_since = 0;
            }
            m_transport.Send(request);
        }
    }

    // References whose cell was not loaded when the leader's switch came are tried again every 2 s.
    if (m_enableWaiting.empty())
        return;
    m_enableRetry -= acEvent.Delta;
    if (m_enableRetry > 0.0)
        return;
    m_enableRetry = 2.0;
    for (auto it = m_enableWaiting.begin(); it != m_enableWaiting.end();)
        it = ApplyReferenceEnable(it->first, it->second) ? m_enableWaiting.erase(it) : std::next(it);
}
