#include "Events/CharacterInteriorCellChangeEvent.h"
#include "Events/CharacterExteriorCellChangeEvent.h"
#include "Events/PlayerLeaveCellEvent.h"

#include <Services/PlayerService.h>
#include <Structs/PluginList.h>
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
#include <Messages/RequestNpcPlugins.h>
#include <Messages/NotifyNpcPluginsDiffer.h>
#include <Messages/NotifyPlayerName.h>
#include <Services/PartyService.h>

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
    , m_npcPluginsConnection(aDispatcher.sink<PacketEvent<RequestNpcPlugins>>().connect<&PlayerService::OnNpcPlugins>(this))
{
}

// An arrival (a load, a door, a fast travel) comes from nowhere yet, from indoors, another worldspace or further than one cell.
// A walk into the next cell is none.
// CharacterService holds back handing the party leader NPCs for a while after an arrival.
bool IsArrival(const CellIdComponent& acPrevious, const GameId& acWorldSpaceId, const GridCellCoords& acCoords) noexcept
{
    return !acPrevious || acPrevious.IsInInteriorCell() || acPrevious.WorldSpaceId != acWorldSpaceId || std::abs(acPrevious.CenterCoords.X - acCoords.X) > 1 ||
           std::abs(acPrevious.CenterCoords.Y - acCoords.Y) > 1;
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

    // A game sends its grid shift before its exterior cell entry, so an arrival outdoors is seen here first.
    if (IsArrival(previous, message.WorldSpaceId, message.CenterCoords))
        pPlayer->MarkCellEnter();
    CellIdComponent cell = CellIdComponent{message.PlayerCell, message.WorldSpaceId, message.CenterCoords};
    pPlayer->SetCellComponent(cell);
    // Until the cell entry that follows says where in its loaded cells the player stands.
    if (!previous || previous.IsInInteriorCell() || previous.WorldSpaceId != message.WorldSpaceId)
        pPlayer->SetStandingCoords(message.CenterCoords);

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
// Called by both exterior handlers: the cell entry moves the range when no grid shift came first (`rangeback`).
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

        const CellIdComponent previous = pPlayer->GetCellComponent();
        // An arrival is a jump from where the player stood, not from the loaded cells' centre, which trails a walk.
        const CellIdComponent cStoodIn = pPlayer->GetStandingCell();

        // A player's range is the game's loaded cells, centred where its last grid shift said.
        // That centre trails the cell the player stands in by up to one, so a walk into the next cell keeps it.
        // Ranged from the cell it stands in, the server would miss what the game still has loaded a cell further back.
        const bool cSameWorld = previous && !previous.IsInInteriorCell() && previous.WorldSpaceId == message.WorldSpaceId;
        auto cell = CellIdComponent{message.CellId, message.WorldSpaceId, cSameWorld ? previous.CenterCoords : message.CurrentCoords};

        if (pPlayer->GetCellComponent())
        {
            m_world.GetDispatcher().trigger(CharacterExteriorCellChangeEvent{pPlayer, entity, message.CellId, message.WorldSpaceId, message.CurrentCoords});
        }

        if (IsArrival(cStoodIn, message.WorldSpaceId, message.CurrentCoords))
            pPlayer->MarkCellEnter();
        pPlayer->SetCellComponent(cell);
        pPlayer->SetStandingCoords(message.CurrentCoords);
        SendPlayerCellChanged(pPlayer);

        // The range moved only when no grid shift came first, from indoors or another worldspace.
        if (cSameWorld)
            return;

        RemoveWhatLeftRange(pPlayer, previous);

        // Sends this player every character in range of where they now are, as the other two cell handlers do.
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

namespace
{
bool SamePlugin(const String& acLeft, const String& acRight) noexcept
{
    return acLeft.size() == acRight.size() &&
           std::equal(acLeft.begin(), acLeft.end(), acRight.begin(), [](const char aLeft, const char aRight) { return std::tolower(static_cast<unsigned char>(aLeft)) == std::tolower(static_cast<unsigned char>(aRight)); });
}

Vector<String> OnlyIn(const Vector<String>& acFrom, const Vector<String>& acNotIn) noexcept
{
    Vector<String> only;
    for (const String& plugin : acFrom)
        if (std::none_of(acNotIn.begin(), acNotIn.end(), [&plugin](const String& acOther) { return SamePlugin(plugin, acOther); }))
            only.push_back(plugin);
    return only;
}

} // namespace

// A game says which of its plugins add or change NPCs, once connected.
// Each other player whose list differs is told, and so is this one: their games roll other people and creatures in the same places.
// A pair is told once per server run for the same difference, so a reconnect does not tell them again.
// A connection sends its list once; another is ignored, so a game cannot have everybody told again and again.
void PlayerService::OnNpcPlugins(const PacketEvent<RequestNpcPlugins>& acMessage) const noexcept
{
    static TiltedPhoques::Set<uint64_t> s_told;

    Player* pPlayer = acMessage.pPlayer;
    if (!pPlayer->GetNpcPlugins().empty())
        return;
    pPlayer->SetNpcPlugins(acMessage.Packet.Plugins);

    for (Player* pOther : m_world.GetPlayerManager())
    {
        if (pOther == pPlayer || pOther->GetNpcPlugins().empty())
            continue;

        Vector<String> onlyHere = OnlyIn(pPlayer->GetNpcPlugins(), pOther->GetNpcPlugins());
        Vector<String> onlyThere = OnlyIn(pOther->GetNpcPlugins(), pPlayer->GetNpcPlugins());
        if (onlyHere.empty() && onlyThere.empty())
            continue;

        // Each side is its name and what only it has, in a set order, so the key is the same whoever reports last.
        std::string here = fmt::format("{}|{}", pPlayer->GetUsername().c_str(), PluginList::Joined(onlyHere).c_str());
        std::string there = fmt::format("{}|{}", pOther->GetUsername().c_str(), PluginList::Joined(onlyThere).c_str());
        if (there < here)
            std::swap(here, there);
        if (!s_told.insert(std::hash<std::string>{}(here + "||" + there)).second)
            continue;

        spdlog::warn("NpcPlugins: '{}' and '{}' do not have the same plugins adding or changing NPCs; only '{}' has {} ({}); only '{}' has {} ({})", pPlayer->GetUsername().c_str(),
                     pOther->GetUsername().c_str(), pPlayer->GetUsername().c_str(), onlyHere.size(), PluginList::Joined(onlyHere).c_str(), pOther->GetUsername().c_str(), onlyThere.size(),
                     PluginList::Joined(onlyThere).c_str());

        NotifyNpcPluginsDiffer toPlayer{};
        toPlayer.Username = pOther->GetUsername();
        toPlayer.OnlyHere = onlyHere;
        toPlayer.OnlyThere = onlyThere;
        pPlayer->Send(toPlayer);

        NotifyNpcPluginsDiffer toOther{};
        toOther.Username = pPlayer->GetUsername();
        toOther.OnlyHere = std::move(onlyThere);
        toOther.OnlyThere = std::move(onlyHere);
        pOther->Send(toOther);
    }
}

// The same player under a new name: character creation, a quest or a mod renamed their character in play.
// The id, the party and every NPC this player runs stay; only the name the others read changes.
void PlayerService::Rename(Player& aPlayer, const String& acName) const noexcept
{
    const String cOld = aPlayer.GetUsername();
    aPlayer.SetUsername(acName);
    if (aPlayer.GetUsername() == cOld)
        return;

    spdlog::info("Rename: player {} '{}' is now '{}', renamed in play", aPlayer.GetId(), cOld.c_str(), aPlayer.GetUsername().c_str());
    NotifyPlayerName notify{};
    notify.PlayerId = aPlayer.GetId();
    notify.Username = aPlayer.GetUsername();
    GameServer::Get()->SendToPlayers(notify, &aPlayer);
    m_world.GetPartyService().BroadcastPlayerList();
}
