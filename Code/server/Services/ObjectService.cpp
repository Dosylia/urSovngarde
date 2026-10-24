#include <Services/ObjectService.h>

#include <GameServer.h>
#include <World.h>
#include <Components.h>

#include <Events/PlayerLeaveCellEvent.h>

#include <Messages/ActivateRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Structs/DoorState.h>

namespace
{
// A door's state after the last activation the server passed on, kept apart from the per-cell object entities.
// Those are made only from the opener's own cell and go when that cell empties, so a door opened from the next cell over was never kept.
struct DoorRecord
{
    uint8_t State{};
    //! Where its opener stood; the record goes once no player has that place in range.
    CellIdComponent Where{};
};
Map<GameId, DoorRecord> s_doorStates;
} // namespace

ObjectService::ObjectService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_leaveCellConnection = aDispatcher.sink<PlayerLeaveCellEvent>().connect<&ObjectService::OnPlayerLeaveCellEvent>(this);
    m_assignObjectConnection = aDispatcher.sink<PacketEvent<AssignObjectsRequest>>().connect<&ObjectService::OnAssignObjectsRequest>(this);
    m_activateConnection = aDispatcher.sink<PacketEvent<ActivateRequest>>().connect<&ObjectService::OnActivate>(this);
    m_lockChangeConnection = aDispatcher.sink<PacketEvent<LockChangeRequest>>().connect<&ObjectService::OnLockChange>(this);
    m_scriptAnimationConnection = aDispatcher.sink<PacketEvent<ScriptAnimationRequest>>().connect<&ObjectService::OnScriptAnimationRequest>(this);
}

// TODO(cosideci): the cell handling of objects need to be revamped.
// We already store the location and worldspace of the mod through CellIdComponent.
// Clients need a message saying the entity was destroyed.
void ObjectService::OnPlayerLeaveCellEvent(const PlayerLeaveCellEvent& acEvent) noexcept
{
    // A door state nobody can see any more is forgotten: its cell may reset, or a script may move the door, before anyone comes back.
    for (auto it = s_doorStates.begin(); it != s_doorStates.end();)
    {
        bool cSeen = false;
        for (Player* pPlayer : m_world.GetPlayerManager())
            cSeen = cSeen || it->second.Where.IsInRange(pPlayer->GetCellComponent(), false);
        it = cSeen ? std::next(it) : s_doorStates.erase(it);
    }

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer->GetCellComponent().Cell == acEvent.OldCell)
            return;
    }

    auto objectView = m_world.view<ObjectComponent, CellIdComponent>();
    Vector<entt::entity> toDestroy;

    for (auto entity : objectView)
    {
        const auto& cellIdComponent = objectView.get<CellIdComponent>(entity);

        if (cellIdComponent.Cell != acEvent.OldCell)
            continue;

        toDestroy.push_back(entity);
    }

    for (auto& entity : toDestroy)
    {
        m_world.destroy(entity);
    }
}

// NOTE: this whole system kinda relies on all objects in a cell being static.
// This is fine for containers and doors, but if this system is expanded, think of temporaries.
void ObjectService::OnAssignObjectsRequest(const PacketEvent<AssignObjectsRequest>& acMessage) noexcept
{
    auto view = m_world.view<FormIdComponent, ObjectComponent, InventoryComponent>();

    AssignObjectsResponse response;

    for (const ObjectData& object : acMessage.Packet.Objects)
    {
        const auto iter = std::find_if(
            std::begin(view), std::end(view),
            [view, id = object.Id](auto entity)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);
                return formIdComponent.Id == id;
            });

        if (iter != std::end(view))
        {
            ObjectData objectData;
            objectData.ServerId = World::ToInteger(*iter);

            auto& formIdComponent = view.get<FormIdComponent>(*iter);
            objectData.Id = formIdComponent.Id;

            auto& objectComponent = view.get<ObjectComponent>(*iter);
            objectData.CurrentLockData = objectComponent.CurrentLockData;

            auto& inventoryComponent = view.get<InventoryComponent>(*iter);
            objectData.CurrentInventory = inventoryComponent.Content;

            objectData.IsSenderFirst = false;
            if (const auto cRecord = s_doorStates.find(formIdComponent.Id); cRecord != s_doorStates.end())
                objectData.DoorOpenState = cRecord->second.State;

            response.Objects.push_back(objectData);
        }
        else
        {
            const auto cEntity = m_world.create();

            m_world.emplace<FormIdComponent>(cEntity, object.Id);

            auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
            objectComponent.CurrentLockData = object.CurrentLockData;

            m_world.emplace<CellIdComponent>(cEntity, object.CellId, object.WorldSpaceId, object.CurrentCoords);
            auto& inventoryComp = m_world.emplace<InventoryComponent>(cEntity);
            inventoryComp.Content = object.CurrentInventory;

            ObjectData objectData;
            objectData.Id = object.Id;
            objectData.ServerId = World::ToInteger(cEntity);
            objectData.IsSenderFirst = true;
            if (const auto cRecord = s_doorStates.find(object.Id); cRecord != s_doorStates.end())
                objectData.DoorOpenState = cRecord->second.State;

            response.Objects.push_back(objectData);
        }
    }

    if (!response.Objects.empty())
        acMessage.pPlayer->Send(response);
}

