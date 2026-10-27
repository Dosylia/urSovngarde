#include <Services/CharacterService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/CharacterSpawnedEvent.h>
#include <Events/CharacterExteriorCellChangeEvent.h>
#include <Events/CharacterInteriorCellChangeEvent.h>
#include <Events/PlayerEnterWorldEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/CharacterRemoveEvent.h>
#include <Events/OwnershipTransferEvent.h>

#include <Game/OwnerView.h>

#include <Messages/ClashRequest.h>
#include <Messages/NotifyClash.h>
#include <Messages/ArrowNockedRequest.h>
#include <Messages/NotifyArrowNocked.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/RequestOwnershipTransfer.h>
#include <Messages/NotifyOwnershipTransfer.h>
#include <Messages/RequestOwnershipClaim.h>
#include <Messages/MountRequest.h>
#include <Messages/NotifyMount.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/DialogueRequest.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyActorTeleport.h>
#include <Messages/RequestActionEvents.h>
#include <Messages/NotifyActionEvents.h>
#include <MovementPacker.h>

#include <Setting.h>
namespace
{
Console::Setting bEnableXpSync{"Gameplay:bEnableXpSync", "Syncs combat XP within the party", true};
}

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this))
    , m_interiorCellChangeEventConnection(aDispatcher.sink<CharacterInteriorCellChangeEvent>().connect<&CharacterService::OnCharacterInteriorCellChange>(this))
    , m_exteriorCellChangeEventConnection(aDispatcher.sink<CharacterExteriorCellChangeEvent>().connect<&CharacterService::OnCharacterExteriorCellChange>(this))
    , m_characterAssignRequestConnection(aDispatcher.sink<PacketEvent<AssignCharacterRequest>>().connect<&CharacterService::OnAssignCharacterRequest>(this))
    , m_transferOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipTransfer>>().connect<&CharacterService::OnOwnershipTransferRequest>(this))
    , m_ownershipTransferEventConnection(aDispatcher.sink<OwnershipTransferEvent>().connect<&CharacterService::OnOwnershipTransferEvent>(this))
    , m_claimOwnershipConnection(aDispatcher.sink<PacketEvent<RequestOwnershipClaim>>().connect<&CharacterService::OnOwnershipClaimRequest>(this))
    , m_removeCharacterConnection(aDispatcher.sink<CharacterRemoveEvent>().connect<&CharacterService::OnCharacterRemoveEvent>(this))
    , m_characterSpawnedConnection(aDispatcher.sink<CharacterSpawnedEvent>().connect<&CharacterService::OnCharacterSpawned>(this))
    , m_referenceMovementSnapshotConnection(aDispatcher.sink<PacketEvent<ClientReferencesMoveRequest>>().connect<&CharacterService::OnReferencesMoveRequest>(this))
    , m_factionsChangesConnection(aDispatcher.sink<PacketEvent<RequestFactionsChanges>>().connect<&CharacterService::OnFactionsChanges>(this))
    , m_mountConnection(aDispatcher.sink<PacketEvent<MountRequest>>().connect<&CharacterService::OnMountRequest>(this))
    , m_newPackageConnection(aDispatcher.sink<PacketEvent<NewPackageRequest>>().connect<&CharacterService::OnNewPackageRequest>(this))
    , m_requestRespawnConnection(aDispatcher.sink<PacketEvent<RequestRespawn>>().connect<&CharacterService::OnRequestRespawn>(this))
    , m_clashConnection(aDispatcher.sink<PacketEvent<ClashRequest>>().connect<&CharacterService::OnClashRequest>(this))
    , m_arrowNockedConnection(aDispatcher.sink<PacketEvent<ArrowNockedRequest>>().connect<&CharacterService::OnArrowNockedRequest>(this))
    , m_syncExperienceConnection(aDispatcher.sink<PacketEvent<SyncExperienceRequest>>().connect<&CharacterService::OnSyncExperienceRequest>(this))
    , m_dialogueConnection(aDispatcher.sink<PacketEvent<DialogueRequest>>().connect<&CharacterService::OnDialogueRequest>(this))
    , m_subtitleConnection(aDispatcher.sink<PacketEvent<SubtitleRequest>>().connect<&CharacterService::OnSubtitleRequest>(this))
    , m_actionEventsConnection(aDispatcher.sink<PacketEvent<RequestActionEvents>>().connect<&CharacterService::OnActionEventsRequest>(this))
{
}

