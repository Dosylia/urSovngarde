#include "Events/CharacterInteriorCellChangeEvent.h"
#include "Events/CharacterExteriorCellChangeEvent.h"
#include "Events/PlayerLeaveCellEvent.h"

#include <Services/PlayerService.h>
#include <Services/CharacterService.h>
#include <GameServer.h>

#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifyActorValueChanges.h>

namespace
{
// Skyrim's actor value index for health. The client and the bot both use the raw number; there is no shared enum.
constexpr uint32_t kHealthValue = 24;
}
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyPlayerCellChanged.h>

#include <Setting.h>
namespace
{
Console::Setting fGoldLossFactor{"Gameplay:fGoldLossFactor", "Factor of the amount of gold lost on death", 0.0f};
}

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_interiorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterInteriorCellRequest>>().connect<&PlayerService::HandleInteriorCellEnter>(this))
    , m_gridCellShiftConnection(aDispatcher.sink<PacketEvent<ShiftGridCellRequest>>().connect<&PlayerService::HandleGridCellShift>(this))
    , m_exteriorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterExteriorCellRequest>>().connect<&PlayerService::HandleExteriorCellEnter>(this))
    , m_playerRespawnConnection(aDispatcher.sink<PacketEvent<PlayerRespawnRequest>>().connect<&PlayerService::OnPlayerRespawnRequest>(this))
    , m_playerLevelConnection(aDispatcher.sink<PacketEvent<PlayerLevelRequest>>().connect<&PlayerService::OnPlayerLevelRequest>(this))
{
}

void SendPlayerCellChanged(const Player* apPlayer) noexcept
{
    auto& cellComponent = apPlayer->GetCellComponent();

    NotifyPlayerCellChanged notify{};
    notify.PlayerId = apPlayer->GetId();
    notify.WorldSpaceId = cellComponent.WorldSpaceId;
    notify.CellId = cellComponent.Cell;

    GameServer::Get()->SendToPlayers(notify, apPlayer);
}

void PlayerService::HandleGridCellShift(const PacketEvent<ShiftGridCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;

    auto& message = acMessage.Packet;

    const GameId oldCell = pPlayer->GetCellComponent().Cell;
    // Where the viewer was, to remove what has just left its range.
    const CellIdComponent previous = pPlayer->GetCellComponent();

    CellIdComponent cell = CellIdComponent{message.PlayerCell, message.WorldSpaceId, message.CenterCoords};
    pPlayer->SetCellComponent(cell);

    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));

    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        const auto& ownedComponent = characterView.get<OwnerComponent>(character);
        const auto& characterCellComponent = characterView.get<CellIdComponent>(character);

        if (ownedComponent.GetOwner() == pPlayer)
            continue;

        const auto& characterComponent = characterView.get<CharacterComponent>(character);
        // An actor's parent cell can be temporary and absent from the client's cell list.
        // Use the same worldspace/position range as movement broadcasts, including dragon range.
        const bool isInRange = cell.IsInRange(characterCellComponent, characterComponent.IsDragon());

        if (!isInRange)
        {
            continue;
        }

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(m_world, character, &spawnMessage);

        pPlayer->Send(spawnMessage);
    }

    RemoveWhatLeftRange(pPlayer, previous);
}

// Removes from a viewer's view what was in range of its previous place but not of its current one.
// Called by both exterior handlers, because a client sends the cell entry before the grid shift (`rangeback`).
void PlayerService::RemoveWhatLeftRange(Player* apPlayer, const CellIdComponent& acPrevious) const noexcept
{
    if (!acPrevious.WorldSpaceId)
        return;

    const auto& now = apPlayer->GetCellComponent();
    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        if (characterView.get<OwnerComponent>(character).GetOwner() == apPlayer)
            continue;

        const auto& characterCellComponent = characterView.get<CellIdComponent>(character);
        const bool isDragon = characterView.get<CharacterComponent>(character).IsDragon();
        if (!acPrevious.IsInRange(characterCellComponent, isDragon) || now.IsInRange(characterCellComponent, isDragon))
            continue;

        NotifyRemoveCharacter removeMessage;
        removeMessage.ServerId = World::ToInteger(character);
        apPlayer->Send(removeMessage);
        spdlog::debug("'{}' walked away from character {:X}; removed from its view", apPlayer->GetUsername().c_str(), removeMessage.ServerId);
    }
}

