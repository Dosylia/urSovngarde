#include <Services/PlayerMarkerService.h>

#include <PerfScope.h>
#include <World.h>
#include <Components.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerWhereabouts.h>

#include <Services/PapyrusService.h>
#include <Services/TransportService.h>

#include <Structs/GridCellCoords.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <ExtraData/ExtraData.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESQuest.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Overrides.h>
#include <Games/TES.h>

namespace
{
// SkyrimTogether.esp's records, at the object ids Tools/VR/player-markers.py gives them.
constexpr uint32_t kQuestId = 0x005000;
constexpr uint32_t kHoldingCellId = 0x005001;
constexpr uint32_t kMapMarkerFirst = 0x005040;

// A marker moves only once its player has moved this far (about 3.5 m).
constexpr float kFollowDistance = 256.f;

// Closer than this (about 30 m), the player is in sight and the marker is put away.
// A loaded body is not enough, since bodies load hundreds of metres away.
constexpr float kHideDistance = 2100.f;

// The world map icon for other players (game marker type 11: a standing stone).
constexpr uint16_t kMapMarkerType = 11;

// The ExtraMapMarker (0x2C) data on a marker reference: name, map flags, icon.
struct MapMarkerData
{
    void* pFullNameVTable;
    BSFixedString Name;
    uint8_t Flags; // 1 visible on the map, 2 can travel to
    uint8_t Pad;
    uint16_t Type;
};

struct MapMarkerExtra : BSExtraData
{
    MapMarkerData* pData;
};

constexpr auto kExtraMapMarker = static_cast<ExtraDataType>(0x2C);
constexpr uint8_t kMapMarkerVisible = 1;

Actor* FindPlayerActor(World& aWorld, uint32_t aPlayerId) noexcept
{
    auto view = aWorld.view<FormIdComponent, PlayerComponent>();
    for (const auto cEntity : view)
    {
        if (view.get<PlayerComponent>(cEntity).Id == aPlayerId)
            return Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
    }
    return nullptr;
}

// The cell for a position: an interior by its id, outdoors the cell under it, loaded as "teleport to player" does.
TESObjectCELL* ResolveCell(World& aWorld, const GameId& acWorldSpaceId, const GameId& acCellId, const glm::vec3& acPosition) noexcept
{
    auto& modSystem = aWorld.GetModSystem();
    if (TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(modSystem.GetGameId(acCellId))))
        return pCell;

    if (!acWorldSpaceId)
        return nullptr;
    TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(modSystem.GetGameId(acWorldSpaceId)));
    if (!pWorldSpace)
        return nullptr;
    const GridCellCoords cGrid = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return pWorldSpace->LoadCell(cGrid.X, cGrid.Y);
}

NiPoint3 ToNiPoint(const glm::vec3& acPosition) noexcept
{
    NiPoint3 point;
    point.x = acPosition.x;
    point.y = acPosition.y;
    point.z = acPosition.z;
    return point;
}

// "Seen, near Whiterun", or the name alone when no place is known.
String MarkerText(const String& acName, const String& acPlace) noexcept
{
    return acPlace.empty() ? acName : acName + ", near " + acPlace;
}
} // namespace

PlayerMarkerService::PlayerMarkerService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&PlayerMarkerService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PlayerMarkerService::OnDisconnected>(this);
    m_joinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&PlayerMarkerService::OnPlayerJoined>(this);
    m_leftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&PlayerMarkerService::OnPlayerLeft>(this);
    m_whereaboutsConnection = aDispatcher.sink<NotifyPlayerWhereabouts>().connect<&PlayerMarkerService::OnWhereabouts>(this);
}

bool PlayerMarkerService::FindForms() noexcept
{
    if (m_formsLooked)
        return m_formsFound;
    m_formsLooked = true;

    Mod* pPlugin = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!pPlugin)
    {
        spdlog::warn("Player markers: SkyrimTogether.esp is not loaded; no players on the map");
        return false;
    }
    m_modBase = static_cast<uint32_t>(pPlugin->standardId) << 24;

    m_pQuest = Cast<TESQuest>(TESForm::GetById(m_modBase | kQuestId));
    m_pHoldingCell = Cast<TESObjectCELL>(TESForm::GetById(m_modBase | kHoldingCellId));
    bool allFound = m_pQuest && m_pHoldingCell;
    for (size_t i = 0; i < kMapMarkers; ++i)
    {
        m_mapSlots[i].pMarker = Cast<TESObjectREFR>(TESForm::GetById(m_modBase | (kMapMarkerFirst + static_cast<uint32_t>(i))));
        allFound = allFound && m_mapSlots[i].pMarker;
    }
    if (!allFound)
    {
        spdlog::warn("Player markers: SkyrimTogether.esp has no player marker records (quest {}, holding cell {}); an older plugin? No players on the map",
                     m_pQuest ? "found" : "missing", m_pHoldingCell ? "found" : "missing");
        return false;
    }

    auto& papyrus = m_world.ctx().at<PapyrusService>();
    if (!papyrus.Get("Quest", "Stop"))
    {
        spdlog::warn("Player markers: the game's Quest.Stop was not found; no players on the map");
        return false;
    }

    spdlog::info("Player markers: forms found in SkyrimTogether.esp (quest {:X})", m_pQuest->formID);
    m_formsFound = true;
    return true;
}