void CharacterService::Serialize(World& aRegistry, entt::entity aEntity, CharacterSpawnRequest* apSpawnRequest) noexcept
{
    const auto& characterComponent = aRegistry.get<CharacterComponent>(aEntity);

    apSpawnRequest->ServerId = World::ToInteger(aEntity);
    apSpawnRequest->AppearanceBuffer = characterComponent.SaveBuffer;
    apSpawnRequest->ChangeFlags = characterComponent.ChangeFlags;
    apSpawnRequest->FaceTints = characterComponent.FaceTints;
    apSpawnRequest->FactionsContent = characterComponent.FactionsContent;
    apSpawnRequest->IsDead = characterComponent.IsDead();
    apSpawnRequest->IsPlayer = characterComponent.IsPlayer();
    apSpawnRequest->IsWeaponDrawn = characterComponent.IsWeaponDrawn();
    apSpawnRequest->IsPlayerSummon = characterComponent.IsPlayerSummon();
    apSpawnRequest->IsDragon = characterComponent.IsDragon();
    apSpawnRequest->PlayerId = characterComponent.PlayerId;

    const auto* pOwnerComponent = aRegistry.try_get<OwnerComponent>(aEntity);
    if (pOwnerComponent)
    {
        apSpawnRequest->OwnershipEpoch = pOwnerComponent->OwnershipEpoch;
    }

    const auto* pFormIdComponent = aRegistry.try_get<FormIdComponent>(aEntity);
    if (pFormIdComponent)
    {
        apSpawnRequest->FormId = pFormIdComponent->Id;
    }

    const auto* pInventoryComponent = aRegistry.try_get<InventoryComponent>(aEntity);
    if (pInventoryComponent)
    {
        apSpawnRequest->InventoryContent = pInventoryComponent->Content;
    }

    const auto* pActorValuesComponent = aRegistry.try_get<ActorValuesComponent>(aEntity);
    if (pActorValuesComponent)
    {
        apSpawnRequest->InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    if (characterComponent.BaseId)
    {
        apSpawnRequest->BaseId = characterComponent.BaseId.Id;
    }

    if (characterComponent.LeveledNpcPickId)
    {
        apSpawnRequest->LeveledNpcPickId = characterComponent.LeveledNpcPickId.Id;
    }

    const auto* pMovementComponent = aRegistry.try_get<MovementComponent>(aEntity);
    if (pMovementComponent)
    {
        apSpawnRequest->Position = pMovementComponent->Position;
        apSpawnRequest->Rotation.x = pMovementComponent->Rotation.x;
        apSpawnRequest->Rotation.y = pMovementComponent->Rotation.z;
    }

    const auto* pCellIdComponent = aRegistry.try_get<CellIdComponent>(aEntity);
    if (pCellIdComponent)
    {
        apSpawnRequest->CellId = pCellIdComponent->Cell;
    }

    auto& animationComponent = aRegistry.get<AnimationComponent>(aEntity);
    apSpawnRequest->ActionsToReplay = animationComponent.ActionsReplayCache.FormRefinedReplayChain();
}

namespace
{
// Every two seconds, an actor whose owner is out of range or silent goes to a player who is in range.
// Silent means no movement snapshot for cSilence, as in a paused game; a silent player is never a candidate.
// Two sweeps in a row are needed, so a player crossing a cell edge does not bounce actors back and forth.
// Players' characters, summons and dead actors stay where they are.

// Longer than a loading screen (8 to 10 s), so actors are not handed away at every door.
constexpr auto cSilence = 15s;

bool IsSilent(const Player* apPlayer, const std::chrono::steady_clock::time_point aNow) noexcept
{
    return apPlayer->GetLastMovementAt() != std::chrono::steady_clock::time_point{} && aNow - apPlayer->GetLastMovementAt() > cSilence;
}
} // namespace

void CharacterService::HandOffAbandonedActors() const noexcept
{
    static std::chrono::steady_clock::time_point s_nextSweep;
    static TiltedPhoques::Map<uint32_t, uint32_t> s_outOfRangeSweeps; // entity -> consecutive sweeps out of range

    const auto now = std::chrono::steady_clock::now();
    if (now < s_nextSweep)
        return;
    s_nextSweep = now + 2s;

    const auto isSilent = [now](const Player* apPlayer) { return IsSilent(apPlayer, now); };

    TiltedPhoques::Set<uint32_t> playerCharacters;
    for (auto pPlayer : m_world.GetPlayerManager())
        if (pPlayer->GetCharacter())
            playerCharacters.insert(World::ToInteger(*pPlayer->GetCharacter()));

    auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    for (auto entity : view)
    {
        const uint32_t cId = World::ToInteger(entity);
        auto& ownerComponent = view.get<OwnerComponent>(entity);
        auto& characterComponent = view.get<CharacterComponent>(entity);
        auto& cellIdComponent = view.get<CellIdComponent>(entity);

        Player* pOwner = ownerComponent.GetOwner();
        if (!pOwner || playerCharacters.count(cId) || characterComponent.IsPlayerSummon() || characterComponent.IsDead())
        {
            s_outOfRangeSweeps.erase(cId);
            continue;
        }

        const bool ownerSilent = isSilent(pOwner);
        if (!ownerSilent && pOwner->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
        {
            s_outOfRangeSweeps.erase(cId);
            continue;
        }

        Player* pCandidate = nullptr;
        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == pOwner || isSilent(pPlayer) || !pPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
                continue;
            if (std::find(ownerComponent.InvalidOwners.begin(), ownerComponent.InvalidOwners.end(), pPlayer) != ownerComponent.InvalidOwners.end())
                continue;
            pCandidate = pPlayer;
            break;
        }

        if (!pCandidate)
        {
            s_outOfRangeSweeps.erase(cId);
            continue;
        }

        if (++s_outOfRangeSweeps[cId] < 2)
            continue;
        s_outOfRangeSweeps.erase(cId);

        const auto& ownerCell = pOwner->GetCellComponent().CenterCoords;
        spdlog::info("Handoff: actor {:X} at grid ({}, {}) from player {:X} at ({}, {}), {}, to player {:X} who is in range", cId, cellIdComponent.CenterCoords.X,
                     cellIdComponent.CenterCoords.Y, pOwner->GetConnectionId(), ownerCell.X, ownerCell.Y, ownerSilent ? "silent (paused or loading)" : "out of its range",
                     pCandidate->GetConnectionId());

        // TransferOwnership bumps the epoch, so a late update from the former owner arrives stale.
        ownerComponent.InvalidOwners.clear();
        TransferOwnership(pCandidate, entity, OwnershipTransferReason::OwnerUnavailable);
    }
}

void CharacterService::OnUpdate(const UpdateEvent&) const noexcept
{
    ProcessFactionsChanges();
    HandOffAbandonedActors();
    ProcessPendingLeaderAssignments();
}

bool CharacterService::HoldUntilSettled(Player* apLeader, const entt::entity aEntity, const uint32_t aOwnershipEpoch, const OwnershipTransferReason aReason) const noexcept
{
    // Right after the leader's arrival (a load), its game is still bringing actors in.
    // Handed over at once, they go back and forth between the games, so the hand-over waits until the arrival is 10 s old.
    constexpr auto cGrace = std::chrono::seconds(10);
    const auto cReadyAt = apLeader->GetLastCellEnterAt() + cGrace;
    if (std::chrono::steady_clock::now() >= cReadyAt)
        return false;

    m_pendingLeaderAssignments.push_back(PendingLeaderAssignment{apLeader->GetId(), aEntity, aOwnershipEpoch, cReadyAt, aReason});
    spdlog::info("Ownership: {} of actor {:X} by player {:X} held back until 10 s after their arrival", GetOwnershipTransferReasonName(aReason), World::ToInteger(aEntity),
                 apLeader->GetId());
    return true;
}

void CharacterService::ProcessPendingLeaderAssignments() const noexcept
{
    if (m_pendingLeaderAssignments.empty())
        return;

    const auto cNow = std::chrono::steady_clock::now();
    for (auto it = m_pendingLeaderAssignments.begin(); it != m_pendingLeaderAssignments.end();)
    {
        if (cNow < it->ReadyAt)
        {
            ++it;
            continue;
        }

        const PendingLeaderAssignment cPending = *it;
        it = m_pendingLeaderAssignments.erase(it);

        Player* const pPlayer = m_world.GetPlayerManager().GetById(cPending.PlayerId);
        if (!pPlayer || !m_world.valid(cPending.Entity))
            continue;
        // The rules are checked again: another arrival, a hand-over or a departure may have come meanwhile.
        if (CanClaimOwnership(pPlayer, cPending.Entity, cPending.OwnershipEpoch, cPending.Reason))
            TransferOwnership(pPlayer, cPending.Entity, cPending.Reason);
    }
}

void CharacterService::OnCharacterExteriorCellChange(const CharacterExteriorCellChangeEvent& acEvent) const noexcept
{
    // The character's own place is set now, before it is sent: its first actions there may come before its movement, and go by it.
    if (auto* pCell = m_world.try_get<CellIdComponent>(acEvent.Entity))
        *pCell = CellIdComponent{acEvent.CellId, acEvent.WorldSpaceId, acEvent.CurrentCoords};

    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        const auto& viewer = pPlayer->GetCellComponent();
        const bool sameWorld = viewer.WorldSpaceId == acEvent.WorldSpaceId;
        const bool inGrid = GridCellCoords::IsCellInGridCell(acEvent.CurrentCoords, viewer.CenterCoords, false);

        if (!sameWorld || !inGrid)
            pPlayer->Send(removeMessage);
        else
            pPlayer->Send(spawnMessage);
    }
}

