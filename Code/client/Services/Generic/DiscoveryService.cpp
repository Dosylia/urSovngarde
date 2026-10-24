#include <TiltedOnlinePCH.h>
#include <PerfScope.h>

#include <Services/DiscoveryService.h>
#include <Games/TES.h>

#include <Games/References.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESNPC.h>
#include <Games/ActorExtension.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/PreUpdateEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/LocationChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>

#include <World.h>

namespace
{
bool IsDefaultModlist(GameList<Mod>& aCurrentModlist) noexcept
{
    static const auto s_defaultModlist = std::to_array<TiltedPhoques::String>(
        {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", 
        "Dragonborn.esm", "_ResourcePack.esl", "SkyrimTogether.esp", "SkyrimTogetherQuestPatches.esp"}
    );

    if (aCurrentModlist.Size() != s_defaultModlist.size())
        return false;

    auto expectedMod = s_defaultModlist.begin();
    for (const auto* pCurrentMod : aCurrentModlist)
    {
        if (pCurrentMod->filename != *expectedMod)
            return false;

        ++expectedMod;
    }

    return true;
}

// A copy this client threw away on its own. See RequestCellReannounce.
std::chrono::steady_clock::time_point s_reannounceAt{};

// Actors this client just moved with MoveTo, and when. See NoteMovedHere.
TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_movedHereAt;
} // namespace

// The server sends a character's spawn to the others only when that character changes cell.
// When this player's own old cell unloads, it can delete a remote copy the server believes we still have.
// Example: player B follows player A through a door; B's spawn reaches A while A's old cell still holds B's copy.
// That old copy is then deleted, and B stays invisible to A.
//
// Announcing the cell again is enough: the server re-sends every character in it, and the spawn lands on an empty slot.
// Nothing else is disturbed, since the server's leave-cell cleanup stops as soon as it finds a player still in the cell.
void DiscoveryService::RequestCellReannounce() noexcept
{
    // A load door tears copies down over several frames, and one announcement after the last of them is wanted.
    // So each request pushes the moment back rather than queuing another.
    s_reannounceAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
}

void DiscoveryService::NoteMovedHere(const uint32_t aFormId) noexcept
{
    s_movedHereAt[aFormId] = std::chrono::steady_clock::now();
}

DiscoveryService::DiscoveryService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    m_preUpdateConnection = m_dispatcher.sink<PreUpdateEvent>().connect<&DiscoveryService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&DiscoveryService::OnConnected>(this);

    EventDispatcherManager::Get()->loadGameEvent.RegisterSink(this);
}

void DiscoveryService::VisitCell(bool aForceTrigger) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    if (pPlayer->GetWorldSpace())
        VisitExteriorCell(aForceTrigger);
    else if (pPlayer->GetParentCellEx())
        VisitInteriorCell(aForceTrigger);

    // exactly how the game does it too
    if (m_pLocation != pPlayer->locationForm)
    {
        m_dispatcher.trigger(LocationChangeEvent());
        m_pLocation = pPlayer->locationForm;
    }
}

void DiscoveryService::VisitExteriorCell(bool aForceTrigger) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto pWorldSpace = pPlayer->GetWorldSpace();

    m_interiorCellId = 0;

    const TES* pTES = TES::Get();
    const uint32_t worldSpaceId = pWorldSpace->formID;
    const GridCellCoords gameCurrentGrid(pTES->currentGridX, pTES->currentGridY);
    const GridCellCoords gameCenterGrid(pTES->centerGridX, pTES->centerGridY);

    if (m_worldSpaceId != worldSpaceId || aForceTrigger)
    {
        DetectGridCellChange(pWorldSpace, true);
        // If the world space changes, then we want to send out a CellChangeEvent out too.
        aForceTrigger = true;
    }
    else if (gameCenterGrid != m_centerGrid)
    {
        DetectGridCellChange(pWorldSpace, false);
    }

    if (gameCurrentGrid != m_currentGrid || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, cellChangeEvent.WorldSpaceId))
        {
            spdlog::error("Failed to find world space id for form id {:X}", pWorldSpace->formID);
            return;
        }

        TESObjectCELL* pCell = pPlayer->GetParentCellEx();
        if (!pCell)
            pCell = ModManager::Get()->GetCellFromCoordinates(gameCurrentGrid.X, gameCurrentGrid.Y, pWorldSpace, false);

        if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
            return;
        }

        cellChangeEvent.CurrentCoords = gameCurrentGrid;

        m_dispatcher.trigger(cellChangeEvent);

        m_currentGrid = gameCurrentGrid;
    }
}

void DiscoveryService::VisitInteriorCell(bool aForceTrigger) noexcept
{
    ResetCachedCellData();

    const uint32_t cellId = PlayerCharacter::Get()->GetParentCellEx()->formID;
    if (m_interiorCellId != cellId || aForceTrigger)
    {
        CellChangeEvent cellChangeEvent{};

        if (!m_world.GetModSystem().GetServerModId(cellId, cellChangeEvent.CellId))
        {
            spdlog::error("Failed to find cell id {:X}", cellId);
            return;
        }

        m_dispatcher.trigger(cellChangeEvent);
        m_interiorCellId = cellId;
    }
}