void PlayerMarkerService::OnConnected(const ConnectedEvent&) noexcept
{
    m_connected = true;
}

void PlayerMarkerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_connected = false;
    m_names.clear();
    if (m_formsFound)
    {
        FreeAll();
        StopQuest();
    }
}

void PlayerMarkerService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    m_names[acMessage.PlayerId] = acMessage.Username;
}

void PlayerMarkerService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    m_names.erase(acMessage.PlayerId);
    if (!m_formsFound)
        return;

    for (size_t i = 0; i < kMapMarkers; ++i)
        if (m_mapSlots[i].PlayerId == acMessage.PlayerId)
            Free(m_mapSlots[i], i);
}

void PlayerMarkerService::OnWhereabouts(const NotifyPlayerWhereabouts& acMessage) noexcept
{
    if (!m_connected || !FindForms())
        return;

    PerfScope perfScope("PlayerMarkerService::OnWhereabouts");

    // Every player gets a world map icon only: a compass marker would need a quest the journal lists.
    // A "Fellow travellers" quest still running from an older save is stopped here.
    if (!m_questStarted && m_pQuest->getState() == TESQuest::State::Running)
    {
        m_questStarted = true;
        StopQuest();
        spdlog::info("Player markers: quest Fellow travellers was running from the save; stopped (map icons only)");
    }

    const uint32_t cLocalId = m_world.GetTransport().GetLocalPlayerId();
    const auto isListed = [&](uint32_t aPlayerId) {
        for (const auto& entry : acMessage.Players)
            if (entry.PlayerId == aPlayerId)
                return true;
        return false;
    };

    // Slots of players who left are freed first, so their markers can be reused.
    for (size_t i = 0; i < kMapMarkers; ++i)
        if (m_mapSlots[i].PlayerId && !isListed(m_mapSlots[i].PlayerId))
            Free(m_mapSlots[i], i);

    for (const auto& entry : acMessage.Players)
    {
        if (entry.PlayerId == cLocalId)
            continue;

        size_t index = kMapMarkers;
        for (size_t i = 0; i < kMapMarkers && index == kMapMarkers; ++i)
            if (m_mapSlots[i].PlayerId == entry.PlayerId)
                index = i;
        for (size_t i = 0; i < kMapMarkers && index == kMapMarkers; ++i)
            if (m_mapSlots[i].PlayerId == 0)
                index = i;
        if (index == kMapMarkers)
            continue; // more players than markers: the rest go unmarked

        Slot& slot = m_mapSlots[index];
        slot.PlayerId = entry.PlayerId;

        const auto cName = m_names.find(entry.PlayerId);
        const String cText = MarkerText(cName != m_names.end() ? cName->second : String("A traveller"), entry.Place);
        if (cText != slot.Text)
        {
            slot.Text = cText;
            SetMapMarkerName(slot.pMarker, cText);
        }

        // In sight: no marker. Body loaded but further away: follow the body. Otherwise: follow the server's position.
        if (Actor* pActor = FindPlayerActor(m_world, entry.PlayerId))
        {
            const PlayerCharacter* pPlayer = PlayerCharacter::Get();
            const glm::vec3 cBody(pActor->position.x, pActor->position.y, pActor->position.z);
            // The same room, or the same worldspace outdoors: only then are the two positions comparable.
            const bool cSameSpace = pPlayer && (pPlayer->parentCell == pActor->parentCell ||
                                                (pPlayer->GetWorldSpace() && pPlayer->GetWorldSpace() == pActor->GetWorldSpace()));
            if (cSameSpace && glm::distance(cBody, glm::vec3(pPlayer->position.x, pPlayer->position.y, pPlayer->position.z)) < kHideDistance)
                Park(slot, index);
            else
                FollowBody(slot, pActor);
        }
        else
            FollowServerPosition(slot, index, entry.WorldSpaceId, entry.CellId, glm::vec3(entry.Position.x, entry.Position.y, entry.Position.z));
    }
}