void CharacterService::OnCharacterInteriorCellChange(const CharacterInteriorCellChangeEvent& acEvent) const noexcept
{
    // The character's own place is set now, before it is sent: its first actions there may come before its movement, and go by it.
    if (auto* pCell = m_world.try_get<CellIdComponent>(acEvent.Entity))
        *pCell = CellIdComponent{acEvent.NewCell};

    CharacterSpawnRequest spawnMessage;
    Serialize(m_world, acEvent.Entity, &spawnMessage);

    NotifyRemoveCharacter removeMessage;
    removeMessage.ServerId = World::ToInteger(acEvent.Entity);

    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (acEvent.Owner == pPlayer)
            continue;

        if (acEvent.NewCell == pPlayer->GetCellComponent().Cell)
            pPlayer->Send(spawnMessage);
        else
            pPlayer->Send(removeMessage);
    }
}

void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    const auto& refId = message.ReferenceId;

    const auto isPlayer = (refId.ModId == 0 && refId.BaseId == 0x14);
    const auto isCustom = isPlayer || refId.ModId == std::numeric_limits<uint32_t>::max();

    // Check if id is the player
    if (!isCustom)
    {
        // Look for the character
        auto view = m_world.view<FormIdComponent, ActorValuesComponent, CharacterComponent, MovementComponent, CellIdComponent, OwnerComponent, InventoryComponent>();

        const auto itor = std::find_if(
            std::begin(view), std::end(view),
            [view, refId](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);

                return formIdComponent.Id == refId;
            });

        if (itor != std::end(view))
        {
            spdlog::debug("FormId: {:x}:{:x} is already managed", refId.ModId, refId.BaseId);

            auto& ownerComponent = view.get<OwnerComponent>(*itor);
            auto& characterComponent = view.get<CharacterComponent>(*itor);
            const bool isOwner = ownerComponent.GetOwner() == acMessage.pPlayer;
            const bool transferToLeader = !isOwner && CanClaimOwnership(acMessage.pPlayer, *itor, ownerComponent.OwnershipEpoch, OwnershipTransferReason::LeaderAssignment);

            if (!characterComponent.LeveledNpcPickId && message.LeveledNpcPickId != GameId{})
            {
                characterComponent.LeveledNpcPickId = FormIdComponent(message.LeveledNpcPickId);
                spdlog::debug(
                    "Stored previously unknown leveled NPC pick {:x}:{:x} for FormId {:x}:{:x}",
                    message.LeveledNpcPickId.ModId,
                    message.LeveledNpcPickId.BaseId,
                    refId.ModId,
                    refId.BaseId);
            }

            AssignCharacterResponse response{};
            response.Cookie = message.Cookie;
            response.Owner = isOwner;
            PopulateAssignmentResponse(*itor, response);
            // Empty for players and for actors whose owner sent none; see the client's base form check.
            response.BaseId = characterComponent.BaseId.Id;
            acMessage.pPlayer->Send(response);

            // The assignment response establishes a remote component before the grant arrives.
            if (transferToLeader && !HoldUntilSettled(acMessage.pPlayer, *itor, ownerComponent.OwnershipEpoch, OwnershipTransferReason::LeaderAssignment))
                TransferOwnership(acMessage.pPlayer, *itor, OwnershipTransferReason::LeaderAssignment);

            return;
        }
    }

    // This entity has no owner create it
    CreateCharacter(acMessage);
}