void PlayerService::HandleExteriorCellEnter(const PacketEvent<EnterExteriorCellRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    auto* pPlayer = acMessage.pPlayer;

    if (pPlayer->GetCharacter())
    {
        auto entity = *pPlayer->GetCharacter();

        auto cell = CellIdComponent{message.CellId, message.WorldSpaceId, message.CurrentCoords};

        if (pPlayer->GetCellComponent())
        {
            m_world.GetDispatcher().trigger(CharacterExteriorCellChangeEvent{pPlayer, entity, message.WorldSpaceId, message.CurrentCoords});
        }

        const CellIdComponent previous = pPlayer->GetCellComponent();
        // An arrival (a load, a door, a fast travel) comes from indoors, another worldspace or further than one cell.
        // A walk into the next cell is none.
        // CharacterService holds back handing the party leader NPCs for a while after an arrival.
        const bool cArrived = !previous || previous.IsInInteriorCell() || previous.WorldSpaceId != message.WorldSpaceId ||
                              std::abs(previous.CenterCoords.X - message.CurrentCoords.X) > 1 || std::abs(previous.CenterCoords.Y - message.CurrentCoords.Y) > 1;
        if (cArrived)
            pPlayer->MarkCellEnter();
        pPlayer->SetCellComponent(cell);
        RemoveWhatLeftRange(pPlayer, previous);

        // Sends this player every character in range of where they now are, as the other two cell handlers do.
        // Walking into the next outdoor cell raises only this event, not a grid shift.
        // Re-sending an existing copy is harmless: the client keeps it and refreshes its ownership epoch.
        auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
        for (auto character : characterView)
        {
            const auto& ownedComponent = characterView.get<OwnerComponent>(character);
            if (ownedComponent.GetOwner() == pPlayer)
                continue;

            const auto& characterCellComponent = characterView.get<CellIdComponent>(character);
            const auto& characterComponent = characterView.get<CharacterComponent>(character);

            // Same range test as the movement broadcasts, dragons included.
            if (!cell.IsInRange(characterCellComponent, characterComponent.IsDragon()))
                continue;

            CharacterSpawnRequest spawnMessage;
            CharacterService::Serialize(m_world, character, &spawnMessage);

            pPlayer->Send(spawnMessage);
        }

        SendPlayerCellChanged(pPlayer);
    }
}

void PlayerService::HandleInteriorCellEnter(const PacketEvent<EnterInteriorCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;

    auto& message = acMessage.Packet;

    const auto oldCell = pPlayer->GetCellComponent().Cell;

    // Into an interior is always an arrival (see HandleExteriorCellEnter).
    pPlayer->MarkCellEnter();
    auto cell = CellIdComponent{message.CellId, {}, {}};
    pPlayer->SetCellComponent(cell);

    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));

    if (pPlayer->GetCharacter())
    {
        auto entity = *pPlayer->GetCharacter();

        if (auto pCellIdComponent = m_world.try_get<CellIdComponent>(entity); pCellIdComponent)
        {
            m_world.GetDispatcher().trigger(CharacterInteriorCellChangeEvent{pPlayer, entity, message.CellId});
        }
    }

    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        const auto& ownedComponent = characterView.get<OwnerComponent>(character);

        if (ownedComponent.GetOwner() == pPlayer)
            continue;

        if (message.CellId != characterView.get<CellIdComponent>(character).Cell)
            continue;

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(m_world, character, &spawnMessage);

        pPlayer->Send(spawnMessage);
    }

    SendPlayerCellChanged(pPlayer);
}