void PlayerMarkerService::StopQuest() noexcept
{
    if (!m_questStarted)
        return;

    ScopedQuestOverride _;
    using Quest = TESQuest;
    PAPYRUS_FUNCTION(void, Quest, Stop);
    s_pStop(m_pQuest);
    m_questStarted = false;
    spdlog::info("Player markers: quest Fellow travellers stopped");
}

void PlayerMarkerService::FollowServerPosition(Slot& aSlot, size_t aIndex, const GameId& acWorldSpaceId, const GameId& acCellId, const glm::vec3& acPosition) noexcept
{
    if (!aSlot.Parked && aSlot.BodyCellId == 0 && aSlot.CellId == acCellId && glm::distance(aSlot.Position, acPosition) < kFollowDistance)
        return;

    TESObjectCELL* pCell = ResolveCell(m_world, acWorldSpaceId, acCellId, acPosition);
    if (!pCell)
        return;

    aSlot.pMarker->MoveTo(pCell, ToNiPoint(acPosition));
    if (aSlot.Parked)
        spdlog::info("Player markers: {:X} follows player {} ({}) to ({:.0f}, {:.0f}, {:.0f}) in cell {:X}", aSlot.pMarker->formID, aSlot.PlayerId, aSlot.Text.c_str(), acPosition.x,
                     acPosition.y, acPosition.z, pCell->formID);
    aSlot.Parked = false;
    aSlot.CellId = acCellId;
    aSlot.BodyCellId = 0;
    aSlot.Position = acPosition;
}

void PlayerMarkerService::FollowBody(Slot& aSlot, Actor* apActor) noexcept
{
    TESObjectCELL* pCell = apActor->parentCell;
    if (!pCell)
        return;

    const glm::vec3 cBody(apActor->position.x, apActor->position.y, apActor->position.z);
    if (!aSlot.Parked && aSlot.BodyCellId == pCell->formID && glm::distance(aSlot.Position, cBody) < kFollowDistance)
        return;

    aSlot.pMarker->MoveTo(pCell, ToNiPoint(cBody));
    if (aSlot.Parked)
        spdlog::info("Player markers: {:X} follows player {} ({}), whose body is here, to ({:.0f}, {:.0f}, {:.0f})", aSlot.pMarker->formID, aSlot.PlayerId, aSlot.Text.c_str(), cBody.x,
                     cBody.y, cBody.z);
    aSlot.Parked = false;
    aSlot.CellId = GameId{};
    aSlot.BodyCellId = pCell->formID;
    aSlot.Position = cBody;
}

void PlayerMarkerService::Park(Slot& aSlot, size_t aIndex) noexcept
{
    if (aSlot.Parked)
        return;

    aSlot.pMarker->MoveTo(m_pHoldingCell, ToNiPoint(glm::vec3(static_cast<float>(aIndex) * 64.f, 0.f, 0.f)));
    aSlot.Parked = true;
    spdlog::info("Player markers: {:X} put away (player {})", aSlot.pMarker->formID, aSlot.PlayerId);
}

void PlayerMarkerService::Free(Slot& aSlot, size_t aIndex) noexcept
{
    Park(aSlot, aIndex);
    aSlot.PlayerId = 0;
    aSlot.Text.clear();
    aSlot.CellId = GameId{};
    aSlot.BodyCellId = 0;
}

void PlayerMarkerService::FreeAll() noexcept
{
    for (size_t i = 0; i < kMapMarkers; ++i)
        if (m_mapSlots[i].PlayerId)
            Free(m_mapSlots[i], i);
}

void PlayerMarkerService::SetMapMarkerName(TESObjectREFR* apMarker, const String& acText) noexcept
{
    auto* pExtra = static_cast<MapMarkerExtra*>(apMarker->extraData.GetByType(kExtraMapMarker));
    if (!pExtra || !pExtra->pData)
    {
        spdlog::warn("Player markers: map marker {:X} has no map marker data", apMarker->formID);
        return;
    }
    pExtra->pData->Name.Set(acText.c_str());
    pExtra->pData->Flags = kMapMarkerVisible; // on the map, never a fast travel destination
    pExtra->pData->Type = kMapMarkerType;
}