void DiscoveryService::DetectGridCellChange(TESWorldSpace* aWorldSpace, bool aNewCellGrid) noexcept
{
    GridCellChangeEvent changeEvent{};

    const uint32_t worldSpaceId = aWorldSpace->formID;
    changeEvent.WorldSpaceId = worldSpaceId;

    const TES* pTES = TES::Get();
    const int32_t startGridX = pTES->centerGridX - GridCellCoords::m_gridsToLoad / 2;
    const int32_t startGridY = pTES->centerGridY - GridCellCoords::m_gridsToLoad / 2;

    for (int32_t i = 0; i < GridCellCoords::m_gridsToLoad; ++i)
    {
        for (int32_t j = 0; j < GridCellCoords::m_gridsToLoad; ++j)
        {
            // If it is a new cell grid, don't check for previously loaded cells.
            if (!aNewCellGrid)
            {
                if (GridCellCoords::IsCellInGridCell(m_centerGrid, {startGridX + i, startGridY + j}, false))
                    continue;
            }

            const TESObjectCELL* pCell = ModManager::Get()->GetCellFromCoordinates(startGridX + i, startGridY + j, aWorldSpace, 0);

            if (!pCell)
            {
                spdlog::warn("Cell not found at coordinates ({}, {}) in worldspace {:X}", startGridX + i, startGridY + j, aWorldSpace->formID);
                continue;
            }

            GameId cellId{};
            if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellId))
            {
                spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
                continue;
            }

            changeEvent.Cells.push_back(cellId);
        }
    }

    TESObjectCELL* pCell = PlayerCharacter::Get()->GetParentCellEx();
    if (!pCell)
        pCell = ModManager::Get()->GetCellFromCoordinates(pTES->currentGridX, pTES->currentGridY, aWorldSpace, false);

    if (!m_world.GetModSystem().GetServerModId(pCell->formID, changeEvent.PlayerCell))
    {
        spdlog::error("Failed to find cell id for form id {:X}", pCell->formID);
        return;
    }

    changeEvent.CenterCoords = m_centerGrid = {pTES->centerGridX, pTES->centerGridY};

    // The server ranges every remote actor against this centre.
    // If it disagrees with the grid the player stands in, nearby actors are withheld (see InterpDiag).
    {
        const auto& position = PlayerCharacter::Get()->position;
        const auto standing = GridCellCoords::CalculateGridCellCoords(position.x, position.y);
        spdlog::info("Grid change: reporting centre ({}, {}), game's current grid ({}, {}), standing in ({}, {}) at ({:.0f}, {:.0f}), {} cells sent", pTES->centerGridX, pTES->centerGridY,
                     pTES->currentGridX, pTES->currentGridY, standing.X, standing.Y, position.x, position.y, changeEvent.Cells.size());
    }

    m_dispatcher.trigger(changeEvent);

    m_worldSpaceId = worldSpaceId;
}