void CharacterService::OnOwnershipTransferRequest(const PacketEvent<RequestOwnershipTransfer>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent, MovementComponent>();
    const auto it = view.find(cEntity);
    if (it == view.end())
    {
        spdlog::debug("Ignored ownership release from player {:X} for missing actor {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer || ownerComponent.OwnershipEpoch != message.OwnershipEpoch)
    {
        const uint32_t ownerId = ownerComponent.GetOwner() ? ownerComponent.GetOwner()->GetId() : 0;
        spdlog::debug(
            "Ignored ownership release from player {:X} for actor {:X}; current owner is {:X} and requested epoch {} does not match {}",
            acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, ownerComponent.OwnershipEpoch);
        return;
    }

    if (message.Reason != OwnershipReleaseReason::Relinquish && message.Reason != OwnershipReleaseReason::DeclineGrant)
    {
        spdlog::warn("Ignored ownership release with invalid reason from player {:X} for actor {:X}", acMessage.pPlayer->GetId(), message.ServerId);
        return;
    }

    auto& characterComponent = view.get<CharacterComponent>(*it);
    if (characterComponent.IsPlayerSummon())
    {
        spdlog::info("Removing summon {:X} after player {:X} relinquished ownership", message.ServerId, acMessage.pPlayer->GetId());
        m_world.GetDispatcher().trigger(CharacterRemoveEvent(message.ServerId));
        return;
    }

    if (message.Reason == OwnershipReleaseReason::Relinquish && (message.WorldSpaceId || message.CellId))
    {
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
        if (pFormIdComponent)
        {
            NotifyActorTeleport notify{};
            notify.FormId = pFormIdComponent->Id;
            notify.WorldSpaceId = message.WorldSpaceId;
            notify.CellId = message.CellId;
            notify.Position = message.Position;

            GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
        }

        auto& cellIdComponent = view.get<CellIdComponent>(*it);
        cellIdComponent.WorldSpaceId = message.WorldSpaceId;
        cellIdComponent.Cell = message.CellId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(message.Position);

        auto& movementComponent = view.get<MovementComponent>(*it);
        movementComponent.Position = message.Position;
    }

    // A normal release starts a fresh search; a declined grant keeps the failed candidates so ownership cannot bounce.
    if (message.Reason == OwnershipReleaseReason::Relinquish)
        ownerComponent.InvalidOwners.clear();

    ownerComponent.InvalidOwners.push_back(acMessage.pPlayer);

    TransferToNextOwner(cEntity, OwnershipTransferReason::Relinquish);
}

void CharacterService::OnOwnershipTransferEvent(const OwnershipTransferEvent& acEvent) const noexcept
{
    // A disconnect starts a fresh search; previously unavailable clients may be ready now.
    const auto view = m_world.view<OwnerComponent>();
    if (const auto it = view.find(acEvent.Entity); it != view.end())
        view.get<OwnerComponent>(*it).InvalidOwners.clear();

    TransferToNextOwner(acEvent.Entity, OwnershipTransferReason::OwnerUnavailable);
}

void CharacterService::OnCharacterRemoveEvent(const CharacterRemoveEvent& acEvent) const noexcept
{
    const auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(static_cast<entt::entity>(acEvent.ServerId));
    if (it == view.end())
        return;

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterDestoy(*it);

    NotifyRemoveCharacter response;
    response.ServerId = acEvent.ServerId;

    for (auto pPlayer : m_world.GetPlayerManager())
        pPlayer->Send(response);

    m_world.destroy(*it);
    spdlog::debug("Character destroyed {:X}", acEvent.ServerId);
}

void CharacterService::OnOwnershipClaimRequest(const PacketEvent<RequestOwnershipClaim>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);

    // A killing blow's claim goes at once: the killer's game is waiting to kill it.
    if (message.KillingBlow)
    {
        if (CanClaimOwnership(acMessage.pPlayer, cEntity, message.ExpectedOwnershipEpoch, OwnershipTransferReason::KillingBlow))
            TransferOwnership(acMessage.pPlayer, cEntity, OwnershipTransferReason::KillingBlow);
        return;
    }

    if (!CanClaimOwnership(acMessage.pPlayer, cEntity, message.ExpectedOwnershipEpoch, OwnershipTransferReason::LeaderClaim, message.Follower))
        return;

    if (message.Follower)
        if (auto* pOwnerComponent = m_world.try_get<OwnerComponent>(cEntity))
            pOwnerComponent->FollowerOfPlayerId = acMessage.pPlayer->GetId();

    // The leader's game claims each NPC it loads, so after its arrival the claim waits like an assignment.
    // A follower and a dead body to drag go at once.
    const auto* pCharacter = m_world.try_get<CharacterComponent>(cEntity);
    const bool cLivingNpc = pCharacter && !pCharacter->IsDead();
    if (!message.Follower && cLivingNpc && HoldUntilSettled(acMessage.pPlayer, cEntity, message.ExpectedOwnershipEpoch, OwnershipTransferReason::LeaderClaim))
        return;
    TransferOwnership(acMessage.pPlayer, cEntity, OwnershipTransferReason::LeaderClaim);
}

void CharacterService::OnCharacterSpawned(const CharacterSpawnedEvent& acEvent) const noexcept
{
    CharacterSpawnRequest message;
    Serialize(m_world, acEvent.Entity, &message);

    const auto& ownerComp = m_world.get<OwnerComponent>(acEvent.Entity);
    if (!GameServer::Get()->SendToPlayersInRange(message, acEvent.Entity, ownerComp.GetOwner()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    GameServer::Get()->GetWorld().GetScriptService().HandleCharacterSpawn(acEvent.Entity);
}

void CharacterService::OnReferencesMoveRequest(const PacketEvent<ClientReferencesMoveRequest>& acMessage) const noexcept
{
    OwnerView<AnimationComponent, MovementComponent, CellIdComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    acMessage.pPlayer->MarkMovement();

    // Relayed at once to every other player in range, with the owner's own tick: each receiving game times its playback
    // from when the owner captured the update, not from when the server passed it on.
    using Packer = MovementPacker<ServerReferencesMoveRequest>;
    thread_local TiltedPhoques::Buffer s_scratch(1 << 16);
    TiltedPhoques::Map<Player*, std::unique_ptr<Packer>> relays;
    const auto characterView = m_world.view<CharacterComponent>();

    for (auto& entry : message.Updates)
    {
        const auto entity = static_cast<entt::entity>(entry.first);

        auto itor = view.find(entity);
        if (itor == std::end(view))
        {
            spdlog::debug("{:x} requested move of {:x} but does not exist", acMessage.pPlayer->GetConnectionId(), World::ToInteger(entity));
            continue;
        }

        auto& movementComponent = view.get<MovementComponent>(*itor);
        auto& cellIdComponent = view.get<CellIdComponent>(*itor);

        // Movement arrives unreliably, so an older report can come after a newer one, and would set the actor back.
        // It is dropped; a step back of more than a second is a clock resync and is taken.
        constexpr uint64_t cReorderWindowMs = 1000;
        if (message.Tick < movementComponent.Tick && movementComponent.Tick - message.Tick < cReorderWindowMs)
            continue;
        movementComponent.Tick = message.Tick;

        const auto movementCopy = movementComponent;

        auto& update = entry.second;
        auto& movement = update.UpdatedMovement;

        movementComponent.Position = movement.Position;
        movementComponent.Rotation = glm::vec3(movement.Rotation.x, 0.f, movement.Rotation.y);
        movementComponent.Variables = movement.Variables;
        movementComponent.Direction = movement.Direction;
        movementComponent.Flying = movement.Flying;
        movementComponent.VRPoseData = update.UpdatedVRPose;

        const CellIdComponent cCellBefore = cellIdComponent;
        cellIdComponent.Cell = movement.CellId;
        cellIdComponent.WorldSpaceId = movement.WorldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(movement.Position.x, movement.Position.y);


        const auto cCharacter = characterView.find(entity);
        const bool cIsDragon = cCharacter != characterView.end() && characterView.get<CharacterComponent>(*cCharacter).IsDragon();
        const bool cIsPlayer = cCharacter != characterView.end() && characterView.get<CharacterComponent>(*cCharacter).IsPlayer();
        const bool cCellChanged = cCellBefore.Cell != cellIdComponent.Cell || cCellBefore.WorldSpaceId != cellIdComponent.WorldSpaceId ||
                                  !(cCellBefore.CenterCoords == cellIdComponent.CenterCoords);
        // An NPC whose own move takes it out of a player's range is removed from that player's view.
        // A move that brings it back sends it to them again.
        // Players' characters are handled by their own cell changes.
        if (cCellChanged && !cIsPlayer && cCharacter != characterView.end())
            UpdateViewsAfterMove(acMessage.pPlayer, entity, cCellBefore, cellIdComponent, cIsDragon);

        ReferenceUpdate relayed;
        relayed.UpdatedMovement = movement;
        relayed.UpdatedVRPose = update.UpdatedVRPose;
        const size_t cEntryBits = Packer::EntryBits(s_scratch, entry.first, relayed);

        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == acMessage.pPlayer || !cellIdComponent.IsInRange(pPlayer->GetCellComponent(), cIsDragon))
                continue;

            // Movement goes unreliably: a lost message costs only itself, and the next one is already on its way.
            auto& pPacker = relays[pPlayer];
            if (!pPacker)
                pPacker = std::make_unique<Packer>(message.Tick, s_scratch);
            ServerReferencesMoveRequest full;
            if (pPacker->Add(entry.first, relayed, cEntryBits, full))
                pPlayer->Send(full, TiltedPhoques::kUnreliable);
        }
    }

    // The last message to a player skips Nagle's wait, so the whole batch leaves now.
    for (auto it = relays.begin(); it != relays.end(); ++it)
        it->first->Send(it.value()->Current(), TiltedPhoques::kUnreliableNoNagle);
}

Vector<ActionEvent> CharacterService::KeepActions(const entt::entity aEntity, AnimationComponent& aAnimationComponent, const Vector<ActionEvent>& acActions) const noexcept
{
    Vector<ActionEvent> kept;
    for (const auto& action : acActions)
    {
        auto [canceled, reason] = GameServer::Get()->GetWorld().GetScriptService().HandleCharacterMove(aEntity);
        if (canceled)
            continue;

        aAnimationComponent.CurrentAction = action;
        kept.push_back(action);
    }

    // A player who comes into range later is sent these, so the actor starts in the right state.
    aAnimationComponent.ActionsReplayCache.AppendAll(kept);
    return kept;
}

void CharacterService::OnActionEventsRequest(const PacketEvent<RequestActionEvents>& acMessage) const noexcept
{
    OwnerView<AnimationComponent, CellIdComponent> view(m_world, acMessage.GetSender());
    const auto characterView = m_world.view<CharacterComponent>();

    // Relayed at once and reliably to every other player in range, as the movement is.
    TiltedPhoques::Map<Player*, NotifyActionEvents> relays;
    for (const auto& [serverId, actions] : acMessage.Packet.Actions)
    {
        const auto entity = static_cast<entt::entity>(serverId);
        auto itor = view.find(entity);
        if (itor == std::end(view))
        {
            spdlog::debug("{:x} sent actions of {:x}, which it does not own or which does not exist", acMessage.pPlayer->GetConnectionId(), serverId);
            continue;
        }

        const Vector<ActionEvent> kept = KeepActions(entity, view.get<AnimationComponent>(*itor), actions);
        if (kept.empty())
            continue;

        const auto& cellIdComponent = view.get<CellIdComponent>(*itor);
        const auto cCharacter = characterView.find(entity);
        const bool cIsDragon = cCharacter != characterView.end() && characterView.get<CharacterComponent>(*cCharacter).IsDragon();
        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == acMessage.pPlayer || !cellIdComponent.IsInRange(pPlayer->GetCellComponent(), cIsDragon))
                continue;

            auto& relayed = relays[pPlayer].Actions[serverId];
            relayed.insert(relayed.end(), kept.begin(), kept.end());
        }
    }

    for (auto it = relays.begin(); it != relays.end(); ++it)
        it->first->Send(it->second);
}