void PlayerService::OnPlayerRespawnRequest(const PacketEvent<PlayerRespawnRequest>& acMessage) const noexcept
{
    float goldLossFactor = fGoldLossFactor.as_float();

    auto character = acMessage.pPlayer->GetCharacter();
    if (!character)
        return;

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(*character));

    if (it != view.end())
    {
        if (goldLossFactor != 0.0)
        {
            auto& inventoryComponent = view.get<InventoryComponent>(*it);

            GameId goldId(0, 0xF);
            int32_t goldCount = inventoryComponent.Content.GetEntryCountById(goldId);
            int32_t goldToRemove = static_cast<int32_t>(goldCount * goldLossFactor);

            Inventory::Entry entry{};
            entry.BaseId = goldId;
            entry.Count = -goldToRemove;

            inventoryComponent.Content.AddOrRemoveEntry(entry);

            NotifyInventoryChanges notifyInventoryChanges{};
            notifyInventoryChanges.ServerId = World::ToInteger(*character);
            if (const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*character))
                notifyInventoryChanges.OwnershipEpoch = pOwnerComponent->OwnershipEpoch;
            notifyInventoryChanges.Item = entry;
            notifyInventoryChanges.Drop = false;

            // Exclude respawned player from inventory changes notification...
            if (!GameServer::Get()->SendToPlayersInRange(notifyInventoryChanges, *character, acMessage.GetSender()))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

            // ...and instead, send NotifyPlayerRespawn so that the client can print a message.
            NotifyPlayerRespawn notifyPlayerRespawn{};
            notifyPlayerRespawn.GoldLost = goldToRemove;

            acMessage.pPlayer->Send(notifyPlayerRespawn);
        }
    }

    // Let all other players in cell respawn this player, since the body state seems to be bugged otherwise.
    // Done outside the inventory lookup, so a character without a server inventory respawns too.
    NotifyRespawn notifyRespawn{};
    notifyRespawn.ActorId = World::ToInteger(*character);

    if (!GameServer::Get()->SendToPlayersInRange(notifyRespawn, *character, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    // The stored health is still the death value; reset it, or every later copy is built dead.
    auto valuesView = m_world.view<ActorValuesComponent>();
    if (const auto valuesIt = valuesView.find(*character); valuesIt != valuesView.end())
    {
        auto& values = valuesView.get<ActorValuesComponent>(*valuesIt);
        auto& current = values.CurrentActorValues.ActorValuesList;
        const auto& maximums = values.CurrentActorValues.ActorMaxValuesList;

        const auto maxIt = maximums.find(kHealthValue);
        const auto curIt = current.find(kHealthValue);

        // Only a death value is reset; a partial health or a missing maximum is left alone.
        if (maxIt != maximums.end() && maxIt->second > 0.f && curIt != current.end() && curIt->second <= 0.f)
        {
            current[kHealthValue] = maxIt->second;

            NotifyActorValueChanges notifyValues{};
            notifyValues.Id = World::ToInteger(*character);
            if (const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*character))
                notifyValues.OwnershipEpoch = pOwnerComponent->OwnershipEpoch;
            notifyValues.Values[kHealthValue] = maxIt->second;

            if (!GameServer::Get()->SendToPlayersInRange(notifyValues, *character, acMessage.GetSender()))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

            spdlog::debug("Respawn restored stored health of {:x} to {:.1f}", World::ToInteger(*character), maxIt->second);
        }
    }

    // A respawned character is alive. Leaving the flag set means the next copy is built as a corpse.
    auto characterView = m_world.view<CharacterComponent>();
    if (const auto characterIt = characterView.find(*character); characterIt != characterView.end())
        characterView.get<CharacterComponent>(*characterIt).SetDead(false);
}

void PlayerService::OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept
{
    acMessage.pPlayer->SetLevel(acMessage.Packet.NewLevel);

    NotifyPlayerLevel notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.NewLevel = acMessage.Packet.NewLevel;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}