void DiscoveryService::VisitForms() noexcept
{
    // Actors briefly lose their 3D when crossing the edge of the loaded cells (a circling dragon does it every few seconds).
    // Removing them right away makes the server destroy and recreate them, so a still existing actor waits out a grace period.
    constexpr auto cRemovalGracePeriod = std::chrono::seconds(5);

    ProcessLists* const pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return;

    const auto now = std::chrono::steady_clock::now();
    const uint64_t visit = ++m_visitCounter;

    static Vector<uint32_t> s_removedForms;
    s_removedForms.clear();

    const auto visitor = [this, visit](TESObjectREFR* apReference, uint32_t aHandle)
    {
        const auto formId = apReference->formID;

        auto it = m_forms.find(formId);
        if (it != m_forms.end() && it->second.pReference != apReference)
        {
            // Same form id but another object (recycled temporary id, or the form was reloaded).
            m_dispatcher.trigger(ActorRemovedEvent(formId));
            m_forms.erase(it);
            it = m_forms.end();
        }

        if (it == m_forms.end())
        {
            m_forms[formId] = KnownForm{apReference, aHandle, visit, {}};

            m_dispatcher.enqueue(ActorAddedEvent(formId));
        }
        else
        {
            it->second.Handle = aHandle;
            it->second.LastSeenVisit = visit;
            it->second.MissingSince = {};
        }
    };

    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
    {
        const uint32_t cHandle = pProcessLists->highActorHandleArray[i];
        TESObjectREFR* const pRefr = TESObjectREFR::GetByHandle(cHandle);
        if (pRefr)
        {
            if (pRefr->GetNiNode())
            {
                visitor(pRefr, cHandle);
            }
        }
    }

    // Not in actor holder
    PlayerCharacter* const pPlayer = PlayerCharacter::Get();
    visitor(pPlayer, pPlayer->GetHandle().handle.iBits);

    // We dispatch removal events first to prevent needless reallocations
    for (auto& [formId, known] : m_forms)
    {
        if (known.LastSeenVisit == visit)
            continue;

        if (known.MissingSince == std::chrono::steady_clock::time_point{})
            known.MissingSince = now;

        const bool isSameObject = known.Handle && TESObjectREFR::GetByHandle(known.Handle) == known.pReference;

        // Copies of actors owned by another player are removed right away once their 3D is gone.
        // The server may already want to respawn them (after a load door), and that request is dropped while the old copy exists.
        // A copy that still has its 3D has only left the high process list for a frame, so it waits out the grace period.
        // Removing it would cancel its server assignment and make it thrash between removed and re-assigned.
        auto* pActor = isSameObject ? Cast<Actor>(known.pReference) : nullptr;
        const bool isRemote = pActor && pActor->GetExtension()->IsRemote();
        const bool isUnloaded = !pActor || !pActor->GetNiNode();

        // Not one this client has just moved: MoveTo drops the 3D for a frame.
        // Removing it then would cancel the assignment the move came from, and it would loop being found, assigned and moved.
        const auto cMoved = s_movedHereAt.find(formId);
        const bool cJustMoved = cMoved != s_movedHereAt.end() && now - cMoved->second < std::chrono::seconds(2);

        if (!isSameObject || (isRemote && isUnloaded && !cJustMoved) || now - known.MissingSince >= cRemovalGracePeriod)
            s_removedForms.push_back(formId);
    }

    for (auto it = s_movedHereAt.begin(); it != s_movedHereAt.end();)
        it = now - it->second >= std::chrono::seconds(2) ? s_movedHereAt.erase(it) : std::next(it);

    for (uint32_t formId : s_removedForms)
    {
        // A conform can remove both the 3D and the high-process handle.
        // Keep the existing discovery entry so rebuilding it does not cancel its assignment.
        const auto cKnown = m_forms.find(formId);
        TESObjectREFR* pLive = cKnown != m_forms.end() && cKnown->second.Handle ? TESObjectREFR::GetByHandle(cKnown->second.Handle) : nullptr;
        if (auto* pActor = Cast<Actor>(pLive); pActor && !pActor->IsDeleted())
        {
            using ReconciliationStage = ActorExtension::ReconciliationStage;
            const auto cStage = pActor->GetExtension()->Reconciliation;
            const auto* pCell = pActor->GetParentCellEx();
            // Finish the disable/enable pair even if the cell starts unloading.
            // Once enabled, an unloaded cell is a real removal.
            if (cStage == ReconciliationStage::WaitingForDisable || (cStage == ReconciliationStage::WaitingFor3D && pCell && pCell->IsAttached()))
            {
                continue;
            }
        }

        // Dispatched exactly once per removal.
        m_forms.erase(formId);
        m_dispatcher.trigger(ActorRemovedEvent(formId));
    }

    // Dispatch all adds
    m_dispatcher.update<ActorAddedEvent>();
}

void DiscoveryService::OnUpdate(const PreUpdateEvent& acUpdateEvent) noexcept
{
    PerfScope perfScope("DiscoveryService::OnUpdate");

    TP_UNUSED(acUpdateEvent);

    if (s_reannounceAt != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() >= s_reannounceAt)
    {
        s_reannounceAt = {};
        spdlog::info("Announcing this cell again: a remote copy was torn down here by a cell unloading, and the server does not know");
        VisitCell(true);
        VisitForms();
        return;
    }

    VisitCell();
    VisitForms();
}

void DiscoveryService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // uGridsToLoad should always be 5, as this is what the server enforces
    auto* pSetting = INISettingCollection::Get()->GetSetting("uGridsToLoad:General");
    if (pSetting && pSetting->data != 5)
    {
        ConnectionErrorEvent errorEvent{};
        errorEvent.ErrorDetail = "{\"error\": \"bad_uGridsToLoad\"}";

        m_world.GetRunner().Trigger(errorEvent);
    }

    VisitCell(true);
}

BSTEventResult DiscoveryService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    spdlog::info("Finished loading, triggering visit cell");

#ifndef SKYRIMVR
    // A VR install always has more plugins (SKSE VR, the address library, ESL support).
    // This warning would fire on every load there, shown as a "connection failed" notification.
    if (!IsDefaultModlist(ModManager::Get()->mods))
    {
        ConnectionErrorEvent errorEvent{};
        errorEvent.ErrorDetail = "{\"error\": \"non_default_install\"}";

        m_world.GetRunner().Trigger(errorEvent);
    }
#endif

    VisitCell(true);

    return BSTEventResult::kOk;
}

void DiscoveryService::ResetCachedCellData() noexcept
{
    m_worldSpaceId = 0;
    m_centerGrid.Reset();
    m_currentGrid.Reset();
}