void CharacterService::UpdateViewsAfterMove(const Player* apOwner, const entt::entity aEntity, const CellIdComponent& acBefore, const CellIdComponent& acNow,
                                            const bool aIsDragon) const noexcept
{
    const uint32_t cServerId = World::ToInteger(aEntity);
    // Only a door or a worldspace change removes it.
    // A walk along the edge of a player's loaded area would remove and re-create it again and again.
    // Outdoors, the copy there waits at that edge until the actor comes back.
    const bool cThroughDoor = acBefore.IsInInteriorCell() || acNow.IsInInteriorCell() || acBefore.WorldSpaceId != acNow.WorldSpaceId;
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == apOwner)
            continue;

        const auto& cViewer = pPlayer->GetCellComponent();
        const bool cWasInRange = acBefore.IsInRange(cViewer, aIsDragon);
        const bool cIsInRange = acNow.IsInRange(cViewer, aIsDragon);
        if (cWasInRange == cIsInRange || (cWasInRange && !cThroughDoor))
            continue;

        if (cWasInRange)
        {
            // A reference of the game's own goes through the door in that player's game too, first.
            // Left where its copy last stood, it stays there in their world, as it last moved (a follower left swimming in a lake).
            const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(aEntity);
            if (pFormIdComponent)
            {
                const auto& cPosition = m_world.get<MovementComponent>(aEntity).Position;
                NotifyActorTeleport teleport{};
                teleport.FormId = pFormIdComponent->Id;
                teleport.WorldSpaceId = acNow.WorldSpaceId;
                teleport.CellId = acNow.Cell;
                teleport.Position.x = cPosition.x;
                teleport.Position.y = cPosition.y;
                teleport.Position.z = cPosition.z;
                pPlayer->Send(teleport);
            }

            NotifyRemoveCharacter removeMessage;
            removeMessage.ServerId = cServerId;
            pPlayer->Send(removeMessage);
            spdlog::info("CellRange: character {:X} moved from cell {:X} to {:X}, out of player {:X}'s range; removed from their view{}", cServerId, acBefore.Cell.BaseId,
                         acNow.Cell.BaseId, pPlayer->GetId(), pFormIdComponent ? ", its reference there moved through the door too" : "");
        }
        else
        {
            CharacterSpawnRequest spawnMessage;
            Serialize(m_world, aEntity, &spawnMessage);
            pPlayer->Send(spawnMessage);
            spdlog::info("CellRange: character {:X} moved from cell {:X} to {:X}, into player {:X}'s range; sent to them", cServerId, acBefore.Cell.BaseId, acNow.Cell.BaseId,
                         pPlayer->GetId());
        }
    }
}

void CharacterService::OnFactionsChanges(const PacketEvent<RequestFactionsChanges>& acMessage) const noexcept
{
    OwnerView<CharacterComponent> view(m_world, acMessage.GetSender());

    auto& message = acMessage.Packet;

    for (auto& [id, factions] : message.Changes)
    {
        auto it = view.find(static_cast<entt::entity>(id));

        if (it == std::end(view) || view.get<OwnerComponent>(*it).GetOwner() != acMessage.pPlayer)
            continue;

        auto& characterComponent = view.get<CharacterComponent>(*it);
        characterComponent.FactionsContent = factions;
        characterComponent.SetDirtyFactions(true);
    }
}