void ObjectService::OnActivate(const PacketEvent<ActivateRequest>& acMessage) const noexcept
{
    // A door's state is kept, so a player who enters its cell later gets the door as it was left.
    if (const uint8_t cAfter = DoorState::AfterActivation(acMessage.Packet.PreActivationOpenState))
    {
        s_doorStates[acMessage.Packet.Id] = DoorRecord{cAfter, acMessage.pPlayer->GetCellComponent()};
        spdlog::debug("Doors: {:X}:{:X} left in door state {} by an activation", acMessage.Packet.Id.ModId, acMessage.Packet.Id.BaseId, cAfter);
    }

    NotifyActivate notifyActivate;
    notifyActivate.Id = acMessage.Packet.Id;
    notifyActivate.ActivatorId = acMessage.Packet.ActivatorId;
    notifyActivate.PreActivationOpenState = acMessage.Packet.PreActivationOpenState;

    // Outdoors, every player who has the door's cell loaded gets it, not only those in that very cell.
    // The sender's game made the activation, so the door is within the sender's loaded grid.
    const CellIdComponent& cSenderCell = acMessage.pPlayer->GetCellComponent();
    for (auto pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == acMessage.pPlayer)
            continue;

        const CellIdComponent& cCell = pPlayer->GetCellComponent();
        const bool cSameCell = cCell.Cell == acMessage.Packet.CellId;
        const bool cLoadedThere = !cSenderCell.IsInInteriorCell() && !cCell.IsInInteriorCell() && cSenderCell.IsInRange(cCell, false);
        if (cSameCell || cLoadedThere)
            pPlayer->Send(notifyActivate);
    }
}

void ObjectService::OnLockChange(const PacketEvent<LockChangeRequest>& acMessage) const noexcept
{
    NotifyLockChange notifyLockChange;
    notifyLockChange.Id = acMessage.Packet.Id;
    notifyLockChange.IsLocked = acMessage.Packet.IsLocked;
    notifyLockChange.LockLevel = acMessage.Packet.LockLevel;

    auto objectView = m_world.view<FormIdComponent, ObjectComponent>();

    const auto iter = std::find_if(
        std::begin(objectView), std::end(objectView),
        [objectView, id = acMessage.Packet.Id](auto entity)
        {
            const auto& formIdComponent = objectView.get<FormIdComponent>(entity);
            return formIdComponent.Id == id;
        });

    if (iter != std::end(objectView))
    {
        auto& objectComponent = objectView.get<ObjectComponent>(*iter);
        objectComponent.CurrentLockData.IsLocked = acMessage.Packet.IsLocked;
        objectComponent.CurrentLockData.LockLevel = acMessage.Packet.LockLevel;
    }

    // A locked door is no longer open as an activation left it: a later arrival must not open it.
    if (acMessage.Packet.IsLocked)
        s_doorStates.erase(acMessage.Packet.Id);

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer == acMessage.pPlayer)
            continue;

        if (pPlayer->GetCellComponent().Cell == acMessage.Packet.CellId)
            pPlayer->Send(notifyLockChange);
    }
}

void ObjectService::OnScriptAnimationRequest(const PacketEvent<ScriptAnimationRequest>& acMessage) noexcept
{
    auto& packet = acMessage.Packet;

    NotifyScriptAnimation message{};
    message.FormID = packet.FormID;
    message.Animation = packet.Animation;
    message.EventName = packet.EventName;

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        pPlayer->Send(message);
    }
}