void CharacterService::OnMountRequest(const PacketEvent<MountRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const entt::entity cRiderEntity = static_cast<entt::entity>(message.RiderId);
    const entt::entity cMountEntity = static_cast<entt::entity>(message.MountId);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto riderIt = view.find(cRiderEntity);
    const auto mountIt = view.find(cMountEntity);

    if (riderIt == view.end() || mountIt == view.end() || cRiderEntity == cMountEntity)
    {
        spdlog::info("Rejected mount request from player {:X} because rider {:X} or mount {:X} is invalid", acMessage.pPlayer->GetId(), message.RiderId, message.MountId);
        return;
    }

    if (!view.get<CharacterComponent>(*mountIt).IsMount())
    {
        spdlog::warn("Rejected mount request from player {:X} because actor {:X} is not a mount", acMessage.pPlayer->GetId(), message.MountId);
        return;
    }

    const auto& riderOwner = view.get<OwnerComponent>(*riderIt);
    const auto& mountOwner = view.get<OwnerComponent>(*mountIt);
    if (riderOwner.GetOwner() != acMessage.pPlayer || riderOwner.OwnershipEpoch != message.RiderOwnershipEpoch || mountOwner.OwnershipEpoch != message.MountOwnershipEpoch)
    {
        spdlog::info(
            "Rejected stale mount request from player {:X} for rider {:X} at epoch {} and mount {:X} at epoch {}; current epochs are {} and {}",
            acMessage.pPlayer->GetId(), message.RiderId, message.RiderOwnershipEpoch, message.MountId, message.MountOwnershipEpoch,
            riderOwner.OwnershipEpoch, mountOwner.OwnershipEpoch);
        return;
    }

    const auto& mountCell = view.get<CellIdComponent>(*mountIt);
    if (!acMessage.pPlayer->GetCellComponent().IsInRange(mountCell, view.get<CharacterComponent>(*mountIt).IsDragon()))
    {
        spdlog::info("Rejected mount request from player {:X} because mount {:X} is out of range", acMessage.pPlayer->GetId(), message.MountId);
        return;
    }

    if (!TransferOwnership(acMessage.pPlayer, *mountIt, OwnershipTransferReason::Mount))
        return;

    NotifyMount notify;
    notify.RiderId = message.RiderId;
    notify.MountId = message.MountId;

    if (!GameServer::Get()->SendToPlayersInRange(notify, cMountEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    else
        spdlog::info("Mount: rider {:X} mounts {:X}; told the players in range", message.RiderId, message.MountId);
}

void CharacterService::OnNewPackageRequest(const PacketEvent<NewPackageRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifyNewPackage notify;
    notify.ActorId = message.ActorId;
    notify.PackageId = message.PackageId;

    const entt::entity cEntity = static_cast<entt::entity>(message.ActorId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnRequestRespawn(const PacketEvent<RequestRespawn>& acMessage) const noexcept
{
    auto view = m_world.view<OwnerComponent, CharacterComponent>();
    auto it = view.find(static_cast<entt::entity>(acMessage.Packet.ActorId));
    if (it == view.end())
    {
        spdlog::warn("No OwnerComponent found for actor id {:X}", acMessage.Packet.ActorId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);

    // Replay cache needs to be cleared when a character respawns
    if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(*it))
        pAnimationComponent->ActionsReplayCache.Clear();

    if (ownerComponent.GetOwner() == acMessage.pPlayer)
    {
        if (!acMessage.Packet.AppearanceBuffer.empty())
        {
            auto& characterComponent = view.get<CharacterComponent>(*it);
            characterComponent.SaveBuffer = acMessage.Packet.AppearanceBuffer;
            characterComponent.ChangeFlags = acMessage.Packet.ChangeFlags;
        }

        NotifyRespawn notify;
        notify.ActorId = acMessage.Packet.ActorId;

        if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
    else
    {
        CharacterSpawnRequest message;
        Serialize(m_world, *it, &message);

        acMessage.GetSender()->Send(message);
    }
}

void CharacterService::OnSyncExperienceRequest(const PacketEvent<SyncExperienceRequest>& acMessage) const noexcept
{
    if (!bEnableXpSync)
        return;

    NotifySyncExperience notify;
    notify.Experience = acMessage.Packet.Experience;

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}

void CharacterService::OnDialogueRequest(const PacketEvent<DialogueRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifyDialogue notify{};
    notify.ServerId = message.ServerId;
    notify.SoundFilename = message.SoundFilename;
    notify.SceneId = message.SceneId;
    notify.ScenePhase = message.ScenePhase;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnSubtitleRequest(const PacketEvent<SubtitleRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifySubtitle notify{};
    notify.ServerId = message.ServerId;
    notify.Text = message.Text;
    notify.SceneId = message.SceneId;
    notify.ScenePhase = message.ScenePhase;

    const entt::entity cEntity = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::CreateCharacter(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    const auto gameId = message.ReferenceId;
    const auto baseId = message.FormId;

    const auto cEntity = m_world.create();
    const auto isTemporary = gameId.ModId == std::numeric_limits<uint32_t>::max();
    const auto isPlayer = (gameId.ModId == 0 && gameId.BaseId == 0x14);
    const auto isCustom = isPlayer || isTemporary;

    // For player characters and temporary forms
    if (!isCustom)
    {
        m_world.emplace<FormIdComponent>(cEntity, gameId.BaseId, gameId.ModId);
    }
    else if (baseId != GameId{} && !isTemporary)
    {
        m_world.destroy(cEntity);
        spdlog::warn("Unexpected NpcId, player {:x} might be forging packets", acMessage.pPlayer->GetConnectionId());
        return;
    }

    auto* const pServer = GameServer::Get();

    m_world.emplace<OwnerComponent>(cEntity, acMessage.pPlayer);

    auto& cellIdComponent = m_world.emplace<CellIdComponent>(cEntity, message.CellId);
    if (message.WorldSpaceId != GameId{})
    {
        cellIdComponent.WorldSpaceId = message.WorldSpaceId;
        cellIdComponent.CenterCoords = GridCellCoords::CalculateGridCellCoords(message.Position);
    }

    auto& characterComponent = m_world.emplace<CharacterComponent>(cEntity);
    characterComponent.ChangeFlags = message.ChangeFlags;
    characterComponent.SaveBuffer = std::move(message.AppearanceBuffer);
    characterComponent.BaseId = FormIdComponent(message.FormId);
    // Client-authoritative like BaseId; worst case a forged id changes which NPC identity renders.
    if (message.LeveledNpcPickId != GameId{})
        characterComponent.LeveledNpcPickId = FormIdComponent(message.LeveledNpcPickId);

    if (characterComponent.LeveledNpcPickId)
        spdlog::debug("Stored leveled NPC pick {:x}:{:x} for FormId {:x}:{:x}", message.LeveledNpcPickId.ModId, message.LeveledNpcPickId.BaseId, gameId.ModId, gameId.BaseId);
    characterComponent.FaceTints = message.FaceTints;
    characterComponent.FactionsContent = message.FactionsContent;
    characterComponent.SetDead(message.CurrentActorData.IsDead);
    characterComponent.SetPlayer(isPlayer);
    characterComponent.SetWeaponDrawn(message.CurrentActorData.IsWeaponDrawn);
    characterComponent.SetDragon(message.IsDragon);
    characterComponent.SetMount(message.IsMount);
    characterComponent.SetPlayerSummon(message.IsPlayerSummon);

    auto& inventoryComponent = m_world.emplace<InventoryComponent>(cEntity);
    inventoryComponent.Content = message.CurrentActorData.InitialInventory;

    auto& actorValuesComponent = m_world.emplace<ActorValuesComponent>(cEntity);
    actorValuesComponent.CurrentActorValues = message.CurrentActorData.InitialActorValues;

    spdlog::debug("FormId: {:x}:{:x} - NpcId: {:x}:{:x} assigned to {:x}", gameId.ModId, gameId.BaseId, baseId.ModId, baseId.BaseId, acMessage.pPlayer->GetConnectionId());

    auto& movementComponent = m_world.emplace<MovementComponent>(cEntity);
    movementComponent.Tick = pServer->GetTick();
    movementComponent.Position = message.Position;
    movementComponent.Rotation = {message.Rotation.x, 0.f, message.Rotation.y};

    m_world.emplace<AnimationComponent>(cEntity);

    // If this is a player character store a ref and trigger an event
    if (isPlayer)
    {
        const auto pPlayer = acMessage.pPlayer;

        pPlayer->SetCharacter(cEntity);
        pPlayer->GetQuestLogComponent().QuestContent = message.QuestContent;
        characterComponent.PlayerId = pPlayer->GetId();

        auto& dispatcher = m_world.GetDispatcher();
        dispatcher.trigger(PlayerEnterWorldEvent(pPlayer));
    }

    AssignCharacterResponse response{};
    response.Cookie = message.Cookie;
    response.Owner = true;
    PopulateAssignmentResponse(cEntity, response);

    pServer->Send(acMessage.pPlayer->GetConnectionId(), response);

    auto& dispatcher = m_world.GetDispatcher();
    dispatcher.trigger(CharacterSpawnedEvent(cEntity));
}

void CharacterService::PopulateAssignmentResponse(const entt::entity aEntity, AssignCharacterResponse& aResponse) const noexcept
{
    aResponse.ServerId = World::ToInteger(aEntity);

    if (const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(aEntity))
        aResponse.OwnershipEpoch = pOwnerComponent->OwnershipEpoch;

    if (const auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(aEntity))
        aResponse.AllActorValues = pActorValuesComponent->CurrentActorValues;

    if (const auto* pInventoryComponent = m_world.try_get<InventoryComponent>(aEntity))
        aResponse.CurrentInventory = pInventoryComponent->Content;

    if (const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(aEntity))
    {
        aResponse.PlayerId = pCharacterComponent->PlayerId;
        aResponse.IsDead = pCharacterComponent->IsDead();
        aResponse.IsWeaponDrawn = pCharacterComponent->IsWeaponDrawn();
        aResponse.LeveledNpcPickId = pCharacterComponent->LeveledNpcPickId.Id;

        if (pCharacterComponent->LeveledNpcPickId)
        {
            spdlog::debug(
                "Including leveled NPC pick in assignment response for actor {:X}, pick: {:x}:{:x}, owner: {}, epoch: {}",
                aResponse.ServerId,
                aResponse.LeveledNpcPickId.ModId,
                aResponse.LeveledNpcPickId.BaseId,
                aResponse.Owner,
                aResponse.OwnershipEpoch);
        }
    }

    if (const auto* pMovementComponent = m_world.try_get<MovementComponent>(aEntity))
        aResponse.Position = pMovementComponent->Position;

    if (const auto* pCellIdComponent = m_world.try_get<CellIdComponent>(aEntity))
    {
        aResponse.CellId = pCellIdComponent->Cell;
        aResponse.WorldSpaceId = pCellIdComponent->WorldSpaceId;
    }

    if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(aEntity))
        aResponse.ActionsToReplay = pAnimationComponent->ActionsReplayCache.FormRefinedReplayChain();
}

const char* CharacterService::GetOwnershipTransferReasonName(const OwnershipTransferReason aReason) noexcept
{
    switch (aReason)
    {
    case OwnershipTransferReason::LeaderAssignment:
        return "party leader assignment";
    case OwnershipTransferReason::LeaderClaim:
        return "party leader claim";
    case OwnershipTransferReason::Mount:
        return "mounting";
    case OwnershipTransferReason::Relinquish:
        return "owner relinquished control";
    case OwnershipTransferReason::OwnerUnavailable:
        return "owner became unavailable";
    case OwnershipTransferReason::KillingBlow:
        return "killing blow on a protected NPC";
    }

    return "unknown reason";
}

bool CharacterService::CanClaimOwnership(Player* apPlayer, const entt::entity aEntity, const uint32_t aExpectedOwnershipEpoch, const OwnershipTransferReason aReason,
                                         const bool aFollower) const noexcept
{
    const uint32_t serverId = World::ToInteger(aEntity);
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto it = view.find(aEntity);
    if (it == view.end())
    {
        spdlog::info("Rejected {} from player {:X} because actor {:X} is unavailable", pReasonName, apPlayer->GetId(), serverId);
        return false;
    }
    // A temporary actor (no form id on the server) can be claimed only as a dead body, so others can drag it.
    // Or for a killing blow, which makes it one.
    if (!m_world.try_get<FormIdComponent>(aEntity) && !view.get<CharacterComponent>(*it).IsDead() && aReason != OwnershipTransferReason::KillingBlow)
    {
        spdlog::info("Rejected {} from player {:X} because actor {:X} is temporary and alive", pReasonName, apPlayer->GetId(), serverId);
        return false;
    }

    const auto& ownerComponent = view.get<OwnerComponent>(*it);
    const auto& characterComponent = view.get<CharacterComponent>(*it);
    const auto& cellIdComponent = view.get<CellIdComponent>(*it);
    Player* const pCurrentOwner = ownerComponent.GetOwner();
    const uint32_t currentOwnerId = pCurrentOwner ? pCurrentOwner->GetId() : 0;

    // Logged at info so a refused claim on a dragged body says why (live-body-grab).
    const auto reject = [&](const char* apReason)
    {
        spdlog::info("Rejected {} from player {:X} for {} {:X}: {} (requested epoch {}, current owner {:X}, current epoch {})", pReasonName, apPlayer->GetId(),
                     characterComponent.IsDead() ? "dead body" : "actor", serverId, apReason, aExpectedOwnershipEpoch, currentOwnerId, ownerComponent.OwnershipEpoch);
        return false;
    };

    if (aExpectedOwnershipEpoch == 0 || ownerComponent.OwnershipEpoch != aExpectedOwnershipEpoch)
        return reject("the ownership epoch is stale");

    if (!pCurrentOwner || pCurrentOwner == apPlayer)
        return reject("the player already owns the actor");

    if (characterComponent.IsMount() || characterComponent.IsPlayer())
        return reject("the actor cannot be claimed");

    // Not for the leader's claim: its game claims only actors loaded next to it, wherever the server last had them.
    if (aReason != OwnershipTransferReason::LeaderClaim && !apPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
        return reject("the actor is out of range");

    // A follower stays with its player's game: another player cannot claim it while that player is connected.
    if (!aFollower && ownerComponent.FollowerOfPlayerId && ownerComponent.FollowerOfPlayerId != apPlayer->GetId() &&
        m_world.GetPlayerManager().GetById(ownerComponent.FollowerOfPlayerId))
        return reject("it is another player's follower");

    // A flying dragon stays with its owner until it lands or dies; flight is not handed over and it would fall.
    if (characterComponent.IsDragon() && pCurrentOwner && pCurrentOwner != apPlayer)
        if (const auto* pMovement = m_world.try_get<MovementComponent>(aEntity); pMovement && pMovement->Flying)
            return reject("it is a dragon flying in its owner's game");

    // A player's killing blow on a protected NPC: the game lets only its own player kill one, so it goes to the killer's game.
    // Party or not, leader or not; another player's follower was refused above.
    if (aReason == OwnershipTransferReason::KillingBlow)
        return characterComponent.IsDead() ? reject("it is already dead") : true;

    // A dead body goes to whoever grabs it, party or not, since only the owner sends its bones.
    // The epoch check above settles two players grabbing at once.
    const bool cDeadBody = characterComponent.IsDead();
    if (cDeadBody)
        return true;

    auto& partyService = m_world.GetPartyService();
    if (!partyService.IsPlayerInParty(apPlayer) || (!cDeadBody && !aFollower && !partyService.IsPlayerLeader(apPlayer)))
        return reject(cDeadBody || aFollower ? "the player is not in a party" : "the player is not the party leader");

    PartyService::Party* const pParty = partyService.GetPlayerParty(apPlayer);
    if (!pParty || std::find(pParty->Members.begin(), pParty->Members.end(), pCurrentOwner) == pParty->Members.end())
        return reject("the current owner is not in the party");

    return true;
}

bool CharacterService::TransferOwnership(Player* apPlayer, const entt::entity aEntity, const OwnershipTransferReason aReason, const bool aResetInvalidOwners) const noexcept
{
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto it = view.find(aEntity);
    if (!apPlayer || it == view.end())
    {
        spdlog::warn("Cannot transfer ownership of actor {:X} for {} because the target is invalid", World::ToInteger(aEntity), pReasonName);
        return false;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    Player* const pOldOwner = ownerComponent.GetOwner();
    if (pOldOwner == apPlayer)
        return true;

    const uint32_t oldOwnerId = pOldOwner ? pOldOwner->GetId() : 0;
    const uint32_t oldEpoch = ownerComponent.OwnershipEpoch;
    uint32_t newEpoch = oldEpoch + 1;
    if (newEpoch == 0)
        newEpoch = 1;

    NotifyOwnershipTransfer notify{};
    notify.ServerId = World::ToInteger(aEntity);
    notify.OwnerPlayerId = apPlayer->GetId();
    notify.OwnershipEpoch = newEpoch;
    notify.CurrentActorData = BuildActorData(aEntity);
    notify.LeveledNpcPickId = view.get<CharacterComponent>(*it).LeveledNpcPickId.Id;

    ownerComponent.SetOwner(apPlayer);
    ownerComponent.OwnershipEpoch = newEpoch;
    // The new owner's reports are ordered on its own clock, which may run a little behind the old owner's.
    if (auto* pMovement = m_world.try_get<MovementComponent>(aEntity))
        pMovement->Tick = 0;
    if (aResetInvalidOwners)
        ownerComponent.InvalidOwners.clear();

    if (!GameServer::Get()->SendToPlayersInRange(notify, aEntity, pOldOwner))
        spdlog::error("Failed to broadcast ownership transfer for actor {:X}", notify.ServerId);

    // The former owner may already be out of range, so notify it directly as well.
    if (pOldOwner)
        pOldOwner->Send(notify);

    // Also tell the new owner directly when the actor is out of its range (a leader's claim), or it never sends for it.
    if (apPlayer != pOldOwner && !apPlayer->GetCellComponent().IsInRange(view.get<CellIdComponent>(*it), view.get<CharacterComponent>(*it).IsDragon()))
        apPlayer->Send(notify);

    spdlog::info(
        "Transferred ownership of actor {:X} from player {:X} to player {:X} for {} (epoch {} to {})",
        notify.ServerId, oldOwnerId, notify.OwnerPlayerId, pReasonName, oldEpoch, newEpoch);

    return true;
}

void CharacterService::TransferToNextOwner(const entt::entity aEntity, const OwnershipTransferReason aReason) const noexcept
{
    const char* pReasonName = GetOwnershipTransferReasonName(aReason);
    const auto view = m_world.view<OwnerComponent, CharacterComponent, CellIdComponent>();
    const auto it = view.find(aEntity);
    if (it == view.end())
    {
        spdlog::warn("Cannot select a new owner for actor {:X} after {} because the actor is missing", World::ToInteger(aEntity), pReasonName);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    const auto& characterComponent = view.get<CharacterComponent>(*it);
    const auto& cellIdComponent = view.get<CellIdComponent>(*it);

    // Players whose game is sending come first; a silent one only when nobody else is in range.
    const auto now = std::chrono::steady_clock::now();
    for (const bool cSilentToo : {false, true})
    {
        for (Player* pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (IsSilent(pPlayer, now) != cSilentToo)
                continue;

            if (std::find(ownerComponent.InvalidOwners.begin(), ownerComponent.InvalidOwners.end(), pPlayer) != ownerComponent.InvalidOwners.end())
                continue;

            if (!pPlayer->GetCellComponent().IsInRange(cellIdComponent, characterComponent.IsDragon()))
                continue;

            // Declined owners are kept, so the actor cannot bounce between unloaded clients.
            if (TransferOwnership(pPlayer, aEntity, aReason, false))
                return;
        }
    }

    spdlog::info("Removing actor {:X} after {} because no eligible owner remains", World::ToInteger(aEntity), pReasonName);
    m_world.GetDispatcher().trigger(CharacterRemoveEvent(World::ToInteger(aEntity)));
}

ActorData CharacterService::BuildActorData(const entt::entity acEntity) const noexcept
{
    ActorData actorData{};

    const auto* pActorValuesComponent = m_world.try_get<ActorValuesComponent>(acEntity);
    if (pActorValuesComponent)
    {
        actorData.InitialActorValues = pActorValuesComponent->CurrentActorValues;
    }

    const auto* pInventoryComponent = m_world.try_get<InventoryComponent>(acEntity);
    if (pInventoryComponent)
    {
        actorData.InitialInventory = pInventoryComponent->Content;
    }

    actorData.IsDead = false;
    const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(acEntity);
    if (pCharacterComponent)
    {
        actorData.IsDead = pCharacterComponent->IsDead();
        actorData.IsWeaponDrawn = pCharacterComponent->IsWeaponDrawn();
    }

    return actorData;
}

void CharacterService::ProcessFactionsChanges() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    const auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();

    TiltedPhoques::Map<Player*, NotifyFactionsChanges> messages;

    for (auto entity : characterView)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(entity);
        auto& cellIdComponent = characterView.get<CellIdComponent>(entity);
        auto& ownerComponent = characterView.get<OwnerComponent>(entity);

        // If we have nothing new to send skip this
        if (!characterComponent.IsDirtyFactions())
            continue;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            if (pPlayer == ownerComponent.GetOwner())
                continue;

            if (!cellIdComponent.IsInRange(pPlayer->GetCellComponent(), characterComponent.IsDragon()))
                continue;

            auto& message = messages[pPlayer];
            auto& change = message.Changes[World::ToInteger(entity)];

            change = characterComponent.FactionsContent;
        }

        characterComponent.SetDirtyFactions(false);
    }

    for (auto [pPlayer, message] : messages)
    {
        if (!message.Changes.empty())
            pPlayer->Send(message);
    }
}

void CharacterService::OnClashRequest(const PacketEvent<ClashRequest>& acMessage) const noexcept
{
    const ClashRequest& packet = acMessage.Packet;
    const auto character = acMessage.pPlayer->GetCharacter();
    const auto other = static_cast<entt::entity>(packet.OtherId);
    if (!character || !m_world.valid(other))
        return;

    NotifyClash notify{};
    notify.FromId = World::ToInteger(*character);
    notify.OtherId = packet.OtherId;
    notify.OtherSide = packet.OtherSide;
    notify.OwnSide = packet.OwnSide;
    notify.Point = packet.Point;
    notify.Speed = packet.Speed;
    notify.Tick = packet.Tick;
    // To everyone near the weapon that was met, its owner included, but not back to the sender.
    if (!GameServer::Get()->SendToPlayersInRange(notify, other, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void CharacterService::OnArrowNockedRequest(const PacketEvent<ArrowNockedRequest>& acMessage) const noexcept
{
    // Always the sender's own character: a player can only say what is on their own bow.
    const auto character = acMessage.pPlayer->GetCharacter();
    if (!character || !m_world.valid(*character))
        return;

    NotifyArrowNocked notify{};
    notify.ServerId = World::ToInteger(*character);
    notify.Nocked = acMessage.Packet.Nocked;
    notify.InHand = acMessage.Packet.InHand;
    notify.HandSide = acMessage.Packet.HandSide;
    notify.HandOffset = acMessage.Packet.HandOffset;
    notify.HandRotation = acMessage.Packet.HandRotation;
    if (!GameServer::Get()->SendToPlayersInRange(notify, *character, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
