#include "Forms/TESObjectCELL.h"
#include <PerfScope.h>
#include "Forms/TESWorldSpace.h"
#include "Services/PapyrusService.h"
#include <Services/PartyService.h>

#include <Services/CharacterService.h>
#include <Services/QuestService.h>
#include <Services/TransportService.h>
#include <Services/InventoryService.h>
#include <Services/DiscoveryService.h>
#include <RecentDeletes.h>
#include <CrashHandler.h>
#include <unordered_set>
#include <CopyRemovalPolicy.h>

#include <Games/References.h>
#include <Games/Misc/SubtitleManager.h>

#include <Forms/TESNPC.h>
#include <Interface/UI.h>
#include <Forms/TESQuest.h>

#include <BranchInfo.h>
#include <Components.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/AnimationSystem.h>
#include <Systems/CacheSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Systems/LeveledNpcSystem.h>

#include <Events/ActorAddedEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/MountEvent.h>
#include <Events/InitPackageEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/DialogueEvent.h>
#include <Events/SubtitleEvent.h>
#include <Events/MoveActorEvent.h>
#include <Events/PartyJoinedEvent.h>

#include <Structs/ActionEvent.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ClashRequest.h>
#include <Messages/NotifyClash.h>
#include <Messages/NotifyArrowNocked.h>
#include <Messages/ArrowNockedRequest.h>
#include <Events/ClashEvent.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Systems/SpawnReveal.h>
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

#include <World.h>
#include <Games/Memory.h>
#include <Forms/ActorValueInfo.h>
#include <Games/TES.h>
#include <AI/AIProcess.h>
#ifdef SKYRIMVR
#include <Games/Skyrim/VRBodySync.h>
#include <Games/Skyrim/Interface/UI.h>

#endif

namespace
{
// Levelled references the owner rolled as another kind of creature: ghost form id -> form id of the copy standing in for it.
// Adopting the wrong creature freezes it or makes it slide, so the owner's creature is spawned here as a copy instead.
// The local reference (the "ghost") is disabled while its copy stands in, and is never assigned to the server.
// A ghost is enabled again when its copy is deleted, on disconnect, or when this client goes away.
// Disable is saved with the reference: a crash while a ghost is disabled leaves that spawn point disabled in the save.
TiltedPhoques::Map<uint32_t, uint32_t> s_ghosts;

// Temporary actors this game made that now belong to another player and are out of view: server id -> local actor.
// The server sends such an actor back as a character without a reference, so every client makes a copy of it.
// The client that made it remembers it here, does not announce it again, and takes it back as that same actor.
// Otherwise one creature becomes two, and an actor/copy pair crashes on travel (`live-copy-abandon`).
// A record lasts until the server removes the character or the connection ends.
struct HandedAway
{
    uint32_t FormId{};
    uint32_t BaseFormId{}; // 0 when the actor was already gone from the game when it was let go
};
TiltedPhoques::Map<uint32_t, HandedAway> s_handedAway;

// Whether this actor is one that was handed away, and as which server character.
bool IsHandedAway(const Actor* apActor, uint32_t& aServerId) noexcept
{
    for (const auto& [serverId, entry] : s_handedAway)
    {
        if (entry.FormId != apActor->formID)
            continue;
        if (entry.BaseFormId != 0 && (!apActor->baseForm || apActor->baseForm->formID != entry.BaseFormId))
            continue;
        aServerId = serverId;
        return true;
    }
    return false;
}

// Form ids of the temporary actors this client made itself: copies of other players and of their creatures.
// Only these are this client's to disable or delete; game-spawned temporary actors can be remote too.
// Disabling a game actor while the game disposes of it crashes in its movement code (SkyrimVR.exe+0x714EB2).
// An id leaves this set as soon as its copy is gone, because the game reuses a deleted actor's form id (`live-temp-reuse`).
// When in doubt an id is removed: a copy left standing is harmless, disabling a game actor is the crash.
std::unordered_set<uint32_t> s_ownCopies;

// Copies this client still had when its last connection ended: form id -> base form id.
// The clean-up on connect deletes them instead of announcing them to the server as new characters.
// The base is kept because the game reuses freed ids; an id also leaves this map when the game removes the actor.
TiltedPhoques::Map<uint32_t, uint32_t> s_lastConnectionCopies;

Actor* RememberOwnCopy(Actor* apActor) noexcept
{
    if (apActor)
        s_ownCopies.insert(apActor->formID);
    return apActor;
}

struct BaseMatch
{
    bool Differs = false;       // the owner's base is not this side's base
    bool Foreign = false;       // and it is another kind of creature
    TESNPC* pOwnerBase = nullptr;
};

// Compares the owner's base with the local one: a differing base is "foreign" when it is another kind of creature.
// Same name means same kind: a levelled list mixes humanoid races freely (Nord and Breton bandits share one graph).
// Only when the names differ does the race decide (Elk vs Deer is foreign; Dragon vs Blood Dragon is not).
// Races come from Actor::race and TESNPC::raceForm; if those disagree for the local actor, the race is not trusted.
BaseMatch CompareBase(World& aWorld, Actor* apActor, const GameId& acRemoteBaseId, const bool aLog) noexcept
{
    BaseMatch match{};
    if (acRemoteBaseId == GameId{})
        return match;

    const uint32_t cRemoteBaseId = aWorld.GetModSystem().GetGameId(acRemoteBaseId);

    const TESNPC* pLocalBase = Cast<TESNPC>(apActor->baseForm);
    if (pLocalBase && pLocalBase->IsTemporary())
        pLocalBase = pLocalBase->GetTemplateBase();

    if (!cRemoteBaseId || !pLocalBase || pLocalBase->formID == cRemoteBaseId)
        return match;

    TESNPC* pRemoteBase = Cast<TESNPC>(TESForm::GetById(cRemoteBaseId));
    if (!pRemoteBase)
        return match;

    match.Differs = true;
    match.pOwnerBase = pRemoteBase;

    const char* pLocalName = pLocalBase->fullName.value.AsAscii();
    const char* pRemoteName = pRemoteBase->fullName.value.AsAscii();

    const bool sameName = std::strcmp(pLocalName ? pLocalName : "", pRemoteName ? pRemoteName : "") == 0;

    const char* pHow;
    if (sameName)
    {
        match.Foreign = false;
        pHow = "name";
    }
    else if (apActor->race && pLocalBase->raceForm.race == apActor->race && pRemoteBase->raceForm.race)
    {
        match.Foreign = pRemoteBase->raceForm.race != apActor->race;
        pHow = "race";
    }
    else
    {
        match.Foreign = true;
        pHow = "name, race unreadable";
    }

    if (aLog)
        spdlog::info("Base form differs on reference {:X}: owner has {:X} ({}), this side {:X} ({}); {} (decided by {})", apActor->formID, cRemoteBaseId,
                     pRemoteName ? pRemoteName : "?", pLocalBase->formID, pLocalName ? pLocalName : "?",
                     match.Foreign ? "another kind of creature" : "same kind, synced normally", pHow);

    return match;
}

// Fallback when no copy could be made: the local creature is adopted but never fed the owner's animation data.
// That data belongs to a graph it does not have; without it the creature slides instead of freezing.
void MarkForeignGraph(World& aWorld, const entt::entity aEntity, Actor* apActor, const GameId& acRemoteBaseId) noexcept
{
    if (!CompareBase(aWorld, apActor, acRemoteBaseId, true).Foreign)
        return;

    if (auto* pAnimation = aWorld.try_get<RemoteAnimationComponent>(aEntity))
        pAnimation->ForeignGraph = true;
    if (auto* pInterpolation = aWorld.try_get<InterpolationComponent>(aEntity))
        pInterpolation->ForeignGraph = true;
}

// The base a copy of the owner's temporary actor is made from: the owner's leveled pick, else the template base.
// A named base whose name differs from the pick's is kept: the pick is only where its stats come from.
// Example: Boethiah Cultist B0E87 takes its stats from Bandit Marauder 39D25 and must not appear as a bandit.
TESNPC* BaseForCopy(World& aWorld, const CharacterSpawnRequest& acMessage) noexcept
{
    TESNPC* pBase = Cast<TESNPC>(TESForm::GetById(aWorld.GetModSystem().GetGameId(acMessage.BaseId)));
    if (acMessage.LeveledNpcPickId == GameId{})
        return pBase;

    TESNPC* pPick = Cast<TESNPC>(TESForm::GetById(aWorld.GetModSystem().GetGameId(acMessage.LeveledNpcPickId)));
    const char* pBaseName = pBase ? pBase->fullName.value.AsAscii() : nullptr;
    const char* pPickName = pPick ? pPick->fullName.value.AsAscii() : nullptr;
    if (pPick && pBase && pPick != pBase && pBaseName && *pBaseName && std::strcmp(pBaseName, pPickName ? pPickName : "") != 0)
    {
        spdlog::info("Copy for server id {:X} made from its own base {:X} ({}), not the levelled pick {:X} ({}) it takes its stats from", acMessage.ServerId, pBase->formID, pBaseName,
                     pPick->formID, pPickName ? pPickName : "?");
        return pBase;
    }
    return pPick ? pPick : pBase;
}

// Spawns the owner's creature and turns the local one into a ghost.
// Returns null when no copy could be made; the caller then adopts the local creature.
Actor* StandInForForeignCreature(Actor* apLocal, TESNPC* apOwnerBase) noexcept
{
    Actor* pCopy = RememberOwnCopy(Actor::Create(apOwnerBase));
    if (!pCopy)
    {
        spdlog::warn("Ghost: could not create a copy of {:X} for reference {:X}, adopting the local creature instead", apOwnerBase->formID, apLocal->formID);
        return nullptr;
    }

    const TESNPC* pLocalBase = Cast<TESNPC>(apLocal->baseForm);
    if (pLocalBase && pLocalBase->IsTemporary())
        pLocalBase = pLocalBase->GetTemplateBase();

    s_ghosts[apLocal->formID] = pCopy->formID;

    // Only a loaded creature is disabled now: one without 3D may be in the middle of being disposed of by the game.
    // Disabling it then crashes in the game's movement code (SkyrimVR.exe+0x714EB2).
    // One that is not loaded is disabled once it loads (ProcessNewEntity).
    const bool cLoaded = apLocal->GetNiNode() != nullptr && !apLocal->IsDeleted();
    if (cLoaded)
        apLocal->Disable();

    spdlog::info("Ghost: reference {:X} rolled as {} ({:X}) here but the owner has {} ({:X}); copy {:X} stands in for it and the local one is {}", apLocal->formID,
                 pLocalBase ? pLocalBase->fullName.value.AsAscii() : "?", pLocalBase ? pLocalBase->formID : 0, apOwnerBase->fullName.value.AsAscii(), apOwnerBase->formID,
                 pCopy->formID, cLoaded ? "disabled" : "not loaded here (disabled once it loads)");
    return pCopy;
}

void EnableGhost(const uint32_t aGhostFormId) noexcept
{
    if (IsProcessExiting())
        return;

    Actor* pGhost = Cast<Actor>(TESForm::GetById(aGhostFormId));
    if (pGhost && pGhost->IsDisabled())
        pGhost->EnableImpl();
    spdlog::info("Ghost: reference {:X} is a normal local creature again{}", aGhostFormId, pGhost ? "" : " (not loaded)");
}

// Called for every temporary actor this client deletes: if it was a stand-in, its ghost comes back.
void ReleaseGhostOf(const uint32_t aCopyFormId) noexcept
{
    for (auto it = s_ghosts.begin(); it != s_ghosts.end(); ++it)
    {
        if (it->second != aCopyFormId)
            continue;
        const uint32_t cGhost = it->first;
        s_ghosts.erase(it);
        EnableGhost(cGhost);
        return;
    }
}

// Removed copies the game still holds handles on: disabled, and freed once the game lets go (CopyRemovalPolicy.h).
// The list never holds more than kMaxWaiting, whatever the game does, so copies cannot pile up.
// RecentDeletes records every deletion, so the crash handler can tell when a register points at a freed copy.
struct WaitingCopy
{
    uint32_t FormId{};
    std::chrono::steady_clock::time_point QueuedAt{};
};

TiltedPhoques::Vector<WaitingCopy> s_waitingCopies;

// Returns the first word at this address, or 0 if reading it faults.
// The caller wraps it in CrashGuard so the crash handler treats a fault in here as expected.
uintptr_t ReadFirstWord(const void* apAddress) noexcept
{
    __try
    {
        return *static_cast<const uintptr_t*>(apAddress);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return 0;
    }
}

//! Whether a pointer from a form lookup is still a real game object: readable, with its vtable inside the game image.
//! Freed memory reused for anything else fails this at once.
//! The read is guarded instead of checked with VirtualQuery, which costs about 10 ms per call on the game's large heap.
bool IsLiveGameObject(const TESForm* apForm) noexcept
{
    if (!apForm)
        return false;

    static const auto s_image = []
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* pNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + pDos->e_lfanew);
        return std::pair<uintptr_t, uintptr_t>{base, base + pNt->OptionalHeader.SizeOfImage};
    }();

    CrashGuard::Enter();
    const uintptr_t vtable = ReadFirstWord(apForm);
    CrashGuard::Leave();

    return vtable >= s_image.first && vtable < s_image.second;
}

uint32_t OutstandingHandles(const Actor* apActor) noexcept
{
    return static_cast<uint32_t>(apActor->handleRefObject.refCount) & 0x3FF;
}

void FreeCopy(Actor* apActor, const uint32_t aFormId, const char* acpWhy) noexcept
{
    const uint32_t refWord = static_cast<uint32_t>(apActor->handleRefObject.refCount);
    RecentDeletes::Record(apActor, aFormId, refWord & 0x3FF, refWord, 0);
    // session-report.py parses this line; keep the colon before the reason.
    spdlog::info("Temporary Remote Deleted {:X}: {}", aFormId, acpWhy);
    apActor->Delete();
    s_ownCopies.erase(aFormId); // the game may give this id to an actor of its own next
}

void RunWaitingCopies() noexcept
{
    if (s_waitingCopies.empty())
        return;

    static std::chrono::steady_clock::time_point s_next;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next)
        return;
    s_next = now + 250ms;

    TiltedPhoques::Vector<WaitingCopy> stillWaiting;
    stillWaiting.reserve(s_waitingCopies.size());

    for (const WaitingCopy& waiting : s_waitingCopies)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(waiting.FormId));
        if (!pActor || pActor->IsDeleted() || !pActor->IsTemporary())
        {
            s_ownCopies.erase(waiting.FormId);
            continue; // gone already, which is the outcome we wanted
        }

        const auto waitedMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - waiting.QueuedAt).count());
        const bool overBound = stillWaiting.size() >= CopyRemovalPolicy::kMaxWaiting;
        const uint32_t handles = OutstandingHandles(pActor);
        const auto reason = CopyRemovalPolicy::ReleaseReason(handles, waitedMs, overBound);

        if (reason != CopyRemovalPolicy::Released::NotYet)
        {
            FreeCopy(pActor, waiting.FormId, CopyRemovalPolicy::Describe(reason));

            // A copy freed while the game still holds it is the case the wait exists to prevent, so it gets its own warning.
            if (CopyRemovalPolicy::FreedWhileHeld(reason))
                spdlog::warn("CopyFreedHeld: {:X} freed after {} ms with {} handle(s) still outstanding -- {}",
                             waiting.FormId, waitedMs, handles, CopyRemovalPolicy::Describe(reason));
            continue;
        }

        stillWaiting.push_back(waiting);
    }

    s_waitingCopies.swap(stillWaiting);
}

void ReleaseAllGhosts() noexcept
{
    for (const auto& [ghost, copy] : s_ghosts)
        EnableGhost(ghost);
    s_ghosts.clear();
}
//! The values a player's fresh copy starts with: visible, and health never below a quarter of its maximum.
//! The server's stored values for a respawning player can be the negative health from the death.
//! A copy that starts that low goes down before the owner's real health arrives; the owner's game decides deaths.
ActorValues StandingValues(const ActorValues& acValues, const uint32_t aFormId) noexcept
{
    ActorValues values = acValues;
    // Invisibility (54) travels with every other value; a copy born invisible stays invisible until a reconnect.
    if (auto inv = values.ActorValuesList.find(ActorValueInfo::kInvisibility); inv != values.ActorValuesList.end() && inv->second != 0.f)
    {
        spdlog::info("Remote player {:X} copy would have spawned invisible ({:.2f}); spawned visible instead", aFormId, inv->second);
        inv.value() = 0.f;
    }
    auto it = values.ActorValuesList.find(ActorValueInfo::kHealth);
    if (it == values.ActorValuesList.end())
        return values;
    float floor = 25.f;
    if (const auto max = values.ActorMaxValuesList.find(ActorValueInfo::kHealth); max != values.ActorMaxValuesList.end() && max->second > 0.f)
        floor = std::max(floor, max->second * 0.25f);
    if (it->second < floor)
    {
        spdlog::info("Remote player {:X} copy would have spawned with the owner's health {:.0f} (a copy that starts down never gets up); started at {:.0f} instead", aFormId, it->second, floor);
        it.value() = floor;
    }
    return values;
}

// Measurement: an NPC this game runs that travels while it lies down (bleedout, essential down, knocked down).
// Players saw downed essential NPCs slide along the floor; this says on whose game, and in which state.
// Called about ten times a second per NPC; one line per NPC at most every five seconds.
void NoteDownedTravel(uint32_t aFormId) noexcept
{
    struct Down
    {
        NiPoint3 From;
        std::chrono::steady_clock::time_point Since;
        std::chrono::steady_clock::time_point SaidAt;
    };
    static TiltedPhoques::Map<uint32_t, Down> s_down;

    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    const uint32_t cFlags = pActor ? pActor->actorState.flags1 : 0;
    const uint32_t cLife = (cFlags >> 21) & 0xF;  // ACTOR_LIFE_STATE: 3 unconscious, 7 essential down, 8 bleedout
    const uint32_t cKnock = (cFlags >> 25) & 0x7; // KNOCK_STATE_ENUM: 0 normal, anything else on the way down or up
    const bool cDown = pActor && !pActor->actorState.IsDeadOrDying() && (pActor->actorState.IsBleedingOut() || cLife == 3 || cKnock != 0);
    if (!cDown)
    {
        s_down.erase(aFormId);
        return;
    }

    const auto cNow = std::chrono::steady_clock::now();
    auto [it, fresh] = s_down.try_emplace(aFormId, Down{pActor->position, cNow, {}});
    if (fresh || cNow - it->second.Since < 1s)
        return;
    const NiPoint3 cAt = pActor->position;
    const float dx = cAt.x - it->second.From.x, dy = cAt.y - it->second.From.y, dz = cAt.z - it->second.From.z;
    const float cMoved = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (cMoved > 30.f && cNow - it->second.SaidAt > 5s)
    {
        it.value().SaidAt = cNow;
        spdlog::info("DownedTravel: own NPC {:X} moved {:.0f} units in {} ms while down (life state {}, knock state {}), now at ({:.0f}, {:.0f}, {:.0f})", aFormId, cMoved,
                     std::chrono::duration_cast<std::chrono::milliseconds>(cNow - it->second.Since).count(), cLife, cKnock, cAt.x, cAt.y, cAt.z);
    }
    it.value().From = cAt;
    it.value().Since = cNow;
}

// Whether the game's equip code can run on this actor: an AI process with its middle-high data, as actors loaded here have.
bool HasLoadedProcess(const Actor* apActor) noexcept
{
    return apActor && apActor->currentProcess && apActor->currentProcess->middleProcess;
}

// Whether an actor is in this player's loaded area: it has its 3D and its cell is attached.
bool IsActorLoadedHere(Actor* apActor) noexcept
{
    const TESObjectCELL* pCell = apActor ? apActor->GetParentCellEx() : nullptr;
    return pCell && pCell->IsAttached() && apActor->GetNiNode();
}

// Whether a place is in this player's loaded area: an interior cell attached here, or an exterior cell of the player's
// worldspace within the loaded grid (uGridsToLoad 5, which the server enforces: two cells either side of the centre).
bool IsPlaceLoadedHere(World& aWorld, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) noexcept
{
    if (!acWorldSpaceId)
    {
        const auto* pCell = Cast<TESObjectCELL>(TESForm::GetById(aWorld.GetModSystem().GetGameId(acCellId)));
        return pCell && pCell->IsAttached();
    }

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const TESWorldSpace* pPlayerWorldSpace = pPlayer ? pPlayer->GetWorldSpace() : nullptr;
    const TES* pTES = TES::Get();
    if (!pPlayerWorldSpace || !pTES || pPlayerWorldSpace->formID != aWorld.GetModSystem().GetGameId(acWorldSpaceId))
        return false;

    const GridCellCoords cCoordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
    return std::abs(cCoordinates.X - pTES->centerGridX) <= 2 && std::abs(cCoordinates.Y - pTES->centerGridY) <= 2;
}
} // namespace

CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_referenceAddedConnection = m_dispatcher.sink<ActorAddedEvent>().connect<&CharacterService::OnActorAdded>(this);
    m_referenceRemovedConnection = m_dispatcher.sink<ActorRemovedEvent>().connect<&CharacterService::OnActorRemoved>(this);

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this);
    m_actionConnection = m_dispatcher.sink<ActionEvent>().connect<&CharacterService::OnActionEvent>(this);

    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&CharacterService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&CharacterService::OnDisconnected>(this);

    m_assignCharacterConnection = m_dispatcher.sink<AssignCharacterResponse>().connect<&CharacterService::OnAssignCharacter>(this);
    m_characterSpawnConnection = m_dispatcher.sink<CharacterSpawnRequest>().connect<&CharacterService::OnCharacterSpawn>(this);
    m_referenceMovementSnapshotConnection = m_dispatcher.sink<ServerReferencesMoveRequest>().connect<&CharacterService::OnReferencesMoveRequest>(this);
    m_factionsConnection = m_dispatcher.sink<NotifyFactionsChanges>().connect<&CharacterService::OnFactionsChanges>(this);
    m_ownershipTransferConnection = m_dispatcher.sink<NotifyOwnershipTransfer>().connect<&CharacterService::OnOwnershipTransfer>(this);
    m_removeCharacterConnection = m_dispatcher.sink<NotifyRemoveCharacter>().connect<&CharacterService::OnRemoveCharacter>(this);
    m_clashConnection = m_dispatcher.sink<NotifyClash>().connect<&CharacterService::OnNotifyClash>(this);
    m_arrowNockedConnection = m_dispatcher.sink<NotifyArrowNocked>().connect<&CharacterService::OnNotifyArrowNocked>(this);

    m_mountConnection = m_dispatcher.sink<MountEvent>().connect<&CharacterService::OnMountEvent>(this);
    m_notifyMountConnection = m_dispatcher.sink<NotifyMount>().connect<&CharacterService::OnNotifyMount>(this);

    m_initPackageConnection = m_dispatcher.sink<InitPackageEvent>().connect<&CharacterService::OnInitPackageEvent>(this);
    m_newPackageConnection = m_dispatcher.sink<NotifyNewPackage>().connect<&CharacterService::OnNotifyNewPackage>(this);

    m_notifyRespawnConnection = m_dispatcher.sink<NotifyRespawn>().connect<&CharacterService::OnNotifyRespawn>(this);
    m_beastFormChangeConnection = m_dispatcher.sink<BeastFormChangeEvent>().connect<&CharacterService::OnBeastFormChange>(this);

    m_addExperienceEventConnection = m_dispatcher.sink<AddExperienceEvent>().connect<&CharacterService::OnAddExperienceEvent>(this);
    m_syncExperienceConnection = m_dispatcher.sink<NotifySyncExperience>().connect<&CharacterService::OnNotifySyncExperience>(this);

    m_dialogueEventConnection = m_dispatcher.sink<DialogueEvent>().connect<&CharacterService::OnDialogueEvent>(this);
    m_dialogueSyncConnection = m_dispatcher.sink<NotifyDialogue>().connect<&CharacterService::OnNotifyDialogue>(this);

    m_subtitleEventConnection = m_dispatcher.sink<SubtitleEvent>().connect<&CharacterService::OnSubtitleEvent>(this);
    m_subtitleSyncConnection = m_dispatcher.sink<NotifySubtitle>().connect<&CharacterService::OnNotifySubtitle>(this);

    m_actorTeleportConnection = m_dispatcher.sink<NotifyActorTeleport>().connect<&CharacterService::OnNotifyActorTeleport>(this);

    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&CharacterService::OnPartyJoinedEvent>(this);
}

// A copy just made for this character has this form id, so no other character may still point at it.
// The game reuses a deleted actor's id, and a character whose copy is gone would otherwise drive the new actor.
void CharacterService::ClaimCopyId(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    auto view = m_world.view<RemoteComponent>();
    for (const auto other : view)
    {
        if (other == aEntity)
            continue;
        auto& remoteComponent = view.get<RemoteComponent>(other);
        if (remoteComponent.CachedRefId != aFormId)
            continue;

        spdlog::warn("Copy id {:X} now belongs to server character {:X}; server character {:X} still pointed at it (its own copy is gone) and "
                     "gets a new one", aFormId, m_world.get<RemoteComponent>(aEntity).Id, remoteComponent.Id);
        remoteComponent.CachedRefId = 0;
        if (const auto* pFormId = m_world.try_get<FormIdComponent>(other); pFormId && pFormId->Id == aFormId)
            m_world.remove<FormIdComponent>(other);
    }
}

void CharacterService::DeleteRemoteEntityComponents(entt::entity aEntity) const noexcept
{
    m_world.remove<FaceGenComponent, InterpolationComponent, RemoteAnimationComponent, RemoteComponent, CacheComponent, WaitingFor3D, PlayerComponent>(aEntity);
}

void CharacterService::DeclineOwnership(const uint32_t aServerId, const uint32_t aOwnershipEpoch) const noexcept
{
    RequestOwnershipTransfer request{};
    request.ServerId = aServerId;
    request.OwnershipEpoch = aOwnershipEpoch;
    request.Reason = OwnershipReleaseReason::DeclineGrant;
    m_transport.Send(request);
}

void CharacterService::ReconcileActorData(
    const entt::entity aEntity, Actor* apActor, const uint32_t aOwnershipEpoch, const ActorData& acActorData, const bool aApplyInventory, const bool aIsLocalOwner) noexcept
{
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity))
    {
        pWaitingFor3D->SpawnRequest.InitialActorValues = acActorData.InitialActorValues;
        pWaitingFor3D->SpawnRequest.InventoryContent = acActorData.InitialInventory;
        pWaitingFor3D->SpawnRequest.IsDead = acActorData.IsDead;
        pWaitingFor3D->SpawnRequest.IsWeaponDrawn = acActorData.IsWeaponDrawn;
        pWaitingFor3D->SpawnRequest.OwnershipEpoch = aOwnershipEpoch;
    }

    if (!apActor)
        return;

    apActor->SetActorValues(acActorData.InitialActorValues);

    if (aApplyInventory)
    {
        // Equipping and unequipping read the actor's AI process: an actor not loaded here has none, and the game crashes on it.
        if (!HasLoadedProcess(apActor))
        {
            m_pendingInventories[apActor->formID] = acActorData.InitialInventory;
            spdlog::info("Inventory of {:X} kept until the actor is loaded here (no AI process yet)", apActor->formID);
        }
        else
        {
            m_pendingInventories.erase(apActor->formID);
            const Inventory currentInventory = apActor->GetActorInventory();
            if (currentInventory.Entries != acActorData.InitialInventory.Entries || currentInventory.CurrentMagicEquipment != acActorData.InitialInventory.CurrentMagicEquipment)
                apActor->SetActorInventory(acActorData.InitialInventory);
        }
    }

    if (apActor->IsDead() != acActorData.IsDead)
    {
        if (acActorData.IsDead)
            apActor->Kill();
        // Respawn resets the reference, which re-rolls a leveled actor and reloads its 3D.
        // The owner would then find it again as a new actor, so it keeps its local death and the death sync reports it.
        else if (aIsLocalOwner)
            spdlog::warn("Keeping actor {:X} dead although the server has it alive", apActor->formID);
        else
            apActor->Respawn();
    }

    if (aIsLocalOwner)
    {
        // A remote draw correction may still be queued when an ownership grant arrives.
        m_weaponDrawUpdates.erase(apActor->formID);

        if (apActor->actorState.IsWeaponDrawn() != acActorData.IsWeaponDrawn)
            apActor->SetWeaponDrawnEx(acActorData.IsWeaponDrawn);
    }
    else
        m_weaponDrawUpdates[apActor->formID] = {acActorData.IsWeaponDrawn};
}

// Sends, per kind of item (same base, same extra data), the count difference as an ordinary inventory change.
// The server applies it to its copy and relays it to the other players.
// Used for a follower whose gear another game overwrote on the server while it owned her.
void CharacterService::SendInventoryDifference(Actor* apActor, const uint32_t aServerId, const uint32_t aOwnershipEpoch, const Inventory& acServerInventory) const noexcept
{
    if (!apActor || !m_transport.IsConnected())
        return;

    const Inventory mine = apActor->GetActorInventory();
    const auto countIn = [](const Inventory& acInventory, const Inventory::Entry& acKind)
    {
        int32_t count = 0;
        for (const auto& entry : acInventory.Entries)
            if (entry.CanBeMerged(acKind))
                count += entry.Count;
        return count;
    };

    TiltedPhoques::Vector<Inventory::Entry> done;
    const auto alreadyDone = [&done](const Inventory::Entry& acKind)
    { return std::any_of(done.begin(), done.end(), [&acKind](const Inventory::Entry& acDone) { return acDone.CanBeMerged(acKind); }); };

    int32_t added = 0, removed = 0;
    for (const Inventory* pSide : {&mine, &acServerInventory})
    {
        for (const auto& kind : pSide->Entries)
        {
            if (alreadyDone(kind))
                continue;
            done.push_back(kind);
            const int32_t difference = countIn(mine, kind) - countIn(acServerInventory, kind);
            if (difference == 0)
                continue;

            RequestInventoryChanges request;
            request.ServerId = aServerId;
            request.OwnershipEpoch = aOwnershipEpoch;
            request.Item = kind;
            request.Item.Count = difference;
            request.Drop = false;
            request.UpdateClients = true;
            m_transport.Send(request);
            if (difference > 0)
                ++added;
            else
                ++removed;
        }
    }

    spdlog::info("Follower {:X}: the server's copy of her inventory put right ({} kinds of item added there, {} taken away)", apActor->formID, added, removed);
}

bool CharacterService::RequestOwnership(const uint32_t aFormId, const uint32_t aServerId, const entt::entity aEntity) const noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
    {
        spdlog::warn("Cannot request ownership of actor {:X} because its form {:X} is unavailable", aServerId, aFormId);
        return false;
    }

    ActorExtension* pExtension = pActor->GetExtension();
    if (pExtension->IsRemotePlayer())
    {
        spdlog::warn("Cannot request ownership of remote player actor {:X}", aServerId);
        return false;
    }

    if (pActor->IsPlayerSummon())
    {
        spdlog::warn("Cannot request ownership of remote player summon {:X}", aServerId);
        return false;
    }

    const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity);
    if (!pRemoteComponent || pRemoteComponent->Id != aServerId || pRemoteComponent->OwnershipEpoch == 0)
        return false;

    RequestOwnershipClaim request;
    request.ServerId = aServerId;
    request.ExpectedOwnershipEpoch = pRemoteComponent->OwnershipEpoch;
    // The server lets a player claim their own follower whoever leads the party (see CanClaimOwnership).
    request.Follower = pActor->IsPlayerTeammate() && !pActor->IsTemporary();

    if (!m_transport.Send(request))
        return false;

    return true;
}

void CharacterService::DeleteTempActor(const uint32_t aFormId) noexcept
{
    if (IsProcessExiting())
        return;

    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (pActor && ((pActor->formID & 0xFF000000) == 0xFF000000))
    {
        const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
        const uint32_t handles = refWord & 0x3FF;

        // Freeing a copy the game still holds is a use-after-free.
        // It is disabled and waits like a copy whose cell unloaded, until the game lets go of it (RunWaitingCopies).
        if (CopyRemovalPolicy::OnRemoved(handles, s_waitingCopies.size()) == CopyRemovalPolicy::Action::WaitDisabled)
        {
            spdlog::info("Temporary Remote Held {:X}: {} handle(s) still on it, disabled and queued ({} of {} slots used); the server removed it",
                         aFormId, handles, s_waitingCopies.size() + 1, CopyRemovalPolicy::kMaxWaiting);
            pActor->Disable();
            s_waitingCopies.push_back(WaitingCopy{aFormId, std::chrono::steady_clock::now()});
            ReleaseGhostOf(aFormId);
            return;
        }
        if (handles != 0)
            spdlog::warn("CopyFreedHeld: {:X} freed immediately with {} handle(s) still outstanding -- the waiting list was full ({} slots)", aFormId, handles,
                         CopyRemovalPolicy::kMaxWaiting);

        RecentDeletes::Record(pActor, aFormId, handles, refWord, RecentDeletes::kOnServersWord);
        pActor->Delete();
        s_ownCopies.erase(aFormId); // the game may give this id to an actor of its own next
        spdlog::info("\tDeleted actor {:X}", aFormId);
        ReleaseGhostOf(aFormId);
    }
}

void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));

    if (acEvent.FormId == 0x14)
    {
        pActor->GetExtension()->SetPlayer(true);
    }

    entt::entity entity;

    const auto view = m_world.view<RemoteComponent>();
    // Matches the id and, for a copy this client made, the base too: the game reuses ids (see ClaimCopyId).
    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [&acEvent, view, pActor](entt::entity entity)
        {
            auto& remoteComponent = view.get<RemoteComponent>(entity);
            if (remoteComponent.CachedRefId != acEvent.FormId)
                return false;
            if (remoteComponent.CachedBaseId != 0 && (!pActor || !pActor->baseForm || pActor->baseForm->formID != remoteComponent.CachedBaseId))
                return false;
            return true;
        });

    if (it != std::end(view))
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));
        pActor->GetExtension()->SetRemote(true);

        entity = *it;
    }
    else
        entity = m_world.create();

    m_world.emplace_or_replace<FormIdComponent>(entity, acEvent.FormId);
    m_world.emplace_or_replace<EarlyAnimationBufferComponent>(entity);

    ProcessNewEntity(entity);
}

void CharacterService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    if (auto* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId)))
    {
        // GetExtension() returns null for an actor this client did not allocate, and this runs during the game's teardown.
        if (ActorExtension* pExtension = pActor->GetExtension())
            pExtension->Reconciliation = ActorExtension::ReconciliationStage::None;
    }

    m_pendingLeveledConforms.erase(acEvent.FormId);
    m_conformEquipment.erase(acEvent.FormId);

    auto view = m_world.view<FormIdComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.FormId](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == formId; });

    if (entityIt == view.end())
    {
        // Not an error: the game removes many actors this client never tracked, dozens at once on a cell change.
        spdlog::debug("Actor removed that was not tracked here: {:X}", acEvent.FormId);
        return;
    }

    const auto cId = *entityIt;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    m_world.remove<EarlyAnimationBufferComponent>(cId);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

#ifdef SKYRIMVR
    VRBodySync::ClearRemotePose(acEvent.FormId);
#endif

    spdlog::info("Actor removed, form id: {:X}", acEvent.FormId);
}

void CharacterService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
#ifdef SKYRIMVR
    SendClashes();
#endif
    {
        PerfScope perfScope("CharacterService::RunSpawnUpdates");
        RunSpawnUpdates();
    }
    {
        PerfScope perfScope("CharacterService::RunLocalUpdates");
        RunLocalUpdates();
    }
    {
        PerfScope perfScope("CharacterService::RunEnemyMeterUpdates");
        RunEnemyMeterUpdates();
    }
    {
        PerfScope perfScope("CharacterService::RunRemotePlayerDiag");
        RunRemotePlayerDiag();
        RunOrphanedRemoteDiag();
    }
    {
        PerfScope perfScope("CharacterService::RunWeaponTouch");
        RunWeaponTouch();
    }
    RunWaitingCopies();
    SpawnReveal::Tick();
    RunFollowerReclaims();

    {
        PerfScope perfScope("CharacterService::RunFactionsUpdates");
        RunFactionsUpdates();
    }
    {
        PerfScope perfScope("CharacterService::RunRemoteUpdates");
        RunRemoteUpdates();
    }
    {
        PerfScope perfScope("CharacterService::RunExperienceUpdates");
        RunExperienceUpdates();
    }
    {
        PerfScope perfScope("CharacterService::ApplyCachedWeaponDraws");
        ApplyCachedWeaponDraws(acUpdateEvent);
        ApplyPendingInventories();
    }
    {
        PerfScope perfScope("CharacterService::ProcessLeveledConforms");
        ProcessLeveledConforms();
    }
}

void CharacterService::OnConnected(const ConnectedEvent& acConnectedEvent) const noexcept
{
    // Go through all the forms that were previously detected
    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    Vector<entt::entity> entities(view.begin(), view.end());

    for (auto entity : entities)
    {
        auto& formIdComponent = m_world.get<FormIdComponent>(entity);

        // A copy from the previous connection: the server sends its character again in a moment, so this one goes.
        // The game has been running it since the disconnect, so it is freed like a copy the server removes.
        if (const auto leftover = s_lastConnectionCopies.find(formIdComponent.Id); leftover != s_lastConnectionCopies.end())
        {
            const uint32_t cBaseId = leftover->second;
            s_lastConnectionCopies.erase(leftover);

            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (pActor && !pActor->IsDeleted() && pActor->IsTemporary() && pActor->baseForm && pActor->baseForm->formID == cBaseId)
            {
                const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
                const uint32_t handles = refWord & 0x3FF;
                if (CopyRemovalPolicy::OnRemoved(handles, s_waitingCopies.size()) == CopyRemovalPolicy::Action::WaitDisabled)
                {
                    spdlog::info("Leftover copy {:X} from the last connection: {} handle(s) still on it, disabled and queued ({} of {} slots used)",
                                 formIdComponent.Id, handles, s_waitingCopies.size() + 1, CopyRemovalPolicy::kMaxWaiting);
                    pActor->Disable();
                    s_waitingCopies.push_back(WaitingCopy{formIdComponent.Id, std::chrono::steady_clock::now()});
                }
                else
                {
                    spdlog::info("Leftover copy {:X} from the last connection deleted on connect", formIdComponent.Id);
                    RecentDeletes::Record(pActor, formIdComponent.Id, handles, refWord, RecentDeletes::kOnConnect);
                    pActor->Delete();
                }
                continue;
            }
        }

        // Only this client's own copies are deleted; a temporary actor the game made itself is kept and announced.
        // Deleting a game actor while its AI still runs crashes the game (SkyrimVR.exe+0x683850). See s_ownCopies.
        if (formIdComponent.Id > 0xFF000000 && s_ownCopies.find(formIdComponent.Id) == s_ownCopies.end())
        {
            spdlog::info("Temporary actor {:X} was made by the game, not by this client; kept on connect", formIdComponent.Id);
            ProcessNewEntity(entity);
            continue;
        }

        if (formIdComponent.Id > 0xFF000000)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (pActor)
            {
                const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
                RecentDeletes::Record(pActor, formIdComponent.Id, refWord & 0x3FF, refWord, RecentDeletes::kOnConnect);
                pActor->Delete();
            }
            s_ownCopies.erase(formIdComponent.Id);

            continue;
        }

        ProcessNewEntity(entity);
    }
}

void CharacterService::OnDisconnected(const DisconnectedEvent& acDisconnectedEvent) const noexcept
{
    // The disconnect that comes with quitting arrives while the game is tearing itself down.
    // Touching actors then fires equip events into plugins whose singletons are gone (e.g. Enchantment Art Extender).
    // The process is ending, so the actors are left as they are.
    if (IsProcessExiting())
    {
        spdlog::info("Disconnected while the game is exiting; actors are left alone");
        return;
    }

    auto remoteView = m_world.view<FormIdComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        auto& formIdComponent = remoteView.get<FormIdComponent>(entity);

        auto pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        // GetExtension() returns null for an actor this client did not allocate.
        ActorExtension* pExtension = pActor->GetExtension();
        if (!pExtension)
            continue;

        if (pExtension->IsRemotePlayer())
        {
            const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
            RecentDeletes::Record(pActor, formIdComponent.Id, refWord & 0x3FF, refWord, RecentDeletes::kOnDisconnect);
            pActor->Delete();
        }
        else
            pExtension->SetRemote(false);
    }

    // Every remaining copy now belongs to the game, so s_ownCopies is emptied; their ids will be reused.
    // The ones still standing are remembered for the clean-up on the next connect (s_lastConnectionCopies).
    // Added to, not replaced: a failed connection attempt also reports a disconnection, with s_ownCopies already empty.
    for (const uint32_t cFormId : s_ownCopies)
    {
        const Actor* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        if (pActor && !pActor->IsDeleted() && pActor->baseForm)
            s_lastConnectionCopies[cFormId] = pActor->baseForm->formID;
    }
    s_ownCopies.clear();
    s_handedAway.clear();

    m_world.clear<WaitingForAssignmentComponent, LocalComponent, RemoteComponent>();

    ReleaseAllGhosts();
    for (const auto& [formId, pickFormId] : m_pendingLeveledConforms)
    {
        if (auto* pActor = Cast<Actor>(TESForm::GetById(formId)))
        {
            auto& stage = pActor->GetExtension()->Reconciliation;
            // Don't leave the actor disabled if we disconnect before re-enabling it.
            if (stage == ActorExtension::ReconciliationStage::WaitingForDisable && !pActor->IsDeleted())
                pActor->EnableImpl();

            stage = ActorExtension::ReconciliationStage::None;
        }
    }

    m_pendingLeveledConforms.clear();
    m_conformEquipment.clear();
}

void CharacterService::OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept
{
    spdlog::debug("Received for cookie {:X}, server id {:X}", acMessage.Cookie, acMessage.ServerId);

    auto view = m_world.view<WaitingForAssignmentComponent>();
    const auto itor = std::find_if(std::begin(view), std::end(view), [view, cookie = acMessage.Cookie](auto entity) { return view.get<WaitingForAssignmentComponent>(entity).Cookie == cookie; });

    if (itor == std::end(view))
    {
        spdlog::warn("Never found requested cookie: {}", acMessage.Cookie);
        return;
    }

    const auto cEntity = *itor;
    const bool isCancelled = view.get<WaitingForAssignmentComponent>(cEntity).Cancelled;

    m_world.remove<WaitingForAssignmentComponent>(cEntity);
#if (!IS_MASTER)
    m_world.remove<ReplayedActionsDebugComponent>(cEntity);
#endif

    if (isCancelled)
    {
        if (acMessage.Owner)
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

        if (m_world.valid(cEntity))
            m_world.destroy(cEntity);

        return;
    }

    if (acMessage.OwnershipEpoch == 0)
    {
        spdlog::warn("Ignored assignment for actor {:X} because the server returned an invalid ownership epoch", acMessage.ServerId);
        return;
    }

    const auto formIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    if (!formIdComponent)
    {
        if (acMessage.Owner)
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

        if (m_world.valid(cEntity))
            m_world.destroy(cEntity);

        spdlog::warn("Discarded assignment for actor {:X} because the local entity no longer has a form", acMessage.ServerId);
        return;
    }

    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent->Id));
    if (!pActor)
    {
        if (acMessage.Owner)
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);

        spdlog::warn("Discarded assignment for actor {:X} because form {:X} is unavailable", acMessage.ServerId, formIdComponent->Id);
        m_world.destroy(cEntity);
        return;
    }

    // TODO: how could this possibly trigger?
    // it's kinda interfering with my WaitingFor3D code
    if (acMessage.PlayerId != 0)
        m_world.emplace_or_replace<PlayerComponent>(cEntity, acMessage.PlayerId);

    ActorData actorData{};
    actorData.InitialActorValues = acMessage.AllActorValues;
    actorData.InitialInventory = acMessage.CurrentInventory;
    actorData.IsDead = acMessage.IsDead;
    actorData.IsWeaponDrawn = acMessage.IsWeaponDrawn;

    if (acMessage.Owner)
    {
        spdlog::debug("Received local actor, form id: {:X}", pActor->formID);

        // The server's data is only an echo of this game's own request, and may already be stale.
        // Applying it could Respawn an actor killed meanwhile; local changes reach the server through LocalComponent instead.
        if (pActor->IsDead() != acMessage.IsDead)
            spdlog::info("Local actor {:X} died or revived while its assignment was in flight (server: {}, local: {}), keeping local state", pActor->formID,
                         acMessage.IsDead, pActor->IsDead());
        m_weaponDrawUpdates.erase(pActor->formID);

        // This player's follower coming back: her gear here is the truth, and the server's copy is corrected instead.
        const bool cMyFollower = pActor->IsPlayerTeammate() && !pActor->IsTemporary();
        if (cMyFollower)
            SendInventoryDifference(pActor, acMessage.ServerId, acMessage.OwnershipEpoch, acMessage.CurrentInventory);

        auto& localAnimationComponent = m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);

        if (auto* pEarlyAnimComponent = m_world.try_get<EarlyAnimationBufferComponent>(cEntity))
        {
            for (const auto& action : pEarlyAnimComponent->Actions)
            {
                localAnimationComponent.Append(action);
            }
        }
        m_world.remove<EarlyAnimationBufferComponent>(cEntity);

        auto& localComponent = m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId, acMessage.OwnershipEpoch);
        localComponent.IsDead = acMessage.IsDead;
        localComponent.IsWeaponDrawn = acMessage.IsWeaponDrawn;
        pActor->GetExtension()->SetRemote(false);
    }
    else
    {
        spdlog::info("Received remote actor, form id: {:X}, isweapondrawn: {}", pActor->formID, acMessage.IsWeaponDrawn);

        // The owner has another kind of creature here (the response carries the owner's base): a copy stands in for it.
        // The copy is set up through the deferred path a temporary actor takes; the local reference becomes a ghost.
        if (const BaseMatch match = CompareBase(m_world, pActor, acMessage.BaseId, false); match.Foreign && match.pOwnerBase)
        {
            if (Actor* pCopy = StandInForForeignCreature(pActor, match.pOwnerBase))
            {
                CharacterSpawnRequest spawn{};
                spawn.ServerId = acMessage.ServerId;
                spawn.BaseId = acMessage.BaseId;
                spawn.Position = acMessage.Position;
                spawn.CellId = acMessage.CellId;
                spawn.InitialActorValues = acMessage.AllActorValues;
                spawn.InventoryContent = acMessage.CurrentInventory;
                spawn.ActionsToReplay = acMessage.ActionsToReplay;
                spawn.PlayerId = acMessage.PlayerId;
                spawn.IsDead = acMessage.IsDead;
                spawn.IsWeaponDrawn = acMessage.IsWeaponDrawn;

                const entt::entity copyEntity = m_world.create();
                m_world.emplace<RemoteComponent>(copyEntity, acMessage.ServerId, pCopy->formID, acMessage.OwnershipEpoch);
                pCopy->MoveTo(PlayerCharacter::Get()->parentCell, acMessage.Position);
                pCopy->SetActorValues(acMessage.AllActorValues);
                auto& copyInterpolation = InterpolationSystem::Setup(m_world, copyEntity);
                copyInterpolation.Position = acMessage.Position;
                AnimationSystem::Setup(m_world, copyEntity);
                m_world.emplace<WaitingFor3D>(copyEntity, spawn);
                return;
            }
        }

        m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, formIdComponent->Id, acMessage.OwnershipEpoch);

        pActor->GetExtension()->SetRemote(true);

        m_world.remove<EarlyAnimationBufferComponent>(cEntity);
        InterpolationSystem::Setup(m_world, cEntity);
        AnimationSystem::Setup(m_world, cEntity);
        AnimationSystem::AddActionsForReplay(m_world.get<RemoteAnimationComponent>(cEntity), acMessage.ActionsToReplay);
        // This path adopted a levelled reference without ever seeing the owner's base; the response carries it now.
        MarkForeignGraph(m_world, cEntity, pActor, acMessage.BaseId);

#if (!IS_MASTER)
        m_world.emplace_or_replace<ReplayedActionsDebugComponent>(cEntity, acMessage.ActionsToReplay);
#endif

        // This player's own follower, owned by another player (the server hands her over during a loading screen).
        // Her gear here is the truth: nothing from the server or the other game is applied, and she is not moved away.
        // Moving her would make the two games pull her back and forth; the other game's outfit for her is not ours.
        const bool cMyFollower = pActor->IsPlayerTeammate() && !pActor->IsTemporary();

        ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, actorData, !cMyFollower, false);
        if (pActor->GetExtension()->IsRemotePlayer())
            InventoryService::ApplyHandEquipment(pActor, acMessage.CurrentInventory, true);

        if (cMyFollower)
        {
            // She is asked for only when near this player; a far game cannot run her and would hand her straight back.
            // One farther away is asked for by RunFollowerReclaims once she is near.
            const auto* pPlayer = PlayerCharacter::Get();
            const bool cHere = pActor->GetNiNode() && pPlayer && glm::distance(glm::vec3(pActor->position), glm::vec3(pPlayer->position)) < 2048.f;
            static TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_nextFollowerClaim;
            const auto now = std::chrono::steady_clock::now();
            auto& next = s_nextFollowerClaim[pActor->formID];
            if (cHere && now >= next)
            {
                next = now + std::chrono::seconds(1);
                spdlog::info("Follower {:X} (server id {:X}) came back as another player's; kept as she is here and asked for", pActor->formID, acMessage.ServerId);
                RequestOwnership(pActor->formID, acMessage.ServerId, cEntity);
            }
            else if (!cHere)
                spdlog::info("Follower {:X} (server id {:X}) came back as another player's, not near this player; asked for once she is", pActor->formID,
                             acMessage.ServerId);
        }
        else
            MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);

        // The owner's leveled pick rides the assignment response for actors we discovered ourselves
        ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId, &acMessage.CurrentInventory);
    }
}

// Logs what the owner's appearance turned into here, from both places that make a player copy.
// TESNPC::Create starts from this player's own race and loads the owner's over it.
// A load that did not take leaves a copy of this player's race with no name and no face (e.g. differing mod lists).
static void LogPlayerCopyAppearance(const CharacterSpawnRequest& acMessage, const TESNPC* apNpc) noexcept
{
    if (!acMessage.IsPlayer || !apNpc)
        return;
    const auto* pRace = reinterpret_cast<const TESForm*>(apNpc->raceForm.race); // TESRace derives from TESForm first
    const TESNPC* pOwnBase = Cast<TESNPC>(PlayerCharacter::Get()->baseForm);
    const bool cOwnRace = pOwnBase && reinterpret_cast<const TESForm*>(pOwnBase->raceForm.race) == pRace;
    spdlog::info("Player copy appearance: {} bytes, change flags {:X}, {} face tints; race {:X}{}, name '{}'", acMessage.AppearanceBuffer.size(), acMessage.ChangeFlags,
                 acMessage.FaceTints.Entries.size(), pRace ? pRace->formID : 0, cOwnRace ? " (this player's own race: the owner's may not have loaded)" : "",
                 apNpc->fullName.value.AsAscii());
}

void CharacterService::OnCharacterSpawn(const CharacterSpawnRequest& acMessage) const noexcept
{
    if (acMessage.OwnershipEpoch == 0)
    {
        spdlog::warn("Ignored spawn for actor {:X} because the ownership epoch is invalid", acMessage.ServerId);
        return;
    }

    auto remoteView = m_world.view<RemoteComponent>();
    const auto remoteItor = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ServerId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteItor != std::end(remoteView))
    {
        const entt::entity cExisting = *remoteItor;
        const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cExisting);
        const bool hasActor = m_world.all_of<WaitingFor3D>(cExisting) || (pFormIdComponent && TESForm::GetById(pFormIdComponent->Id));

        if (hasActor)
        {
            // The actor is here, so it is not respawned, but its ownership epoch is refreshed from the re-sent spawn.
            // The server refuses a claim whose epoch does not match its own, so a stale epoch blocks every later claim.
            auto& remoteComponent = remoteView.get<RemoteComponent>(cExisting);
            if (remoteComponent.OwnershipEpoch != acMessage.OwnershipEpoch)
            {
                spdlog::info("Character {:X} is already spawned; its ownership epoch moved {} -> {}", acMessage.ServerId, remoteComponent.OwnershipEpoch, acMessage.OwnershipEpoch);
                remoteComponent.OwnershipEpoch = acMessage.OwnershipEpoch;
            }
            else
                // The server re-sends every character in range at each cell crossing, so this is routine.
                spdlog::debug("Character with remote id {:X} is already spawned.", acMessage.ServerId);

            // Position, cell and death state normally arrive through the movement and death paths, and are left alone.
            // The server withholds updates for characters outside the loaded grid, and re-sends the spawn on return.
            // So the spawn's state is applied only when no update has arrived for this character for a while.
            if (auto* pInterpolation = m_world.try_get<InterpolationComponent>(cExisting))
            {
                constexpr uint64_t cStaleAfterMs = 2000;
                const uint64_t currentTick = m_transport.GetClock().GetCurrentTick();
                const uint64_t newestTick = pInterpolation->TimePoints.empty() ? 0 : pInterpolation->TimePoints.back().Tick;
                const bool stale = newestTick == 0 || (currentTick > newestTick && currentTick - newestTick > cStaleAfterMs);

                if (stale && pFormIdComponent)
                {
                    if (Actor* pActor = Cast<Actor>(TESForm::GetById(pFormIdComponent->Id)))
                    {
                        const glm::vec3 wanted{acMessage.Position.x, acMessage.Position.y, acMessage.Position.z};
                        if (newestTick == 0)
                            spdlog::info("Character {:X} had no update yet and the server has re-sent it; moving the copy to where the server says ({:.0f}, {:.0f}, {:.0f})",
                                         acMessage.ServerId, wanted.x, wanted.y, wanted.z);
                        else
                            spdlog::info("Character {:X} was {} ms without an update and the server has re-sent it; moving the copy to where the server says ({:.0f}, {:.0f}, {:.0f})",
                                         acMessage.ServerId, currentTick - newestTick, wanted.x, wanted.y, wanted.z);

                        // The buffered points predate the silence and would drag the copy back through where it was.
                        pInterpolation->TimePoints.clear();
                        pInterpolation->PositionError = {};
                        pInterpolation->IsExtrapolating = false;
                        pInterpolation->Position = wanted;
                        pActor->ForcePosition(NiPoint3(wanted));

                        // Actor values and weapon state are also sent in range only, so they are stale too.
                        // Under this staleness gate nothing fresher arrives to conflict with the spawn's values.
                        pActor->SetActorValues(acMessage.IsPlayer ? StandingValues(acMessage.InitialActorValues, pActor->formID) : acMessage.InitialActorValues);
                        if (pActor->actorState.IsWeaponDrawn() != acMessage.IsWeaponDrawn)
                            pActor->SetWeaponDrawnEx(acMessage.IsWeaponDrawn);

                        // A death is broadcast to all, but a copy with a stale epoch drops it (ownership transfers are range-filtered).
                        // The spawn is then the only place that still carries the death.
                        if (pActor->IsDead() != acMessage.IsDead)
                        {
                            spdlog::info("Character {:X} came back {} and this copy had it the other way; correcting", acMessage.ServerId, acMessage.IsDead ? "dead" : "alive");
                            acMessage.IsDead ? pActor->Kill() : pActor->Respawn();
                        }

                        // Faction changes are range-filtered too; a copy in the wrong faction squares up to someone and never swings.
                        if (auto* pCache = m_world.try_get<CacheComponent>(cExisting))
                        {
                            if (!(pCache->FactionsContent == acMessage.FactionsContent))
                            {
                                spdlog::info("Character {:X} came back in different factions from the server's; applying them", acMessage.ServerId);
                                pCache->FactionsContent = acMessage.FactionsContent;
                                pActor->SetFactions(pCache->FactionsContent);
                            }
                        }

                        // Inventory is only compared, not applied: SetActorInventory on every return can flood refused Unequip replays.
                        if (!(pActor->GetActorInventory() == acMessage.InventoryContent))
                            spdlog::info("Character {:X} also came back with a different inventory from the server's; not applied, only noticed", acMessage.ServerId);
                    }
                }
            }

            return;
        }

        // The server still counts this character as spawned here, but the local copy is gone, so it is spawned again.
        spdlog::info("Character with remote id {:X} has no actor anymore, spawning it again", acMessage.ServerId);
        DeleteRemoteEntityComponents(cExisting);
        if (m_world.orphan(cExisting))
            m_world.destroy(cExisting);
    }

    Actor* pActor = nullptr;

    std::optional<entt::entity> entity;

    // One of this game's own actors, coming back from the player it was given to. See s_handedAway.
    if (acMessage.FormId == GameId{})
    {
        if (const auto handed = s_handedAway.find(acMessage.ServerId); handed != s_handedAway.end())
        {
            const uint32_t cOwnId = handed->second.FormId;
            s_handedAway.erase(handed);

            Actor* pOwn = Cast<Actor>(TESForm::GetById(cOwnId));
            TESNPC* pSentBase = acMessage.BaseId != GameId{} ? Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.BaseId))) : nullptr;
            const bool cSameKind = pOwn && pSentBase && pOwn->baseForm == pSentBase;

            const auto view = m_world.view<FormIdComponent>();
            const auto itor = std::find_if(std::begin(view), std::end(view), [cOwnId, view](entt::entity aOther) { return view.get<FormIdComponent>(aOther).Id == cOwnId; });

            if (pOwn && !pOwn->IsDeleted() && cSameKind)
            {
                pActor = pOwn;
                entity = itor != std::end(view) ? *itor : m_world.create();
                spdlog::info("HandedAway: server character {:X} is this game's own actor {:X} come back; it is that actor again and no copy is made", acMessage.ServerId, cOwnId);
            }
            else
            {
                spdlog::info("HandedAway: server character {:X} came back, but the actor it was here ({:X}) is {}; a copy is made", acMessage.ServerId, cOwnId,
                             !pOwn ? "not loaded" : pOwn->IsDeleted() ? "deleted" : "another creature now");
                // The actor now carrying that id was held back from the server because of the record, so it is processed now.
                if (pOwn && itor != std::end(view))
                    ProcessNewEntity(*itor);
            }
        }
    }

    if (pActor)
    {
        // Taken back above.
    }
    // Custom forms
    else if (acMessage.FormId == GameId{})
    {
        TESNPC* pNpc = nullptr;

        entity = m_world.create();

        if (acMessage.BaseId != GameId{})
        {
            pNpc = BaseForCopy(m_world, acMessage);

            if (!pNpc)
            {
                spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod, base: {:X}:{:X}, form: {:X}:{:X}", acMessage.BaseId.BaseId, acMessage.BaseId.ModId, acMessage.FormId.BaseId, acMessage.FormId.ModId);
                return;
            }

            pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
        }
        else
        {
            // Players and npcs with temporary ref ids and base ids (usually random events)
            pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            FaceGenSystem::Setup(m_world, *entity, acMessage.FaceTints);
            LogPlayerCopyAppearance(acMessage, pNpc);
        }

        pActor = RememberOwnCopy(Actor::Create(pNpc));
    }
    else
    {
        const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);

        auto waitingView = m_world.view<FormIdComponent, WaitingForAssignmentComponent>();
        const auto waitingItor = std::find_if(std::begin(waitingView), std::end(waitingView), [waitingView, cActorId](auto entity) { return waitingView.get<FormIdComponent>(entity).Id == cActorId; });

        if (waitingItor != std::end(waitingView))
        {
            spdlog::info("Character with form id {:X} already has a spawn request in progress.", cActorId);
            return;
        }

        auto* const pForm = TESForm::GetById(cActorId);
        pActor = Cast<Actor>(pForm);

        // Moving a reference that is still loading crashes; discovery asks for its assignment once it has loaded.
        if (pActor && pActor->IsStillLoading())
        {
            spdlog::info("Skipped spawn for actor {:X} because it is still loading, server id: {:X}", cActorId, acMessage.ServerId);
            return;
        }

        if (!pActor)
        {
            // The reference is not loaded here, yet its owner drives it and it can walk up and attack unseen.
            // A copy of the owner's base stands in, registered as a ghost so the local reference is disabled if it loads.
            TESNPC* pOwnerBase = acMessage.BaseId != GameId{} ? Cast<TESNPC>(TESForm::GetById(World::Get().GetModSystem().GetGameId(acMessage.BaseId))) : nullptr;
            Actor* pCopy = pOwnerBase ? RememberOwnCopy(Actor::Create(pOwnerBase)) : nullptr;
            if (!pCopy)
            {
                spdlog::error("Failed to retrieve Actor {:X}, it will not be spawned, possibly missing mod", cActorId);
                spdlog::error("\tForm : {:X}", pForm ? pForm->formID : 0);
                return;
            }
            s_ghosts[cActorId] = pCopy->formID;
            spdlog::info("Stand-in: reference {:X} is not loaded here; copy {:X} of the owner's {} ({:X}) stands in for it", cActorId, pCopy->formID, pOwnerBase->fullName.value.AsAscii(),
                         pOwnerBase->formID);
            pActor = pCopy;
            entity = m_world.create();
        }

        // Another kind of creature than the owner's: a copy of the owner's stands in, the local one becomes a ghost.
        // The local reference keeps its own entity (a bare FormIdComponent that ProcessNewEntity skips).
        if (const BaseMatch match = CompareBase(m_world, pActor, acMessage.BaseId, false); match.Foreign && match.pOwnerBase)
        {
            if (Actor* pCopy = StandInForForeignCreature(pActor, match.pOwnerBase))
            {
                pActor = pCopy;
                entity = m_world.create();
            }
        }

        if (!entity)
        {
            const auto view = m_world.view<FormIdComponent>();
            const auto itor = std::find_if(std::begin(view), std::end(view), [cActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == cActorId; });

            if (itor != std::end(view))
                entity = *itor;
            else
                entity = m_world.create();
        }
    }

    if (!pActor)
    {
        spdlog::error("Actor object {:X} could not be created.", acMessage.ServerId);
        return;
    }

    // The base name lets two players' logs be compared: a leveled creature can resolve to a different animal on each side.
    const auto* pSpawnedBase = Cast<TESNPC>(pActor->baseForm);
    spdlog::info("CharacterSpawnRequest, server id: {:X}, form id: {:X}, local base {:X} ({})", acMessage.ServerId, pActor->formID, pActor->baseForm ? pActor->baseForm->formID : 0,
                 pSpawnedBase ? pSpawnedBase->fullName.value.AsAscii() : "?");

    // Pending reconciliation re-enables the actor after applying the owner's pick.
    if (pActor->IsDisabled() && pActor->GetExtension()->Reconciliation != ActorExtension::ReconciliationStage::WaitingForDisable)
    {
        spdlog::warn("Disabled actor is being re-enabled: {:X}", pActor->formID);
        pActor->EnableImpl();
    }

    pActor->GetExtension()->SetRemote(true);

    pActor->rotation.x = acMessage.Rotation.x;
    pActor->rotation.z = acMessage.Rotation.y;
    pActor->MoveTo(PlayerCharacter::Get()->parentCell, acMessage.Position);
    pActor->SetActorValues(acMessage.IsPlayer ? StandingValues(acMessage.InitialActorValues, pActor->formID) : acMessage.InitialActorValues);

    pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        pActor->SetIgnoreFriendlyHit(true);
        pActor->SetPlayerRespawnMode();
        // The copy stays essential (it cannot die here), but may get up again once its owner's real health arrives.
        pActor->SetNoBleedoutRecovery(false);
        m_world.emplace_or_replace<PlayerComponent>(*entity, acMessage.PlayerId);
    }

    // A body that is to be dead is killed once it has its 3D, by the WaitingFor3D step below.
    // Killed before that, it dies with nothing played, and that step then finds it dead and leaves it standing.
    if (pActor->IsDead() != acMessage.IsDead && (!acMessage.IsDead || pActor->GetNiNode()))
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

    spdlog::info("Spawn Request Is summon {}", acMessage.IsPlayerSummon);

    if (acMessage.IsPlayerSummon)
    {
        // Prevents remote summons agroing other players.
        pActor->SetCommandingActor(PlayerCharacter::Get()->GetHandle());
    }

    // Static references arrive with their own locally rolled leveled pick; conform to the owner's.
    if (acMessage.FormId != GameId{})
        ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId, &acMessage.InventoryContent);

    auto& newRemote = m_world.emplace_or_replace<RemoteComponent>(*entity, acMessage.ServerId, pActor->formID, acMessage.OwnershipEpoch);
    if (s_ownCopies.find(pActor->formID) != s_ownCopies.end() && pActor->baseForm)
        newRemote.CachedBaseId = pActor->baseForm->formID;
    ClaimCopyId(*entity, pActor->formID);

    auto& interpolationComponent = InterpolationSystem::Setup(m_world, *entity);
    interpolationComponent.Position = acMessage.Position;

    AnimationSystem::Setup(m_world, *entity);
    MarkForeignGraph(m_world, *entity, pActor, acMessage.BaseId);

    m_world.emplace_or_replace<WaitingFor3D>(*entity, acMessage);

    auto& remoteAnimationComponent = m_world.get<RemoteAnimationComponent>(*entity);

    AnimationSystem::AddActionsForReplay(remoteAnimationComponent, acMessage.ActionsToReplay);

#if (!IS_MASTER)
    m_world.emplace_or_replace<ReplayedActionsDebugComponent>(*entity, acMessage.ActionsToReplay);
#endif
}

void CharacterService::OnReferencesMoveRequest(const ServerReferencesMoveRequest& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent>();

    for (const auto& [serverId, update] : acMessage.Updates)
    {
        auto itor = std::find_if(std::begin(view), std::end(view), [serverId = serverId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == serverId; });

        if (itor == std::end(view))
            continue;

        auto& interpolationComponent = view.get<InterpolationComponent>(*itor);
        auto& animationComponent = view.get<RemoteAnimationComponent>(*itor);
        const auto& movement = update.UpdatedMovement;

        InterpolationComponent::TimePoint point;
        point.Tick = acMessage.Tick;
        point.Position = movement.Position;
        point.Rotation = {movement.Rotation.x, 0.f, movement.Rotation.y};
        point.Variables = movement.Variables;
        point.Direction = movement.Direction;
        point.VRPoseData = update.UpdatedVRPose;

        InterpolationSystem::AddPoint(interpolationComponent, point, m_transport.GetMessageReceiveTime());

        for (const auto& action : update.ActionEvents)
        {
            animationComponent.TimePoints.push_back(action);
        }

        // Bounds the replay queue: an actor plays one action a frame, so an unbounded queue falls minutes behind.
        if (const auto* pFormId = m_world.try_get<FormIdComponent>(*itor))
            AnimationSystem::TrimBacklog(animationComponent, pFormId->Id);
        else
            AnimationSystem::TrimBacklog(animationComponent, 0);
    }
}

void CharacterService::OnActionEvent(const ActionEvent& acActionEvent) const noexcept
{
    auto view = m_world.view<LocalAnimationComponent, FormIdComponent>();
    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        auto& localComponent = view.get<LocalAnimationComponent>(*itor);

        localComponent.Append(acActionEvent);
    }
    else if (m_transport.IsOnline())
    {
        // A `LocalAnimationComponent` is not attached yet, but the actor already exists and is running animations

        auto view = m_world.view<FormIdComponent, EarlyAnimationBufferComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = acActionEvent.ActorId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (itor != std::end(view))
        {
            view.get<EarlyAnimationBufferComponent>(*itor).Actions.push_back(acActionEvent);
        }
    }
}

void CharacterService::OnFactionsChanges(const NotifyFactionsChanges& acEvent) const noexcept
{
    // Every remote actor, not only those with a CacheComponent: copies made from a server spawn never get one (live-factions).
    auto view = m_world.view<RemoteComponent, FormIdComponent>();

    for (const auto& [id, factions] : acEvent.Changes)
    {
        const auto itor = std::find_if(std::begin(view), std::end(view), [id = id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

        if (itor == std::end(view))
            continue;

        auto* const pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(*itor).Id));
        if (!pActor)
            continue;

        if (auto* pCache = m_world.try_get<CacheComponent>(*itor))
            pCache->FactionsContent = factions;

        pActor->SetFactions(factions);
        spdlog::info("Factions of remote actor {:X} ({:X}) updated: {} faction(s) from its owner", pActor->formID, id, factions.NpcFactions.size() + factions.ExtraFactions.size());
    }
}

void CharacterService::OnOwnershipTransfer(const NotifyOwnershipTransfer& acMessage) noexcept
{
    if (acMessage.OwnershipEpoch == 0)
    {
        spdlog::warn("Ignored ownership update for actor {:X} because the epoch is invalid", acMessage.ServerId);
        return;
    }

    auto entity = Utils::FindEntityByServerId(acMessage.ServerId);
    if (entity && !m_world.any_of<LocalComponent, RemoteComponent>(*entity))
        entity.reset();

    uint32_t currentEpoch = 0;
    if (entity)
    {
        if (const auto* pLocalComponent = m_world.try_get<LocalComponent>(*entity))
            currentEpoch = pLocalComponent->OwnershipEpoch;
        else if (const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(*entity))
            currentEpoch = pRemoteComponent->OwnershipEpoch;
    }

    if (currentEpoch != 0 && acMessage.OwnershipEpoch <= currentEpoch)
    {
        spdlog::debug("Ignored stale ownership update for actor {:X} at epoch {}; current epoch is {}", acMessage.ServerId, acMessage.OwnershipEpoch, currentEpoch);
        return;
    }

    const bool isLocalOwner = acMessage.OwnerPlayerId == m_transport.GetLocalPlayerId();
    if (!entity)
    {
        // A transfer lacks the form data to recreate an unknown actor.
        // Declining lets the server try another loaded client.
        if (isLocalOwner)
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);
        else
            spdlog::debug("Ignored ownership update for unknown actor {:X} at epoch {}", acMessage.ServerId, acMessage.OwnershipEpoch);
        return;
    }

    const entt::entity cEntity = *entity;
    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(cEntity);
    Actor* pActor = pFormIdComponent ? Cast<Actor>(TESForm::GetById(pFormIdComponent->Id)) : nullptr;

    // Preserve the accepted epoch's pick for actors that still need to be created.
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(cEntity))
        pWaitingFor3D->SpawnRequest.LeveledNpcPickId = acMessage.LeveledNpcPickId;

    // A reference that rolled as another creature here is ghosted behind a copy; taking it over would flip the creature for everyone.
    // So the hand-off is declined and the server keeps looking.
    // Matched by either id: the character is usually bound to the stand-in copy, not to the ghost the map is keyed by.
    if (isLocalOwner && pFormIdComponent)
    {
        const uint32_t cBoundId = pFormIdComponent->Id;
        const auto ghost = std::find_if(s_ghosts.begin(), s_ghosts.end(), [cBoundId](const auto& acEntry) { return acEntry.first == cBoundId || acEntry.second == cBoundId; });
        if (ghost != s_ghosts.end())
        {
            const auto* pCopy = Cast<Actor>(TESForm::GetById(ghost->second));
            const Actor* pLocal = Cast<Actor>(TESForm::GetById(ghost->first));
            const auto* pLocalBase = pLocal ? Cast<TESNPC>(pLocal->baseForm) : nullptr;
            const auto* pCopyBase = pCopy ? Cast<TESNPC>(pCopy->baseForm) : nullptr;
            spdlog::info("Hand-off of {:X} (server id {:X}) declined: a copy of ours stands in for reference {:X} here ({} here, the owner's kind is {}); taking it "
                         "would run our copy as the creature", pFormIdComponent->Id, acMessage.ServerId, ghost->first, pLocalBase ? pLocalBase->fullName.value.AsAscii() : "not loaded",
                         pCopyBase ? pCopyBase->fullName.value.AsAscii() : "?");
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);
            return;
        }
    }

    if (isLocalOwner)
    {
        if (!pFormIdComponent || !pActor || !pActor->GetNiNode())
        {
            uint32_t cachedRefId = pFormIdComponent ? pFormIdComponent->Id : 0;
            if (const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(cEntity))
                cachedRefId = pRemoteComponent->CachedRefId;

            if (pActor)
                pActor->GetExtension()->SetRemote(true);

            m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);
            if (m_world.all_of<RemoteComponent>(cEntity))
                m_world.get<RemoteComponent>(cEntity).OwnershipEpoch = acMessage.OwnershipEpoch;
            else if (cachedRefId != 0)
                m_world.emplace<RemoteComponent>(cEntity, acMessage.ServerId, cachedRefId, acMessage.OwnershipEpoch);

            spdlog::warn("Declined ownership of actor {:X} at epoch {} because the actor is not ready", acMessage.ServerId, acMessage.OwnershipEpoch);
            DeclineOwnership(acMessage.ServerId, acMessage.OwnershipEpoch);
            return;
        }

        // Reconcile while hooks still treat the actor as remote/non-authoritative.
        pActor->GetExtension()->SetRemote(true);
        m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);
        m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, pFormIdComponent->Id, acMessage.OwnershipEpoch);

        // This player's own follower coming back: her gear here is the truth (see OnAssignCharacter).
        const bool cMyFollower = pActor->IsPlayerTeammate() && !pActor->IsTemporary();
        ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, acMessage.CurrentActorData, !cMyFollower, true);
        ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId);

        DeleteRemoteEntityComponents(cEntity);
        CacheSystem::Setup(m_world, cEntity, pActor);
        m_world.emplace_or_replace<LocalAnimationComponent>(cEntity);
        auto& localComponent = m_world.emplace_or_replace<LocalComponent>(cEntity, acMessage.ServerId, acMessage.OwnershipEpoch);
        localComponent.IsDead = acMessage.CurrentActorData.IsDead;
        localComponent.IsWeaponDrawn = acMessage.CurrentActorData.IsWeaponDrawn;

        // LocalComponent is installed only after canonical reconciliation is complete.
        pActor->GetExtension()->SetRemote(false);
        spdlog::info("Gained ownership of actor {:X} (form {:X}) at epoch {}", acMessage.ServerId, pActor ? pActor->formID : 0, acMessage.OwnershipEpoch);
        if (cMyFollower)
            SendInventoryDifference(pActor, acMessage.ServerId, acMessage.OwnershipEpoch, acMessage.CurrentActorData.InitialInventory);
        return;
    }

    if (pActor)
        pActor->GetExtension()->SetRemote(true);

    m_world.remove<LocalAnimationComponent, LocalComponent>(cEntity);

    if (pFormIdComponent)
    {
        m_world.emplace_or_replace<RemoteComponent>(cEntity, acMessage.ServerId, pFormIdComponent->Id, acMessage.OwnershipEpoch);

        // The new owner's snapshots carry on from its own view of the actor, so the previous owner's would only drag it back.
        // Playback starts over from where the actor is now.
        const auto* pOldInterpolation = m_world.try_get<InterpolationComponent>(cEntity);
        const glm::vec3 cPosition = pActor ? glm::vec3(pActor->position) : (pOldInterpolation ? pOldInterpolation->Position : glm::vec3{});
        const bool cForeignGraph = pOldInterpolation && pOldInterpolation->ForeignGraph;
        auto& interpolation = InterpolationSystem::Setup(m_world, cEntity);
        interpolation.Position = cPosition;
        interpolation.ForeignGraph = cForeignGraph;
        if (!m_world.all_of<RemoteAnimationComponent>(cEntity))
            AnimationSystem::Setup(m_world, cEntity);
    }
    else if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(cEntity))
    {
        pRemoteComponent->OwnershipEpoch = acMessage.OwnershipEpoch;
    }

    // This player's own follower given to somebody else (the server does that during a loading screen): her gear here is kept.
    const bool cMyFollower = pActor && pActor->IsPlayerTeammate() && !pActor->IsTemporary();
    ReconcileActorData(cEntity, pActor, acMessage.OwnershipEpoch, acMessage.CurrentActorData, pActor && pActor->GetNiNode() && !cMyFollower, false);
    if (pActor)
        ApplyLeveledNpcPick(pActor, acMessage.LeveledNpcPickId, &acMessage.CurrentActorData.InitialInventory);

    spdlog::info("Actor {:X} is now owned by player {:X} at epoch {}", acMessage.ServerId, acMessage.OwnerPlayerId, acMessage.OwnershipEpoch);
    // She is asked for back only while loaded and near the player; this game cannot run one left behind and would hand her back.
    // One that catches up later is asked for then (OnAssignCharacter, RunFollowerReclaims).
    if (cMyFollower)
    {
        const auto* pPlayer = PlayerCharacter::Get();
        const bool cHere = pActor->GetNiNode() && pPlayer && glm::distance(glm::vec3(pActor->position), glm::vec3(pPlayer->position)) < 2048.f;
        spdlog::info("Follower {:X} (server id {:X}) was given to player {:X}; {}", pActor->formID, acMessage.ServerId, acMessage.OwnerPlayerId,
                     cHere ? "asked for back" : "not near this player, asked for once she is");
        if (cHere)
            RequestOwnership(pActor->formID, acMessage.ServerId, cEntity);
    }
}

void CharacterService::RunFollowerReclaims() const noexcept
{
    // Once a second, asks back each follower of this player that another game runs, once she is loaded and near.
    static std::chrono::steady_clock::time_point s_next;
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < s_next || !m_transport.IsConnected())
        return;
    s_next = cNow + std::chrono::seconds(1);

    const auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    static TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_asked;
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    for (const auto cEntity : view)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
        if (!pActor || pActor->IsTemporary() || !pActor->IsPlayerTeammate() || pActor->IsDead() || !pActor->GetNiNode())
            continue;
        if (glm::distance(glm::vec3(pActor->position), glm::vec3(pPlayer->position)) >= 2048.f)
            continue;
        auto& asked = s_asked[pActor->formID];
        if (cNow < asked)
            continue;
        asked = cNow + std::chrono::seconds(5);
        const auto& remote = view.get<RemoteComponent>(cEntity);
        spdlog::info("Follower {:X} (server id {:X}) is near this player again; asked for", pActor->formID, remote.Id);
        RequestOwnership(pActor->formID, remote.Id, cEntity);
    }
}

void CharacterService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) const noexcept
{
    // Removals go to every player, in range or not, so a record kept for a character nobody has any more ends here.
    if (const auto handed = s_handedAway.find(acMessage.ServerId); handed != s_handedAway.end())
    {
        const uint32_t cOwnId = handed->second.FormId;
        s_handedAway.erase(handed);
        spdlog::info("HandedAway: server character {:X} is gone from the server; actor {:X} is an ordinary actor of this game again", acMessage.ServerId, cOwnId);

        // If it stands here unannounced, it is announced now.
        const auto formView = m_world.view<FormIdComponent>();
        const auto itor = std::find_if(std::begin(formView), std::end(formView), [cOwnId, formView](entt::entity aOther) { return formView.get<FormIdComponent>(aOther).Id == cOwnId; });
        if (itor != std::end(formView))
            ProcessNewEntity(*itor);
    }

    auto view = m_world.view<RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.ServerId, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor != std::end(view))
    {
        // A copy whose 3D has not loaded yet has no FormIdComponent; the spawn put its id on the RemoteComponent.
        // Left behind, it would be taken for an actor of this game's own, and stay without its gear.
        const auto& cRemote = view.get<RemoteComponent>(*itor);
        uint32_t formId = 0;
        if (const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*itor))
            formId = pFormIdComponent->Id;
        else if (Actor* pCopy = Cast<Actor>(TESForm::GetById(cRemote.CachedRefId)); pCopy && pCopy->IsTemporary() &&
                 (!cRemote.CachedBaseId || (pCopy->baseForm && pCopy->baseForm->formID == cRemote.CachedBaseId)))
        {
            formId = cRemote.CachedRefId;
            spdlog::info("Removed before loading: server character {:X} is gone before its copy {:X} loaded; the copy goes too", acMessage.ServerId, formId);
        }

        if (formId)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(formId));
            if (pActor && pActor->IsTemporary())
                CharacterService::DeleteTempActor(formId);
            else if (pActor)
                pActor->GetExtension()->SetRemote(false);
        }

        DeleteRemoteEntityComponents(*itor);
    }
}

void CharacterService::OnNotifyRespawn(const NotifyRespawn& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();
    const auto entityIt = std::find_if(view.begin(), view.end(), [view, id = acMessage.ActorId](auto aEntity) { return view.get<RemoteComponent>(aEntity).Id == id; });

    if (entityIt == view.end())
    {
        spdlog::error("Actor to respawn not found in: {:X}", acMessage.ActorId);
        return;
    }

    const auto cId = *entityIt;

    auto& formIdComponent = view.get<FormIdComponent>(cId);
    CancelServerAssignment(*entityIt, formIdComponent.Id);

    m_world.remove<EarlyAnimationBufferComponent>(cId);

    if (m_world.all_of<FormIdComponent>(cId))
        m_world.remove<FormIdComponent>(cId);

    if (m_world.orphan(cId))
        m_world.destroy(cId);

    RequestRespawn request;
    request.ActorId = acMessage.ActorId;

    m_transport.Send(request);
}

void CharacterService::OnBeastFormChange(const BeastFormChangeEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(view.begin(), view.end(), [view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });

    // The player's entity may not exist yet, or may already be torn down.
    if (it == view.end())
    {
        spdlog::warn("{}: the player has no entity right now; beast form change not sent", __FUNCTION__);
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    uint32_t serverId = serverIdRes.value();

    RequestRespawn request;
    request.ActorId = serverId;

    Actor* pActor = Utils::GetByServerId<Actor>(serverId);
    if (!pActor)
    {
        spdlog::warn(__FUNCTION__ ": could not find actor for server id {:X}", serverId);
        return;
    }

    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
    {
        spdlog::warn(__FUNCTION__ ": could not find actor baseform for server id {:X}", serverId);
        return;
    }

    pNpc->Serialize(&request.AppearanceBuffer);
    request.ChangeFlags = pNpc->GetChangeFlags();

    m_transport.Send(request);
}

void CharacterService::OnMountEvent(const MountEvent& acEvent) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto riderIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.RiderID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (riderIt == std::end(view))
    {
        spdlog::warn("Rider not found, form id: {:X}", acEvent.RiderID);
        return;
    }

    const entt::entity cRiderEntity = *riderIt;
    const auto* pRiderLocalComponent = m_world.try_get<LocalComponent>(cRiderEntity);
    if (!pRiderLocalComponent)
        return;

    const auto mountIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.MountID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (mountIt == std::end(view))
    {
        spdlog::warn("Mount not found, form id: {:X}", acEvent.MountID);
        return;
    }

    const entt::entity cMountEntity = *mountIt;

    uint32_t mountServerId = 0;
    uint32_t mountOwnershipEpoch = 0;
    if (const auto* pMountLocalComponent = m_world.try_get<LocalComponent>(cMountEntity))
    {
        mountServerId = pMountLocalComponent->Id;
        mountOwnershipEpoch = pMountLocalComponent->OwnershipEpoch;
    }
    else if (const auto* pMountRemoteComponent = m_world.try_get<RemoteComponent>(cMountEntity))
    {
        mountServerId = pMountRemoteComponent->Id;
        mountOwnershipEpoch = pMountRemoteComponent->OwnershipEpoch;
    }
    else
        return;

    MountRequest request{};
    request.RiderId = pRiderLocalComponent->Id;
    request.RiderOwnershipEpoch = pRiderLocalComponent->OwnershipEpoch;
    request.MountId = mountServerId;
    request.MountOwnershipEpoch = mountOwnershipEpoch;

    m_transport.Send(request);
}

void CharacterService::OnNotifyMount(const NotifyMount& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();

    const auto riderIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.RiderId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (riderIt == std::end(remoteView))
    {
        spdlog::warn("Rider with remote id {:X} not found.", acMessage.RiderId);
        return;
    }

    auto& riderFormIdComponent = remoteView.get<FormIdComponent>(*riderIt);
    TESForm* pRiderForm = TESForm::GetById(riderFormIdComponent.Id);
    Actor* pRider = Cast<Actor>(pRiderForm);
    if (!pRider)
        return;

    const auto mountIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.MountId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });
    if (mountIt == std::end(remoteView))
    {
        spdlog::warn("Cannot apply mount update because mount {:X} is unavailable", acMessage.MountId);
        return;
    }

    const auto& mountFormIdComponent = remoteView.get<FormIdComponent>(*mountIt);
    Actor* pMount = Cast<Actor>(TESForm::GetById(mountFormIdComponent.Id));
    if (!pMount)
        return;

    const bool cAccepted = pRider->InitiateMountPackage(pMount);
    // Logs that the order arrived and whether the game accepted it (`live-mount`).
    spdlog::info("Mount: copy {:X} of rider {:X} told to ride copy {:X} of mount {:X}; the game {}", pRider->formID, acMessage.RiderId, pMount->formID,
                 acMessage.MountId, cAccepted ? "accepted it" : "refused it");
}

void CharacterService::OnInitPackageEvent(const InitPackageEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto actorIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.ActorId, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (actorIt == std::end(view))
        return;

    const entt::entity cActorEntity = *actorIt;

    std::optional<uint32_t> actorServerIdRes = Utils::GetServerId(cActorEntity);
    if (!actorServerIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    NewPackageRequest request;
    request.ActorId = actorServerIdRes.value();
    if (!m_world.GetModSystem().GetServerModId(acEvent.PackageId, request.PackageId.ModId, request.PackageId.BaseId))
        return;

    m_transport.Send(request);
}

void CharacterService::OnNotifyNewPackage(const NotifyNewPackage& acMessage) const noexcept
{
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.ActorId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Actor for package with remote id {:X} not found.", acMessage.ActorId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    const uint32_t cPackageFormId = World::Get().GetModSystem().GetGameId(acMessage.PackageId);
    const TESForm* pPackageForm = TESForm::GetById(cPackageFormId);
    if (!pPackageForm)
    {
        spdlog::warn("Actor package not found, base id: {:X}, mod id: {:X}", acMessage.PackageId.BaseId, acMessage.PackageId.ModId);
        return;
    }

    TESPackage* pPackage = Cast<TESPackage>(pPackageForm);
    if (!pPackage)
    {
        // The id resolved to a form that is not a package (a mod mismatch); SetPackage must not get null.
        spdlog::warn("Form {:X} is not a package; leaving actor {:X} on the package it has", cPackageFormId, pActor->formID);
        return;
    }

    pActor->SetPackage(pPackage);
}

void CharacterService::OnAddExperienceEvent(const AddExperienceEvent& acEvent) noexcept
{
    m_cachedExperience += acEvent.Experience;
}

void CharacterService::OnNotifySyncExperience(const NotifySyncExperience& acMessage) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    if (PlayerCharacter::LastUsedCombatSkill == -1)
        return;

    pPlayer->AddSkillExperience(PlayerCharacter::LastUsedCombatSkill, acMessage.Experience);
}

void CharacterService::OnDialogueEvent(const DialogueEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    // The game often speaks a line twice in a row, and each copy sent made the other game stop the line and start it again.
    static uint32_t s_lastActor = 0;
    static String s_lastFile;
    static std::chrono::steady_clock::time_point s_lastAt{};
    const auto cNow = std::chrono::steady_clock::now();
    if (acEvent.ActorID == s_lastActor && acEvent.VoiceFile == s_lastFile && cNow - s_lastAt < 1s)
        return;
    s_lastActor = acEvent.ActorID;
    s_lastFile = acEvent.VoiceFile;
    s_lastAt = cNow;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.ActorID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.ActorID);
        return;
    }

    DialogueRequest request{};
    request.ServerId = serverIdRes.value();
    request.SoundFilename = acEvent.VoiceFile;
    if (acEvent.SceneID)
        m_world.GetModSystem().GetServerModId(acEvent.SceneID, request.SceneId);

    // Logged so that a line the other player did not hear can be traced.
    spdlog::info("Dialogue: {:X} (server id {:X}) says {}{}; sent", acEvent.ActorID, request.ServerId, acEvent.VoiceFile.c_str(),
                 acEvent.SceneID ? fmt::format(" in scene {:X}", acEvent.SceneID) : std::string());
    m_transport.Send(request);
}

void CharacterService::OnNotifyDialogue(const NotifyDialogue& acMessage) noexcept
{
    // A member can initiate dialogue with an NPC owned by this client.
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        spdlog::info("Dialogue: server id {:X} says {}, but it has no actor here; not heard", acMessage.ServerId, acMessage.SoundFilename.c_str());
        return;
    }

    // Every client plays a quest's scenes itself, so when the line's scene plays here too, the local copy already voices it.
    // Playing the sender's line would cut that off and end the scene phase early. Menu and other-scene lines still play.
    BGSScene* pScene = pActor->GetCurrentScene();
    if (acMessage.SceneId && pScene && pScene->isPlaying && pScene->formID == m_world.GetModSystem().GetGameId(acMessage.SceneId))
    {
        spdlog::info("Dialogue: {:X} (server id {:X}) says {} in scene {:X}, which plays here too; the local line is kept", pActor->formID, acMessage.ServerId,
                     acMessage.SoundFilename.c_str(), pScene->formID);
        return;
    }

    pActor->StopCurrentDialogue(true);
    pActor->SpeakSound(acMessage.SoundFilename.c_str());
    spdlog::info("Dialogue: {:X} (server id {:X}) says {}; played here", pActor->formID, acMessage.ServerId, acMessage.SoundFilename.c_str());
}

void CharacterService::OnSubtitleEvent(const SubtitleEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
    auto entityIt = std::find_if(view.begin(), view.end(), [view, formId = acEvent.SpeakerID](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (entityIt == view.end())
        return;

    auto serverIdRes = Utils::GetServerId(*entityIt);
    if (!serverIdRes)
    {
        spdlog::error("{}: server id not found for form id {:X}", __FUNCTION__, acEvent.SpeakerID);
        return;
    }

    SubtitleRequest request{};
    request.ServerId = serverIdRes.value();
    request.Text = acEvent.Text;
    request.TopicFormId = acEvent.TopicFormID;
    if (acEvent.SceneID)
        m_world.GetModSystem().GetServerModId(acEvent.SceneID, request.SceneId);

    m_transport.Send(request);
}

void CharacterService::OnNotifySubtitle(const NotifySubtitle& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
        return;

    // The local copy of the scene shows its own subtitle, see OnNotifyDialogue.
    // Only the actor's own voice line ending hides a subtitle, so a remote one arriving after the local line ended would stay up.
    BGSScene* pScene = pActor->GetCurrentScene();
    if (acMessage.SceneId && pScene && pScene->isPlaying && pScene->formID == m_world.GetModSystem().GetGameId(acMessage.SceneId))
        return;

    // This is only for fallout 4
    TESTopicInfo* pInfo = nullptr;
    pInfo = Cast<TESTopicInfo>(TESForm::GetById(acMessage.TopicFormId));

    SubtitleManager::Get()->ShowSubtitle(pActor, acMessage.Text.c_str(), pInfo);
}

void CharacterService::OnNotifyActorTeleport(const NotifyActorTeleport& acMessage) noexcept
{
    auto& modSystem = m_world.GetModSystem();

    const uint32_t cActorId = World::Get().GetModSystem().GetGameId(acMessage.FormId);
    Actor* pActor = Cast<Actor>(TESForm::GetById(cActorId));
    if (!pActor)
    {
        spdlog::error(__FUNCTION__ ": failed to retrieve actor to teleport.");
        return;
    }

    // A teleport this player cannot see is not made: an actor not loaded here, going to a place not loaded here.
    // Moving it there would load a far exterior cell, whose streaming can still run once the game has dropped the cell.
    // Nor is one out to the exterior beyond the loaded area: the game reads that cell from disk at once, in this frame.
    // The server keeps where the actor is; an actor that comes into view here later is placed by its assignment.
    const bool cPlaceLoaded = IsPlaceLoadedHere(m_world, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);
    const bool cActorLoaded = IsActorLoadedHere(pActor);
    if (!cPlaceLoaded && (!cActorLoaded || acMessage.WorldSpaceId))
    {
        spdlog::info("Teleport of actor {:X} not made: {} (world space {:X}, cell {:X}, position ({:.0f}, {:.0f}, {:.0f}))", pActor->formID,
                     cActorLoaded ? "it goes out beyond the loaded area here" : "it is not loaded here, nor is the place it goes to", acMessage.WorldSpaceId.BaseId,
                     acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
        return;
    }

    if (MoveActor(pActor, acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position))
        spdlog::info("Successfully teleported actor, form id: {:X}, world space: {:X}, cell: {:X}, position: ({}, {}, {})", pActor->formID, acMessage.WorldSpaceId.BaseId,
                     acMessage.CellId.BaseId, acMessage.Position.x, acMessage.Position.y, acMessage.Position.z);
}

void CharacterService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // Takes ownership of all actors
    if (acEvent.IsLeader)
    {
        auto view = m_world.view<FormIdComponent>(entt::exclude<ObjectComponent>);
        Vector<entt::entity> entities(view.begin(), view.end());

        for (auto entity : entities)
            ProcessNewEntity(entity);
    }
}

bool CharacterService::MoveActor(const Actor* apActor, const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    TESObjectCELL* pCell = nullptr;
    if (!acWorldSpaceId)
    {
        const uint32_t cCellId = m_world.GetModSystem().GetGameId(acCellId);
        pCell = Cast<TESObjectCELL>(TESForm::GetById(cCellId));
    }
    // In case of lazy-loading of exterior cells
    else
    {
        const uint32_t cWorldSpaceId = m_world.GetModSystem().GetGameId(acWorldSpaceId);
        TESWorldSpace* const pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceId));
        if (pWorldSpace)
        {
            GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
            pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
        }
    }

    if (!pCell)
    {
        spdlog::error(__FUNCTION__ ": failed to fetch cell to teleport, actor: {:X}, worldspace: {:X}, cell: {:X}, position: {}, {}, {}", apActor->formID, acWorldSpaceId.BaseId, acCellId.BaseId, acPosition.x, acPosition.y, acPosition.z);
        return false;
    }

    // MoveTo drops the actor's 3D for a frame; this keeps discovery from taking that for a remote copy leaving.
    DiscoveryService::NoteMovedHere(apActor->formID);
    apActor->MoveTo(pCell, acPosition);
    return true;
}

void CharacterService::ProcessNewEntity(entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    if (!pActor)
    {
        spdlog::warn(__FUNCTION__ ": actor for new entity not found, form id: {:X}", formIdComponent.Id);
        return;
    }

    // A ghost: a copy of the owner's creature is bound to the server in its place (see s_ghosts).
    // One that was not loaded when the copy was made turns up here, enabled, once its cell loads, and is disabled now.
    if (s_ghosts.find(formIdComponent.Id) != s_ghosts.end())
    {
        if (pActor && !pActor->IsDisabled() && !IsProcessExiting())
        {
            pActor->Disable();
            spdlog::info("Ghost: reference {:X} loaded after its stand-in; disabled", formIdComponent.Id);
        }
        return;
    }

    if (auto* pRemoteComponent = m_world.try_get<RemoteComponent>(aEntity); pRemoteComponent)
    {
        // TODO(cosideci): don't just take all actors (i.e. from other parties),
        // maybe check it server side, add a variable to the request.
        if (m_world.GetPartyService().IsLeader() && !pActor->IsTemporary() && !pActor->IsMount())
        {
            spdlog::info("Sending ownership claim for actor {:X} with server id {:X}", pActor->formID, pRemoteComponent->Id);

            RequestOwnership(pActor->formID, pRemoteComponent->Id, aEntity);
        }
        else
            spdlog::info("New entity remotely managed, form id: {:X}, server id: {:X}", pActor->formID, pRemoteComponent->Id);

        return;
    }

    if (m_world.any_of<RemoteComponent, LocalComponent, WaitingForAssignmentComponent>(aEntity))
        return;

    // One of this game's own actors given to another player while out of view (see s_handedAway).
    // The server still has it and sends it when in range; announcing it here would duplicate it for everybody.
    if (uint32_t serverId = 0; IsHandedAway(pActor, serverId))
    {
        const auto remoteView = m_world.view<RemoteComponent>();
        const bool cCopyStands = std::any_of(std::begin(remoteView), std::end(remoteView), [remoteView, serverId](entt::entity aOther) { return remoteView.get<RemoteComponent>(aOther).Id == serverId; });
        if (cCopyStands)
            spdlog::warn("HandedAway: actor {:X} is back in view after the server sent character {:X}, so a copy of it stands here too; not handled, both stay", pActor->formID,
                         serverId);
        else
            spdlog::info("HandedAway: actor {:X} is back in view and is still server character {:X}; not announced as a new one, the server sends it when it is in range",
                         pActor->formID, serverId);
        return;
    }

    CacheSystem::Setup(World::Get(), aEntity, pActor);

    RequestServerAssignment(aEntity);
}

void CharacterService::RequestServerAssignment(const entt::entity aEntity) const noexcept
{
    if (!m_transport.IsOnline())
        return;

    static uint32_t sCookieSeed = 0;

    const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    auto* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    if (!pActor)
        return;

    TESNPC* pNpc = Cast<TESNPC>(pActor->baseForm);
    if (!pNpc)
        return;

    AssignCharacterRequest message{};

    message.Cookie = sCookieSeed;

    if (!m_world.GetModSystem().GetServerModId(formIdComponent.Id, message.ReferenceId))
    {
        spdlog::error("Server reference id not found for form id {:X}", formIdComponent.Id);
        return;
    }

    // An actor the game holds outside any cell (taken out of the world, or not placed yet) is not announced.
    if (!pActor->parentCell)
    {
        spdlog::info("Actor {:X} has no cell; not announced to the server", pActor->formID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(pActor->parentCell->formID, message.CellId))
    {
        spdlog::error("Server cell id not found for cell id {:X}", pActor->parentCell->formID);
        return;
    }

    if (const auto pWorldSpace = pActor->GetWorldSpace())
    {
        if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, message.WorldSpaceId))
            return;
    }

    message.Position = pActor->position;
    message.Rotation.x = pActor->rotation.x;
    message.Rotation.y = pActor->rotation.z;

    // Serialize the base form
    const auto isPlayer = (formIdComponent.Id == 0x14);
    const auto isTemporary = pActor->formID >= 0xFF000000;

    if (isPlayer)
    {
        pNpc->MarkChanged(0x2000800);
    }

    const auto changeFlags = pNpc->GetChangeFlags();

    if (isPlayer || changeFlags != 0)
    {
        message.ChangeFlags = changeFlags;
        pNpc->Serialize(&message.AppearanceBuffer);
    }

    if (isPlayer)
    {
        auto& entries = message.FaceTints.Entries;

        const auto& tints = PlayerCharacter::Get()->GetTints();

        entries.resize(tints.length);

        for (auto i = 0u; i < tints.length; ++i)
        {
            entries[i].Alpha = tints[i]->alpha;
            entries[i].Color = tints[i]->color;
            entries[i].Type = tints[i]->type;

            if (tints[i]->texture)
                entries[i].Name = tints[i]->texture->name.AsAscii();
        }
    }

    if (isPlayer)
    {
        auto& questLog = message.QuestContent.Entries;
        auto& modSystem = m_world.GetModSystem();

        for (const auto& objective : PlayerCharacter::Get()->objectives)
        {
            auto* pQuest = objective.instance->quest;
            if (!pQuest)
                continue;

            if (!QuestService::IsNonSyncableQuest(pQuest))
            {
                GameId id{};

                if (modSystem.GetServerModId(pQuest->formID, id))
                {
                    auto& entry = questLog.emplace_back();
                    entry.Stage = pQuest->currentStage;
                    entry.Id = id;
                }
            }
        }

        // remove duplicates
        const auto ip = std::unique(questLog.begin(), questLog.end());
        questLog.resize(std::distance(questLog.begin(), ip));
    }

    message.CurrentActorData = BuildActorData(pActor);

    message.FactionsContent = pActor->GetFactions();
    message.IsDragon = pActor->IsDragon();
    message.IsMount = pActor->IsMount();
    message.IsPlayerSummon = pActor->GetCommandingActor() && pActor->GetCommandingActor()->formID == 0x14;

    if (const TESNPC* pPick = pActor->GetLeveledPick())
    {
        const uint32_t pickFormId = pPick->formID;
        if (m_world.GetModSystem().GetServerModId(pickFormId, message.LeveledNpcPickId))
            spdlog::info("Captured leveled NPC pick {:X} for actor {:X} (base {:X})", pickFormId, pActor->formID, pNpc->formID);
        else
            spdlog::warn("Leveled NPC pick {:X} has no server id, identity sync skipped", pickFormId);
    }
    else if (pNpc->IsTemporary())
    {
        spdlog::info("No leveled pick recoverable for temp base {:X} (actor {:X}), identity sync unavailable", pNpc->formID, pActor->formID);
    }

    if (pNpc->IsTemporary())
        pNpc = pNpc->GetTemplateBase();

    // The base form is sent for every actor but the player (the server rejects it there), not only temporary ones.
    // A levelled reference can roll as another creature on each machine; the base lets the other side tell them apart.
    if (pNpc && !isPlayer)
    {
        if (!m_world.GetModSystem().GetServerModId(pNpc->formID, message.FormId) && isTemporary)
        {
            spdlog::error("Server NPC form id not found for form id {:X}", pNpc->formID);
            return;
        }
    }

    // Serialize actions
    auto* const pExtension = pActor->GetExtension();

    message.LatestAction = pExtension->LatestAnimation;
    pActor->SaveAnimationVariables(message.LatestAction.Variables);

    spdlog::debug("Request id: {:X}, cookie: {:X}, entity: {:X}", formIdComponent.Id, sCookieSeed, to_integral(aEntity));

    if (m_transport.Send(message))
    {
        m_world.emplace<WaitingForAssignmentComponent>(aEntity, sCookieSeed);

        sCookieSeed++;
    }
}

void CharacterService::CancelServerAssignment(const entt::entity aEntity, const uint32_t aFormId) const noexcept
{
    // Gone from the game, so its id may be given to an actor of the game's own next. See s_lastConnectionCopies.
    s_lastConnectionCopies.erase(aFormId);

    // TEMPORARY: logs what a temporary actor was when its removal was noticed (checked by live-copies-left, live-seeker-copy-*).
    const bool cTemporaryId = (aFormId & 0xFF000000) == 0xFF000000;
    const bool cGameMade = s_ownCopies.find(aFormId) == s_ownCopies.end();
    if (cTemporaryId)
    {
        Actor* pSeen = Cast<Actor>(TESForm::GetById(aFormId));
        spdlog::info("CopyGone: {:X} removed as {}; the actor is {}; made by this client: {}", aFormId,
                     m_world.all_of<RemoteComponent>(aEntity) ? "remote" : m_world.all_of<LocalComponent>(aEntity) ? "local" : m_world.all_of<WaitingForAssignmentComponent>(aEntity) ? "waiting" : "untracked",
                     pSeen ? (pSeen->IsDeleted() ? "still there, marked deleted" : pSeen->GetNiNode() ? "still there, with 3D" : "still there, no 3D") : "gone from the game already",
                     s_ownCopies.find(aFormId) != s_ownCopies.end() ? "yes" : "no");
    }

    if (m_world.all_of<RemoteComponent>(aEntity))
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));

        if (pActor)
        {
            if (pActor->IsTemporary() && !IsProcessExiting() && s_ownCopies.find(aFormId) == s_ownCopies.end())
            {
                // The game's own temporary actor, remote only because the server gave it to somebody else.
                // The game is disposing of it and will finish; only the record of it is ours. See s_ownCopies.
                spdlog::info("Temporary remote {:X} was made by the game, not by this client; left to the game (form flags {:X})", aFormId, pActor->flags);
                const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
                RecentDeletes::Record(pActor, aFormId, refWord & 0x3FF, refWord, RecentDeletes::kLeftToGame);
                if (ActorExtension* pExtension = pActor->GetExtension())
                    pExtension->SetRemote(false);

                const uint32_t cServerId = m_world.get<RemoteComponent>(aEntity).Id;
                s_handedAway[cServerId] = HandedAway{aFormId, pActor->baseForm ? pActor->baseForm->formID : 0};
                spdlog::info("HandedAway: actor {:X} is server character {:X}, another player's now; remembered, so that it is the same actor when it comes back", aFormId,
                             cServerId);
            }
            else if (pActor->IsTemporary() && !IsProcessExiting())
            {
                // This runs while the game is disposing of the actor; freeing a copy it still holds is a use-after-free.
                // Who still has a claim on it is recorded first; the outstanding BSPointerHandle count is what matters.
                uint32_t claims = 0;
                if (const PlayerCharacter* pPlayer = PlayerCharacter::Get())
                {
                    if (pPlayer->GetCombatTarget() == pActor)
                        claims |= RecentDeletes::kPlayerCombatTarget;
                }
                if (pActor->currentProcess)
                    claims |= RecentDeletes::kHasProcess;
                if (pActor->GetNiNode())
                    claims |= RecentDeletes::kHas3D;
                if (pActor->actorState.IsDeadOrDying())
                    claims |= RecentDeletes::kDeadOrDying;

                // Anyone else fighting it, among the actors this mod tracks (a short loop).
                {
                    auto combatView = m_world.view<FormIdComponent>();
                    for (auto other : combatView)
                    {
                        Actor* pOther = Cast<Actor>(TESForm::GetById(combatView.get<FormIdComponent>(other).Id));
                        if (pOther && pOther != pActor && pOther->GetCombatTarget() == pActor)
                        {
                            claims |= RecentDeletes::kOtherCombatTarget;
                            break;
                        }
                    }
                }

                const uint32_t refWord = static_cast<uint32_t>(pActor->handleRefObject.refCount);
                const uint32_t handles = refWord & 0x3FF;
                RecentDeletes::Record(pActor, aFormId, handles, refWord, claims);

                if (CopyRemovalPolicy::OnRemoved(handles, s_waitingCopies.size()) == CopyRemovalPolicy::Action::WaitDisabled)
                {
                    // Each outcome (held, freed, forced out) logs its own line; session-report.py counts them.
                    spdlog::info("Temporary Remote Held {:X}: {} handle(s) still on it, disabled and queued ({} of {} slots used)",
                                 aFormId, handles, s_waitingCopies.size() + 1, CopyRemovalPolicy::kMaxWaiting);

                    // Disabled, not freed: silent, no AI, not drawn, and nothing dangling.
                    pActor->Disable();
                    s_waitingCopies.push_back(WaitingCopy{aFormId, std::chrono::steady_clock::now()});
                    ReleaseGhostOf(aFormId);
                    DeleteRemoteEntityComponents(aEntity);

                    // The server still counts this character as spawned here; announcing the cell again makes it re-send it.
                    if (m_transport.IsConnected())
                        DiscoveryService::RequestCellReannounce();
                    return;
                }

                // Nothing holds it, or the waiting list is full: free it here.
                // The bound keeps disabled copies from piling up while the game keeps its claims on them.
                if (handles == 0)
                {
                    spdlog::info("Temporary Remote Deleted {:X}: nothing held it", aFormId);
                }
                else
                {
                    // Freed while still held, because the list is full: if this is common, kMaxWaiting needs raising.
                    spdlog::warn("CopyFreedHeld: {:X} freed immediately with {} handle(s) still outstanding -- the waiting list was full ({} slots)",
                                 aFormId, handles, CopyRemovalPolicy::kMaxWaiting);
                }

                pActor->Delete();
                s_ownCopies.erase(aFormId);
                ReleaseGhostOf(aFormId);

                // The cell unloaded under the copy, but the server still counts this character as spawned here.
                // Announcing the cell again makes the server re-send everything in it.
                if (m_transport.IsConnected())
                    DiscoveryService::RequestCellReannounce();
            }
            else
            {
                pActor->GetExtension()->SetRemote(false);
            }
        }

        DeleteRemoteEntityComponents(aEntity);

        return;
    }

    // Not remote: a copy of ours that this client now runs is let go by the game like its own actors, not deleted here.
    // So its id leaves s_ownCopies.
    s_ownCopies.erase(aFormId);

    // Keep the cookie until the server response arrives so awarded ownership can be relinquished.
    if (m_world.all_of<WaitingForAssignmentComponent>(aEntity))
    {
        auto& waitingComponent = m_world.get<WaitingForAssignmentComponent>(aEntity);
        waitingComponent.Cancelled = true;
        return;
    }

    if (m_world.all_of<LocalComponent>(aEntity))
    {
        auto& localComponent = m_world.get<LocalComponent>(aEntity);

        RequestOwnershipTransfer request{};
        request.ServerId = localComponent.Id;
        request.OwnershipEpoch = localComponent.OwnershipEpoch;
        request.Reason = OwnershipReleaseReason::Relinquish;

        if (Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId)))
        {
            if (!pActor->IsTemporary())
            {
                auto& modSystem = m_world.GetModSystem();

                if (TESWorldSpace* pWorldSpace = pActor->GetWorldSpace())
                {
                    if (!modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId))
                        spdlog::error("World space id not found, despite having a world space, {:X}", pWorldSpace->formID);
                }

                if (TESObjectCELL* pCell = pActor->GetParentCellEx())
                {
                    if (!modSystem.GetServerModId(pCell->formID, request.CellId))
                        spdlog::error("Cell id not found, despite having a cell, {:X}", pCell->formID);
                }

                request.Position = pActor->position;
            }
        }

        spdlog::info(
            "Transferring ownership of local actor, server id: {:X}, epoch: {}, worldspace: {:X}, cell: {:X}, position: "
            "({}, {}, {})",
            request.ServerId, request.OwnershipEpoch, request.WorldSpaceId.BaseId, request.CellId.BaseId, request.Position.x, request.Position.y, request.Position.z);

        m_transport.Send(request);

        // One of this game's own temporary actors, given up because its cell unloaded (see s_handedAway).
        // If another player is there, the server gives it to them under this same server id.
        if (cTemporaryId && cGameMade && !IsProcessExiting())
        {
            const Actor* pOwn = Cast<Actor>(TESForm::GetById(aFormId));
            s_handedAway[request.ServerId] = HandedAway{aFormId, pOwn && pOwn->baseForm ? pOwn->baseForm->formID : 0};
            spdlog::info("HandedAway: actor {:X} was server character {:X} and is given up; remembered, in case the server gives it to another player and sends it back", aFormId,
                         request.ServerId);
        }

        m_world.remove<LocalAnimationComponent, LocalComponent>(aEntity);
    }
}

Actor* CharacterService::CreateCharacterForEntity(entt::entity aEntity) const noexcept
{
    PerfCounterScope perfScope(PerfCounter::kActorSpawn);
    auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(aEntity);
    auto* pInterpolationComponent = m_world.try_get<InterpolationComponent>(aEntity);

    if (!pWaitingFor3D || !pInterpolationComponent)
    {
        spdlog::error(__FUNCTION__ ": could not find WaitingFor3D or InterpolationComponent");
        return nullptr;
    }

    auto& acMessage = pWaitingFor3D->SpawnRequest;

    Actor* pActor = nullptr;

    // Custom forms
    if (acMessage.FormId == GameId{})
    {
        TESNPC* pNpc = nullptr;

        if (acMessage.BaseId != GameId{})
        {
            pNpc = BaseForCopy(m_world, acMessage);

            if (!pNpc)
            {
                spdlog::error("Failed to retrieve NPC, it will not be spawned, possibly missing mod");
                return nullptr;
            }

            pNpc->Deserialize(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
        }
        else
        {
            pNpc = TESNPC::Create(acMessage.AppearanceBuffer, acMessage.ChangeFlags);
            FaceGenSystem::Setup(m_world, aEntity, acMessage.FaceTints);
            LogPlayerCopyAppearance(acMessage, pNpc);
        }

        pActor = RememberOwnCopy(Actor::Create(pNpc));
    }
    else
    {
        // A placed reference whose stand-in copy is gone: one made outside the loaded cells is deleted by the next grid shift.
        // This runs once the character is inside the loaded grid, so the new stand-in is made where it can stay.
        const uint32_t cRefId = m_world.GetModSystem().GetGameId(acMessage.FormId);
        TESNPC* pOwnerBase = nullptr;
        if (acMessage.LeveledNpcPickId != GameId{})
            pOwnerBase = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.LeveledNpcPickId)));
        if (!pOwnerBase && acMessage.BaseId != GameId{})
            pOwnerBase = Cast<TESNPC>(TESForm::GetById(m_world.GetModSystem().GetGameId(acMessage.BaseId)));

        if (pOwnerBase && cRefId != 0)
        {
            pActor = RememberOwnCopy(Actor::Create(pOwnerBase));
            if (pActor)
            {
                s_ghosts[cRefId] = pActor->formID;
                if (Actor* pLocal = Cast<Actor>(TESForm::GetById(cRefId)); pLocal && !pLocal->IsDisabled() && !IsProcessExiting())
                    pLocal->Disable();
                spdlog::info("Stand-in: the copy for reference {:X} (server id {:X}) was gone; copy {:X} of the owner's {} ({:X}) stands in again, inside the loaded cells",
                             cRefId, m_world.get<RemoteComponent>(aEntity).Id, pActor->formID, pOwnerBase->fullName.value.AsAscii(), pOwnerBase->formID);
            }
        }
    }

    auto& remoteComponent = m_world.get<RemoteComponent>(aEntity);

    if (!pActor)
    {
        // Once per character: RunSpawnUpdates asks again every five seconds.
        static std::unordered_set<uint32_t> s_said;
        if (s_said.size() > 1024)
            s_said.clear();
        if (s_said.insert(remoteComponent.Id).second)
            spdlog::error(__FUNCTION__ ": could not spawn actor for remote server id {:X} ({}); asking again every 5 s", remoteComponent.Id,
                          acMessage.FormId != GameId{} ? "a placed reference whose copy is gone" : "no base to build it from");
        return nullptr;
    }

    pActor->GetExtension()->SetRemote(true);
    pActor->rotation.x = acMessage.Rotation.x;
    pActor->rotation.z = acMessage.Rotation.y;
    pActor->MoveTo(PlayerCharacter::Get()->parentCell, pInterpolationComponent->Position);
    pActor->SetActorValues(acMessage.IsPlayer ? StandingValues(acMessage.InitialActorValues, pActor->formID) : acMessage.InitialActorValues);

    pActor->GetExtension()->SetPlayer(acMessage.IsPlayer);
    if (acMessage.IsPlayer)
    {
        pActor->SetIgnoreFriendlyHit(true);
        pActor->SetPlayerRespawnMode();
        // The copy stays essential (it cannot die here), but may get up again once its owner's real health arrives.
        pActor->SetNoBleedoutRecovery(false);
        m_world.emplace_or_replace<PlayerComponent>(aEntity, acMessage.PlayerId);
    }

    if (pActor->IsDead() != acMessage.IsDead)
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();

    spdlog::info("Spawned character for entity, server id: {:X}", remoteComponent.Id);

    return pActor;
}

ActorData CharacterService::BuildActorData(Actor* apActor) const noexcept
{
    ActorData actorData{};
    actorData.InitialActorValues = apActor->GetEssentialActorValues();
    actorData.InitialInventory = apActor->GetActorInventory();
    actorData.IsDead = apActor->IsDead();
    actorData.IsWeaponDrawn = apActor->actorState.IsWeaponFullyDrawn();

    return actorData;
}

void CharacterService::ApplyLeveledNpcPick(Actor* apActor, const GameId& acPickId, const Inventory* apOwnerEquipment) const noexcept
{
    if (acPickId == GameId{})
        return;

    // On VR: SetLeveledCreature is AE 20231 -> VR 0x1402b8f10, CreateTemplateActorBase is AE 14375 -> VR 0x19c0c0.
    // cmpayc/TiltedEvolutionVR derived the same CreateTemplateActorBase address (thiscall and fastcall match on x64).
    // GarbageCollector::Add is not called (it crashed); see LeveledNpcSystem::ApplyPick.

    TESNPC* pBase = Cast<TESNPC>(apActor->baseForm);
    if (!pBase)
        return;

    if (!LeveledNpcSystem::GetOriginalBase(apActor))
        {
        spdlog::warn("Leveled pick {:x}:{:x} received for actor {:X} without an original leveled base, keeping local base", acPickId.ModId, acPickId.BaseId, apActor->formID);
            return;
        }

    const uint32_t cPickId = World::Get().GetModSystem().GetGameId(acPickId);
    if (cPickId == 0)
    {
        spdlog::warn("Leveled NPC pick {:X}:{:X} not resolvable, possibly missing mod, keeping local pick", acPickId.ModId, acPickId.BaseId);
        return;
    }

    TESNPC* pPick = Cast<TESNPC>(TESForm::GetById(cPickId));
    if (!pPick)
    {
        spdlog::warn("Leveled NPC pick {:X} is not an NPC, keeping local pick", cPickId);
        return;
    }

    const TESNPC* pLocalPick = apActor->GetLeveledPick();
    const uint32_t localPickId = pLocalPick ? pLocalPick->formID : 0;
    if (m_ownerLeveledPicks.size() > 4096)
        m_ownerLeveledPicks.clear();
    m_ownerLeveledPicks[apActor->formID] = cPickId;

    // Even a pick matching the current base must supersede pending work.
    if (pBase->IsTemporary() && localPickId == cPickId && m_pendingLeveledConforms.find(apActor->formID) == m_pendingLeveledConforms.end())
    {
        spdlog::info("Leveled actor {:X} already matches owner's pick {:X}", apActor->formID, cPickId);
        return;
    }

    spdlog::info("Queued leveled NPC reconciliation for actor {:X}, base: {:X}, local pick: {:X}, owner's pick: {:X}",
        apActor->formID, pBase->formID, localPickId, cPickId);

    // Defer reference changes to the service update: cell attach may still own the actor here.
    // Queueing to the runner from a drained task would re-lock its drain mutex.
    // Preserve the stage when a newer pick arrives during a disable or rebuild.
    m_pendingLeveledConforms[apActor->formID] = cPickId;
    // The rebuild re-equips the creature's outfit, which a copy refuses, so the owner's weapons are put back after it.
    if (apOwnerEquipment)
        m_conformEquipment[apActor->formID] = *apOwnerEquipment;
}

// A copy rebuilt for the owner's pick loses its weapons: the game re-equips its outfit, and a copy refuses the game's own equips.
// The owner's hand items, kept when the rebuild was queued, are put back the way a player copy's are.
// Only when the owner's list holds a weapon, so an NPC whose server copy is older than its drawn weapon keeps what it has.
void CharacterService::RestoreConformedWeapons(Actor* apActor) noexcept
{
    const auto found = m_conformEquipment.find(apActor->formID);
    if (found == m_conformEquipment.end())
        return;
    const Inventory cEquipment = std::move(found->second);
    m_conformEquipment.erase(found);

    const ActorExtension* pExtension = apActor->GetExtension();
    if (!pExtension || !pExtension->IsRemote())
        return;

    auto& modSystem = m_world.GetModSystem();
    const bool cHasWeapon = std::any_of(cEquipment.Entries.begin(), cEquipment.Entries.end(),
                                        [&modSystem](const auto& acEntry)
                                        {
                                            const TESForm* pForm = acEntry.IsWorn() ? TESForm::GetById(modSystem.GetGameId(acEntry.BaseId)) : nullptr;
                                            return pForm && pForm->formType == FormType::Weapon;
                                        });
    if (!cHasWeapon)
        return;

    InventoryService::ApplyHandEquipment(apActor, cEquipment, true);
    const TESForm* pLeft = apActor->GetEquippedWeapon(0);
    const TESForm* pRight = apActor->GetEquippedWeapon(1);
    spdlog::info("Leveled copy {:X} rebuilt: the owner's weapons put back, now holding left {:X}, right {:X}", apActor->formID, pLeft ? pLeft->formID : 0,
                 pRight ? pRight->formID : 0);
}

void CharacterService::ProcessLeveledConforms() noexcept
{
    using ReconciliationStage = ActorExtension::ReconciliationStage;

    // Never touch references while the loading screen is up: the cell attach owns them, and changing them crashes the loader.
    UI* pUI = UI::Get();
    if (pUI && pUI->GetMenuOpen(BSFixedString("Loading Menu")))
        return;

    // A pick that matched when it arrived can stop matching: the game re-rolls a leveled actor when it builds it.
    // So the owner's picks are checked again once a second.
    static std::chrono::steady_clock::time_point s_nextPickCheck;
    if (const auto cNow = std::chrono::steady_clock::now(); cNow >= s_nextPickCheck)
    {
        s_nextPickCheck = cNow + std::chrono::seconds(1);
        for (auto it = m_ownerLeveledPicks.begin(); it != m_ownerLeveledPicks.end();)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(it->first));
            const ActorExtension* pExtension = pActor ? pActor->GetExtension() : nullptr;
            if (!pActor || pActor->IsDeleted() || !pExtension || !pExtension->IsRemote())
            {
                it = m_ownerLeveledPicks.erase(it);
                continue;
            }
            const TESNPC* pLocalPick = pActor->GetLeveledPick();
            if (pLocalPick && pLocalPick->formID != it->second && m_pendingLeveledConforms.find(it->first) == m_pendingLeveledConforms.end())
            {
                // Twice at most per actor, or a reconciliation that never matches would rebuild the creature every second.
                static std::unordered_map<uint32_t, int> s_rerolls;
                if (s_rerolls.size() > 4096)
                    s_rerolls.clear();
                if (int& tries = s_rerolls[it->first]; tries < 2)
                {
                    ++tries;
                    spdlog::info("Leveled actor {:X} was rolled again by the game as {:X}; the owner's pick is {:X}, reconciling again ({} of 2)", it->first,
                                 pLocalPick->formID, it->second, tries);
                    m_pendingLeveledConforms[it->first] = it->second;
                }
            }
            ++it;
        }
    }

    if (m_pendingLeveledConforms.empty())
        return;

    for (auto it = m_pendingLeveledConforms.begin(); it != m_pendingLeveledConforms.end();)
    {
        const uint32_t cPickFormId = it->second;

        Actor* pActor = Cast<Actor>(TESForm::GetById(it->first));
        TESNPC* pPick = Cast<TESNPC>(TESForm::GetById(cPickFormId));
        if (!pActor || pActor->IsDeleted() || !pPick)
        {
            if (pActor)
                pActor->GetExtension()->Reconciliation = ReconciliationStage::None;

            it = m_pendingLeveledConforms.erase(it);
            continue;
        }

        auto& stage = pActor->GetExtension()->Reconciliation;
        if (stage == ReconciliationStage::WaitingFor3D)
        {
            const auto* pCell = pActor->GetParentCellEx();
            if (!pCell || !pCell->IsAttached())
            {
                spdlog::info("Abandoning leveled NPC reconciliation for actor {:X} because its cell is not attached, pick: {:X}, cell state: {}, disabled: {}",
                    it->first, cPickFormId, pCell ? static_cast<int>(pCell->cellState) : -1, pActor->IsDisabled());
                stage = ReconciliationStage::None;
                it = m_pendingLeveledConforms.erase(it);
                continue;
            }

            if (pActor->IsDisabled() || !pActor->GetNiNode())
            {
                ++it;
                continue;
            }

            if (pActor->baseForm && pActor->baseForm->IsTemporary() && pActor->GetLeveledPick() == pPick)
            {
                spdlog::info("Completed leveled NPC reconciliation for actor {:X}, base: {:X}, pick: {:X}", it->first, pActor->baseForm->formID, cPickFormId);
                RestoreConformedWeapons(pActor);
                stage = ReconciliationStage::None;
                it = m_pendingLeveledConforms.erase(it);
                continue;
            }

            // A newer pick arrived during the rebuild; start its disable now.
            stage = ReconciliationStage::None;
        }

        if (stage == ReconciliationStage::WaitingForDisable)
        {
            if (!pActor->IsDisabled() || pActor->GetNiNode())
            {
                spdlog::debug("Waiting for leveled actor {:X} to finish disabling before applying pick {:X}, disabled: {}, has 3D: {}",
                    it->first, cPickFormId, pActor->IsDisabled(), pActor->GetNiNode() != nullptr);
                ++it;
                continue;
            }

            if (!LeveledNpcSystem::ApplyPick(pActor, pPick))
            {
                spdlog::warn("Could not rebuild leveled actor {:X} from its original base and pick {:X}, keeping local base", it->first, cPickFormId);
            pActor->EnableImpl();
                stage = ReconciliationStage::None;
                it = m_pendingLeveledConforms.erase(it);
                continue;
            }

            // Recompute the graph descriptor after changing picks; stale variable indices can cause out-of-bounds writes.
            pActor->GetExtension()->GraphDescriptorHash = 0;

            // Enable can return before the rebuilt 3D is available to discovery.
            stage = ReconciliationStage::WaitingFor3D;
            pActor->EnableImpl();
            spdlog::info("Re-enabled conformed leveled actor {:X}, base: {:X}, pick: {:X}, waiting for 3D",
                it->first, pActor->baseForm->formID, cPickFormId);
            ++it;
            continue;
        }

        if (!pActor->loadedState && !LeveledNpcSystem::IsLeveledNpcBase(Cast<TESNPC>(pActor->baseForm)))
        {
            // Wait for distant actors to load 3D; newer picks replace pending work and disconnects clear it.
            // Unresolved shells bypass this wait because they need a pick before they can load a model.
            ++it;
            continue;
        }

        // DisableImpl() is asynchronous: it only queues a request to disable this actor.
        // Wait for the disabled flag and old 3D removal before changing the base.
        pActor->DisableImpl();
        stage = ReconciliationStage::WaitingForDisable;
        ++it;
    }
}

void CharacterService::RunLocalUpdates() const noexcept
{
    // The local player is sent at ~30 Hz for smooth VR head and hand movement, other actors at 10 Hz.
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    static std::chrono::steady_clock::time_point lastFullSendTimePoint;
    constexpr auto cDelayBetweenPlayerSnapshots = 33ms;
    constexpr auto cDelayBetweenSnapshots = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenPlayerSnapshots)
        return;

    // Logs how long this client was silent, with the open menus, when it starts sending again.
    // The server drops a silent client and hands its actors away, so the cause of a gap is worth knowing.
    if (lastSendTimePoint.time_since_epoch().count() != 0)
    {
        const auto silence = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSendTimePoint).count();
        if (silence >= 1000)
        {
            UI* pUI = UI::Get();
            std::string menus;
            if (pUI)
            {
                for (IMenu* pMenu : pUI->menuStack)
                {
                    if (!pMenu)
                        continue;
                    if (BSFixedString* pName = pUI->LookupMenuNameByInstance(pMenu))
                        menus += fmt::format("{}{}", menus.empty() ? "" : ", ", pName->AsAscii());
                }
            }
            spdlog::warn("Silence: sent nothing for {} ms; menus [{}]. The server drops a client that stops talking, and a drop hands its actors away.", silence,
                         menus.empty() ? "none" : menus);
        }
    }

    lastSendTimePoint = now;

#ifdef SKYRIMVR
    // The arrow on the string is an animation attachment the other side never gets, so it is sent from the attack state.
    // Each attack state change is logged (session-report.py reads it).
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
    {
        static uint32_t s_lastAttackState = 0xFFFFFFFFu;
        const uint32_t attackState = pPlayer->actorState.AttackState();
        if (attackState != s_lastAttackState)
        {
            s_lastAttackState = attackState;
            spdlog::info("VRArchery: local attack state {} (8 bow draw, 9 arrow on the string, 10 drawn, 11 releasing, 12 released), weapon drawn {}", attackState,
                         pPlayer->actorState.IsWeaponDrawn());
        }

        // Measurement: whether the heading sent for this player follows the headset or the body.
        VRBodySync::NoteHeading(pPlayer);

        // Measurement: where the game keeps this player's own arrow, in the hand and on the string, and whether it is drawn.
        if (pPlayer->actorState.IsWeaponDrawn())
            VRBodySync::LogLocalArrowNodes(pPlayer, attackState);

        // Sent to the others when the arrow goes on and comes off, so their copy of this player shows it.
        static bool s_nockedSent = false;
        // Nocked from state 9 (the arrow on the string, pulled or not) until the bow is shot or put away (0).
        // A VR draw goes 8, 9, 10 (pulled), and back to 9 and 8 while it still sits on the string, so 8 keeps it.
        const bool cNocked = attackState >= 9 || (s_nockedSent && attackState >= 8);
        // Before that, an arrow held in the hand: taken from the quiver, or ready after a shot.
        // Sent when it comes and goes, and again when the hand holds it differently (at most once a second).
        const VRBodySync::ArrowHold cHold = !cNocked && pPlayer->actorState.IsWeaponDrawn() ? VRBodySync::LocalArrowHold() : VRBodySync::ArrowHold{};
        static VRBodySync::ArrowHold s_holdSent{};
        static std::chrono::steady_clock::time_point s_holdSentAt{};
        // A dot product of the two rotations under 0.996 is a turn of more than about 10 degrees.
        const bool cHoldMoved = cHold.InHand && s_holdSent.InHand && now - s_holdSentAt >= 1s &&
                                (cHold.Side != s_holdSent.Side || glm::distance(cHold.Offset, s_holdSent.Offset) > 3.f ||
                                 std::abs(glm::dot(cHold.Rotation, s_holdSent.Rotation)) < 0.996f);
        if ((cNocked != s_nockedSent || cHold.InHand != s_holdSent.InHand || cHoldMoved) && m_transport.IsConnected())
        {
            ArrowNockedRequest request{};
            request.Nocked = cNocked;
            request.InHand = cHold.InHand;
            request.HandSide = cHold.Side;
            request.HandOffset = cHold.Offset;
            request.HandRotation = cHold.Rotation;
            if (m_transport.Send(request))
            {
                s_nockedSent = cNocked;
                s_holdSent = cHold;
                s_holdSentAt = now;
                spdlog::info("ArrowNocked: sent this player's arrow {}{}", cNocked ? "on the string" : cHold.InHand ? "in the hand" : "gone",
                             cHold.InHand ? fmt::format(" ({} hand, its head at ({:.0f}, {:.0f}, {:.0f}) in the hand bone's space)", cHold.Side ? "right" : "left", cHold.Offset.x,
                                                        cHold.Offset.y, cHold.Offset.z)
                                          : std::string());
            }
        }
    }
#endif

    const bool fullSnapshot = now - lastFullSendTimePoint >= cDelayBetweenSnapshots;
    if (fullSnapshot)
        lastFullSendTimePoint = now;

    ClientReferencesMoveRequest message;
    message.Tick = m_transport.GetClock().GetCurrentTick();

    auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();

    for (auto entity : animatedLocalView)
    {
        auto& localComponent = animatedLocalView.get<LocalComponent>(entity);
        auto& animationComponent = animatedLocalView.get<LocalAnimationComponent>(entity);
        auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);

        if (!fullSnapshot && formIdComponent.Id != 0x14)
            continue;

        if (formIdComponent.Id != 0x14)
            NoteDownedTravel(formIdComponent.Id);

        AnimationSystem::Serialize(m_world, message, localComponent, animationComponent, formIdComponent);
    }

    if (fullSnapshot || !message.Updates.empty())
        m_transport.Send(message);
}

// Shows the other player's name and health above their head on the game's world-space meter (WSEnemyMeters).
// There is only one meter, so the game's own fresh target wins it.
// Otherwise the other player's bar shows when you look at them, or whenever they are hurt.
void CharacterService::RunEnemyMeterUpdates() noexcept
{
#ifdef SKYRIMVR
    constexpr float cRange = 3000.f;      // about 40 m
    constexpr float cAcquireAngle = 18.f; // degrees off the centre of your gaze to bring the bar up
    constexpr float cHoldAngle = 32.f;    // wider once it is up, so a glance aside does not drop it
    constexpr float cHurtFraction = 0.6f; // below this, show them wherever you are looking
    constexpr float cBlindRange = 1500.f; // fallback when the headset node cannot be read

    static std::chrono::steady_clock::time_point s_next;
    static uint32_t s_shownHandle = 0;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next)
        return;
    s_next = now + 250ms; // the meter fades if it is not told again, so this is a refresh as much as a decision

    const auto clear = [&]()
    {
        if (s_shownHandle)
        {
            UI::SetEnemyMeterTarget(0, 0);
            s_shownHandle = 0;
        }
    };

    if (!m_transport.IsConnected())
    {
        clear();
        return;
    }
    // The game pointed the meter at its own target (a hit on an enemy): leave it alone, but only briefly.
    // The game re-points it on every hit, so a long hold-off would hide the other player's bar for a whole fight.
    if (now - UI::LastGameEnemyMeterTargetAt() < 2s)
    {
        s_shownHandle = 0;
        return;
    }
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    Actor* pBest = nullptr;
    float bestScore = 0.f;

    auto view = m_world.view<PlayerComponent, FormIdComponent, RemoteComponent>();
    for (auto entity : view)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor || !pActor->GetNiNode() || pActor->actorState.IsDeadOrDying())
            continue;

        const float dx = pActor->position.x - pPlayer->position.x;
        const float dy = pActor->position.y - pPlayer->position.y;
        const float dz = pActor->position.z - pPlayer->position.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance > cRange)
            continue;

        NiPoint3 chest = pActor->position;
        chest.z += 100.f; // the head and chest, not the feet: that is what you look at
        const float angle = VRBodySync::HeadsetAngleTo(chest);

        const uint32_t handle = pActor->GetHandle().handle.iBits;
        const bool held = handle != 0 && handle == s_shownHandle;
        const bool looking = angle < 0.f ? distance < cBlindRange : angle <= (held ? cHoldAngle : cAcquireAngle);

        const float maxHealth = pActor->GetActorPermanentValue(ActorValueInfo::kHealth);
        const float fraction = maxHealth > 0.f ? pActor->GetActorValue(ActorValueInfo::kHealth) / maxHealth : 1.f;
        const bool hurt = fraction < cHurtFraction;

        if (!looking && !hurt)
            continue;

        // Looking at someone beats a scratch across the room; the worse they are hurt, the more it counts.
        const float score = (looking ? 100.f - std::max(angle, 0.f) : 0.f) + (hurt ? (1.f - fraction) * 60.f : 0.f);
        if (score > bestScore)
        {
            bestScore = score;
            pBest = pActor;
        }
    }

    if (!pBest)
    {
        clear();
        return;
    }
    const uint32_t handle = pBest->GetHandle().handle.iBits;
    if (!handle)
        return;
    UI::SetEnemyMeterTarget(handle, pBest->GetLevel());
    s_shownHandle = handle;
#endif
}

#ifdef SKYRIMVR
// Every 10 s, counts actors still flagged remote that have no RemoteComponent, and warns if there are any.
// HookActorProcess suppresses the AI of anything flagged remote, and nothing would clear the flag on such an actor.
void CharacterService::RunOrphanedRemoteDiag() noexcept
{
#ifdef SKYRIMVR
    static std::chrono::steady_clock::time_point s_next;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next)
        return;
    s_next = now + 10s;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    uint32_t orphans = 0;
    uint32_t firstId = 0;
    const char* pFirstName = "?";

    auto view = m_world.view<FormIdComponent>();
    for (auto entity : view)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor)
            continue;

        const ActorExtension* pExtension = pActor->GetExtension();
        if (!pExtension || !pExtension->IsRemote())
            continue;

        // Flagged remote with nothing to say it should be.
        if (m_world.all_of<RemoteComponent>(entity))
            continue;

        ++orphans;
        if (!firstId)
        {
            firstId = pActor->formID;
            if (const TESNPC* pBase = Cast<TESNPC>(pActor->baseForm))
                pFirstName = pBase->fullName.value.AsAscii();
        }
    }

    if (orphans)
        spdlog::warn("OrphanDiag: {} actors are still flagged remote with no remote component; first is {:X} ({}). Their AI is being suppressed and nothing is left to turn it back on.", orphans,
                     firstId, pFirstName);
#endif
}

// Runs every frame, because a haptic pulse that arrives late does not feel like contact.
// The cost is one form lookup and a distance test per tracked actor; anything beyond arm's reach is skipped early.
void CharacterService::RunWeaponTouch() noexcept
{
#ifdef SKYRIMVR
    VRBodySync::BeginWeaponTouch();

    auto view = m_world.view<FormIdComponent>();
    for (auto entity : view)
        VRBodySync::ConsiderForWeaponTouch(Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id)));

    VRBodySync::EndWeaponTouch();
#endif
}

// Clears invisibility on remote player copies.
// TEMPORARY: also logs a CopyDiag line per copy, for copies that can be hit but not seen (session-report.py reads it).
void CharacterService::RunRemotePlayerDiag() noexcept
{
    static std::chrono::steady_clock::time_point s_next;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_next)
        return;
    // Every 2 s: the corrective pass is cheap; the expensive measuring below has its own timer.
    s_next = now + 2s;
    if (!m_transport.IsConnected())
        return;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
    static std::chrono::steady_clock::time_point s_nextBody;
    bool measured = false;
    auto view = m_world.view<PlayerComponent, FormIdComponent, RemoteComponent>();
    for (auto entity : view)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (!pActor)
            continue;
        const float dx = pActor->position.x - pPlayer->position.x;
        const float dy = pActor->position.y - pPlayer->position.y;
        const float dz = pActor->position.z - pPlayer->position.z;
        // Whatever made the copy invisible (a synced value, a magic effect it ran), it is cleared here and logged.
        const float invisibility = pActor->GetActorValue(ActorValueInfo::kInvisibility);
        if (invisibility != 0.f)
        {
            spdlog::info("Remote player {:X} copy was invisible ({:.2f}); cleared", pActor->formID, invisibility);
            pActor->SetActorValue(ActorValueInfo::kInvisibility, 0.f);
        }

        // The node's fade (+0x158) is not forced: a copy beyond the fade distance is correctly faded out by the game.

        // The CopyDiag line costs 20 to 30 ms, because DescribeBody walks and measures the whole skeleton.
        // So each copy is measured once when first seen, and after that every five minutes.
        static std::unordered_set<uint32_t> s_measuredOnce;
        if (s_measuredOnce.size() > 256)
            s_measuredOnce.clear();
        const bool cFirstSight = s_measuredOnce.insert(pActor->formID).second;
        if (!cFirstSight && now < s_nextBody)
            continue;

        spdlog::info("CopyDiag: player copy {:X} '{}' {:.0f} units away, dz {:.0f}, form flags {:X}, health {:.0f}, dead {}, bleedout {}, state1 {:X}, invisibility {:.2f}, {}", pActor->formID,
                     pActor->baseForm ? Cast<TESNPC>(pActor->baseForm)->fullName.value.AsAscii() : "?", std::sqrt(dx * dx + dy * dy + dz * dz), dz, pActor->flags,
                     pActor->GetActorValue(24), pActor->IsDead(), pActor->actorState.IsBleedingOut(), pActor->actorState.flags1, invisibility, VRBodySync::DescribeBody(pActor));
        measured = true;
    }

    if (measured)
        s_nextBody = now + 5min;
}
#else
void CharacterService::RunRemotePlayerDiag() noexcept {}
void CharacterService::RunOrphanedRemoteDiag() noexcept {}
void CharacterService::RunWeaponTouch() noexcept {}
#endif

void CharacterService::RunRemoteUpdates() noexcept
{
    // Each actor plays at its own delay, from how its updates arrive (InterpolationSystem, PlaybackClock).
    // The same tick drives the reference, its skeleton and its replayed actions, so a fast-moving body stays on its reference.
    const double cNow = m_transport.GetLastPollTime();
    // For an actor with no update yet: the fixed delay used before the playback clock.
    constexpr uint64_t cFallbackDelay = 225;
    const uint64_t nowTick = m_transport.GetClock().GetCurrentTick();
    const uint64_t fallbackTick = nowTick > cFallbackDelay ? nowTick - cFallbackDelay : 0;

    // Interpolation has to keep running even if the actor is not in view, otherwise we will never know if we need to spawn it
    auto interpolatedEntities = m_world.view<RemoteComponent, InterpolationComponent>();

    for (auto entity : interpolatedEntities)
    {
        auto* pFormIdComponent = m_world.try_get<FormIdComponent>(entity);
        auto& interpolationComponent = interpolatedEntities.get<InterpolationComponent>(entity);

        Actor* pActor = nullptr;
        if (pFormIdComponent)
        {
            auto* pForm = TESForm::GetById(pFormIdComponent->Id);
            pActor = Cast<Actor>(pForm);
        }

        InterpolationSystem::Update(pActor, interpolationComponent, cNow);

#ifdef SKYRIMVR
        // Cheap: it returns at once unless this is a dead body within arm's reach.
        // A body another player owns that is being moved here is asked for, since only the owner sends a body's motion.
        if (VRBodySync::IsRemoteBodyGrabbedHere(pActor, interpolationComponent.Position) && pFormIdComponent)
        {
            const auto& remoteComponent = interpolatedEntities.get<RemoteComponent>(entity);
            spdlog::info("Body {:X} (server id {:X}) is being moved here; asking for it, so that this side sends it", pFormIdComponent->Id, remoteComponent.Id);
            if (!RequestOwnership(pFormIdComponent->Id, remoteComponent.Id, entity))
                spdlog::info("Body {:X}: not asked after all (ownership epoch {} known here)", pFormIdComponent->Id, remoteComponent.OwnershipEpoch);
        }
#endif
    }

    auto animatedView = m_world.view<RemoteComponent, RemoteAnimationComponent, FormIdComponent>();

    for (auto entity : animatedView)
    {
        auto& animationComponent = animatedView.get<RemoteAnimationComponent>(entity);
        auto& formIdComponent = animatedView.get<FormIdComponent>(entity);

        auto* pForm = TESForm::GetById(formIdComponent.Id);
        auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        const auto* pInterpolation = m_world.try_get<InterpolationComponent>(entity);
        const uint64_t cActorTick = pInterpolation ? InterpolationSystem::GetPlaybackTick(*pInterpolation) : 0;
        AnimationSystem::Update(m_world, pActor, animationComponent, cActorTick ? cActorTick : fallbackTick);
    }

    auto facegenView = m_world.view<FormIdComponent, FaceGenComponent>();

    for (auto entity : facegenView)
    {
        auto& formIdComponent = facegenView.get<FormIdComponent>(entity);
        auto& faceGenComponent = facegenView.get<FaceGenComponent>(entity);

        const auto* pForm = TESForm::GetById(formIdComponent.Id);
        auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        FaceGenSystem::Update(m_world, pActor, faceGenComponent);
    }

    auto waitingView = m_world.view<FormIdComponent, WaitingFor3D>();

    Vector<entt::entity> readyEntities;
    for (auto entity : waitingView)
    {
        auto& formIdComponent = waitingView.get<FormIdComponent>(entity);
        auto& waitingFor3D = waitingView.get<WaitingFor3D>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor || !pActor->GetNiNode())
            continue;

        // By now, the actor has materialized in the world and is ready for further setup

        pActor->SetActorInventory(waitingFor3D.SpawnRequest.InventoryContent);
        // On VR the inventory apply above does not put weapons and torches in a remote player's hands, so they are applied here.
        // Players only: an NPC's server inventory can predate its AI drawing a weapon.
        if (pActor->GetExtension()->IsRemotePlayer())
            InventoryService::ApplyHandEquipment(pActor, waitingFor3D.SpawnRequest.InventoryContent, true);
        pActor->SetFactions(waitingFor3D.SpawnRequest.FactionsContent);

        if (!waitingFor3D.SpawnRequest.ActionsToReplay.Actions.empty())
        {
            pActor->LoadAnimationVariables(waitingFor3D.SpawnRequest.ActionsToReplay.Actions[0].Variables);
        }

        m_weaponDrawUpdates[pActor->formID] = {waitingFor3D.SpawnRequest.IsWeaponDrawn};
        if (waitingFor3D.SpawnRequest.IsPlayer)
            spdlog::info("Player copy {:X} came with its weapon {}", pActor->formID, waitingFor3D.SpawnRequest.IsWeaponDrawn ? "drawn" : "sheathed");

        if (pActor->IsDead() != waitingFor3D.SpawnRequest.IsDead)
        {
            if (waitingFor3D.SpawnRequest.IsDead)
                spdlog::info("Character {:X} ({:X}) came dead; killed now that it has its 3D", waitingFor3D.SpawnRequest.ServerId, pActor->formID);
            waitingFor3D.SpawnRequest.IsDead ? pActor->Kill() : pActor->Respawn();
        }

        if (pActor->IsVampireLord())
            pActor->FixVampireLordModel();

        readyEntities.push_back(entity);

        // A player's copy is hidden until the interpolation has put it where its owner is (SpawnReveal).
        // Not an NPC's: one standing still sends no movement, so it would appear late.
        if (waitingFor3D.SpawnRequest.IsPlayer)
            SpawnReveal::HideUntilPlaced(pActor);

        spdlog::info("Applied 3D for actor, form id: {:X}", pActor->formID);
    }

    for (auto entity : readyEntities)
    {
        m_world.remove<WaitingFor3D>(entity);

        // Reprocess the remote actor now that an ownership grant can be accepted without immediately declining it.
        ProcessNewEntity(entity);
    }
}

void CharacterService::RunFactionsUpdates() const noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 2000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    RequestFactionsChanges message;

    auto factionedActors = m_world.view<LocalComponent, CacheComponent, FormIdComponent>();
    for (auto entity : factionedActors)
    {
        auto& formIdComponent = factionedActors.get<FormIdComponent>(entity);
        auto& localComponent = factionedActors.get<LocalComponent>(entity);
        auto& cacheComponent = factionedActors.get<CacheComponent>(entity);

        const auto* pForm = TESForm::GetById(formIdComponent.Id);
        const auto* pActor = Cast<Actor>(pForm);
        if (!pActor)
            continue;

        // Check if cached factions and current factions are identical
        auto factions = pActor->GetFactions();

        if (cacheComponent.FactionsContent == factions)
            continue;

        cacheComponent.FactionsContent = factions;

        // If not send the current factions and replace the cached factions
        message.Changes[localComponent.Id] = factions;
    }

    if (!message.Changes.empty())
        m_transport.Send(message);
}

void CharacterService::RunSpawnUpdates() const noexcept
{
    auto invisibleView = m_world.view<RemoteComponent, InterpolationComponent, RemoteAnimationComponent, WaitingFor3D>(entt::exclude<FormIdComponent>);
    Vector<entt::entity> entities(invisibleView.begin(), invisibleView.end());

    for (const auto entity : entities)
    {
        auto& remoteComponent = m_world.get<RemoteComponent>(entity);
        auto& interpolationComponent = m_world.get<InterpolationComponent>(entity);

        if (const auto pWorldSpace = PlayerCharacter::Get()->GetWorldSpace())
        {
            float characterX = interpolationComponent.Position.x;
            float characterY = interpolationComponent.Position.y;
            const auto characterCoords = GridCellCoords::CalculateGridCellCoords(characterX, characterY);
            const TES* pTES = TES::Get();
            const auto playerCoords = GridCellCoords(pTES->centerGridX, pTES->centerGridY);

            // A dragon can fight well outside the 5x5 grid, so it is created at the server's wide range too.
            // Not a stand-in (FormId set): outside the loaded cells the game deletes it at once, and it would be remade every frame.
            const auto& cSpawnRequest = m_world.get<WaitingFor3D>(entity).SpawnRequest;
            const bool isDragon = cSpawnRequest.IsDragon && cSpawnRequest.FormId == GameId{};
            if (GridCellCoords::IsCellInGridCell(characterCoords, playerCoords, isDragon))
            {
                // The id lookup can return a freed copy (EngineFixesVR's FormCaching), and casting it faults.
                // So the object is checked to be a live game object first; a dead one counts as no copy.
                TESForm* pCached = TESForm::GetById(remoteComponent.CachedRefId);
                if (pCached && !IsLiveGameObject(pCached))
                {
                    spdlog::warn("Cached copy {:X} of remote character {:X} no longer resolves to a live object; making a fresh one",
                                 remoteComponent.CachedRefId, remoteComponent.Id);
                    remoteComponent.CachedRefId = 0;
                    pCached = nullptr;
                }

                // A copy whose base changed is no longer ours: the game reused its id for one of its own actors (see ClaimCopyId).
                if (pCached && remoteComponent.CachedBaseId != 0)
                {
                    const auto* pCachedActor = Cast<Actor>(pCached);
                    if (!pCachedActor || !pCachedActor->baseForm || pCachedActor->baseForm->formID != remoteComponent.CachedBaseId)
                    {
                        spdlog::warn("Copy {:X} of server character {:X} is gone; its id now names another actor (base {:X}, the copy's was {:X}). A new copy is made",
                                     remoteComponent.CachedRefId, remoteComponent.Id, pCachedActor && pCachedActor->baseForm ? pCachedActor->baseForm->formID : 0,
                                     remoteComponent.CachedBaseId);
                        remoteComponent.CachedRefId = 0;
                        remoteComponent.CachedBaseId = 0;
                        pCached = nullptr;
                    }
                }

                auto* pActor = Cast<Actor>(pCached);
                if (!pActor)
                {
                    // A character that cannot be built here is retried every 5 s, not every frame.
                    static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> s_nextAttempt;
                    const auto cNow = std::chrono::steady_clock::now();
                    auto& nextAttempt = s_nextAttempt[remoteComponent.Id];
                    if (cNow < nextAttempt)
                        continue;
                    if (s_nextAttempt.size() > 1024)
                        s_nextAttempt.clear();

                    // A copy gone within two seconds of being made would be removed again, so it waits like a failed one.
                    static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> s_madeAt;
                    if (s_madeAt.size() > 1024)
                        s_madeAt.clear();
                    if (const auto cMade = s_madeAt.find(remoteComponent.Id); cMade != s_madeAt.end() && cNow - cMade->second < std::chrono::seconds(2))
                    {
                        spdlog::info("Copy of server character {:X} made {} ms ago is gone again; next try in 5 s", remoteComponent.Id,
                                     std::chrono::duration_cast<std::chrono::milliseconds>(cNow - cMade->second).count());
                        s_madeAt.erase(cMade);
                        s_nextAttempt[remoteComponent.Id] = cNow + std::chrono::seconds(5); // not nextAttempt: the map may have been cleared above
                        continue;
                    }

                    pActor = CreateCharacterForEntity(entity);
                    if (!pActor)
                    {
                        s_nextAttempt[remoteComponent.Id] = cNow + std::chrono::seconds(5);
                        continue;
                    }
                    s_madeAt[remoteComponent.Id] = cNow;

                    remoteComponent.CachedRefId = pActor->formID;
                    remoteComponent.CachedBaseId = s_ownCopies.find(pActor->formID) != s_ownCopies.end() && pActor->baseForm ? pActor->baseForm->formID : 0;
                    ClaimCopyId(entity, pActor->formID);
                }

                pActor->MoveTo(PlayerCharacter::Get()->parentCell, interpolationComponent.Position);
            }
        }
    }
}

void CharacterService::RunExperienceUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (m_cachedExperience == 0.f)
        return;

    if (!World::Get().GetPartyService().IsInParty())
        return;

    SyncExperienceRequest message;
    message.Experience = m_cachedExperience;

    m_cachedExperience = 0.f;

    m_transport.Send(message);

    spdlog::debug("Sending over experience {}", message.Experience);
}

void CharacterService::ApplyPendingInventories() noexcept
{
    for (auto it = m_pendingInventories.begin(); it != m_pendingInventories.end();)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(it->first));
        if (!pActor)
        {
            it = m_pendingInventories.erase(it);
            continue;
        }
        if (!HasLoadedProcess(pActor))
        {
            ++it;
            continue;
        }
        const Inventory current = pActor->GetActorInventory();
        if (current.Entries != it->second.Entries || current.CurrentMagicEquipment != it->second.CurrentMagicEquipment)
            pActor->SetActorInventory(it->second);
        spdlog::info("Inventory of {:X} applied now that the actor is loaded here", it->first);
        it = m_pendingInventories.erase(it);
    }
}

void CharacterService::ApplyCachedWeaponDraws(const UpdateEvent& acUpdateEvent) noexcept
{
    std::vector<uint32_t> toRemove{};

    for (auto& [cId, _] : m_weaponDrawUpdates)
    {
        auto& data = m_weaponDrawUpdates[cId];

        data.m_timer += acUpdateEvent.Delta;

        // Remote actors get 2 passes because Skyrim's weapon drawing is the most finnicky thing in existence.
        double maxTime = data.m_isFirstPass ? 0.5 : 2.0;
        if (data.m_timer <= maxTime)
            continue;

        Actor* pActor = Cast<Actor>(TESForm::GetById(cId));
        if (!pActor || !pActor->GetExtension()->IsRemote())
        {
            toRemove.push_back(cId);
            continue;
        }

        // WEAPON_STATE, bits 5-7 of ActorState::flags2: a draw that took shows "wantToDraw" or "drawing" here.
        static constexpr const char* kWeaponStates[8] = {"sheathed", "wantToDraw", "drawing", "drawn", "wantToSheathe", "sheathing", "6", "7"};
        const uint32_t cStateBefore = pActor->actorState.flags2 >> 5 & 7;
        pActor->SetWeaponDrawnEx(data.m_drawWeapon);
        if (pActor->GetExtension()->IsRemotePlayer())
            spdlog::info("Weapon of player copy {:X} set {} ({} pass); the game went from {} to {}", cId, data.m_drawWeapon ? "drawn" : "sheathed", data.m_isFirstPass ? "first" : "second",
                         kWeaponStates[cStateBefore], kWeaponStates[pActor->actorState.flags2 >> 5 & 7]);

        if (!data.m_isFirstPass)
            toRemove.push_back(cId);

        data.m_isFirstPass = false;
    }

    for (uint32_t id : toRemove)
        m_weaponDrawUpdates.erase(id);
}

// Sends each clash VRBodySync saw: the other player's weapon body met one of this player's HIGGS bodies.
// It is felt here, and the other player feels it on the hand holding that weapon.
void CharacterService::SendClashes() noexcept
{
#ifdef SKYRIMVR
    VRBodySync::Clash clash;
    while (VRBodySync::TakeClash(clash))
    {
        auto view = m_world.view<FormIdComponent, RemoteComponent>();
        const auto it = std::find_if(view.begin(), view.end(), [&view, &clash](auto entity) { return view.get<FormIdComponent>(entity).Id == clash.FormId; });
        if (it == view.end())
            continue;
        // A weapon on a hand holding no weapon or shield is a hit on the hand, not a parry: nothing is felt, heard or sent.
        // Such touches are logged at most every 5 s.
        if (!clash.Parries)
        {
            static uint32_t s_bare = 0;
            static auto s_nextBareSaid = std::chrono::steady_clock::now();
            ++s_bare;
            if (const auto cNow = std::chrono::steady_clock::now(); cNow >= s_nextBareSaid)
            {
                spdlog::info("Clash: {} touch(es) of the weapon of {:X} on our {} holding no weapon or shield since the last such line; no parry", s_bare, clash.FormId,
                             clash.OwnSide == 0 ? "left hand" : clash.OwnSide == 1 ? "right hand" : "hand");
                s_bare = 0;
                s_nextBareSaid = cNow + std::chrono::seconds(5);
            }
            continue;
        }
        // Every touch is weighed by the defender's rule (ActorValueService); only a meeting is felt, heard and sent.
        const uint64_t cTick = m_transport.GetClock().GetCurrentTick();
        m_dispatcher.trigger(ClashEvent{clash.FormId, cTick});
        if (!clash.Start)
        {
            // A resting touch is not a meeting; it is logged every 5 s while it lasts.
            static uint32_t s_touches = 0;
            static auto s_nextSaid = std::chrono::steady_clock::now();
            ++s_touches;
            if (const auto cNow = std::chrono::steady_clock::now(); cNow >= s_nextSaid)
            {
                spdlog::info("Clash: {} touch(es) of the weapon of {:X} since the last such line, still touching, not meetings", s_touches, clash.FormId);
                s_touches = 0;
                s_nextSaid = cNow + std::chrono::seconds(5);
            }
            continue;
        }
        ClashRequest request{};
        request.OtherId = view.get<RemoteComponent>(*it).Id;
        request.OtherSide = clash.Side;
        request.OwnSide = clash.OwnSide;
        request.Point = clash.Point;
        request.Speed = clash.Speed;
        request.Tick = cTick;
        m_transport.Send(request);
        spdlog::info("Clash: our {} ({:.0f} units from its HIGGS body) met the {} weapon of {:X} (server {}) at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units a second; felt{} here, sent",
                     clash.OwnSide == 0 ? "left hand" : clash.OwnSide == 1 ? "right hand" : "hand or weapon", clash.HandDistance, clash.Side ? "right" : "left", clash.FormId, request.OtherId,
                     clash.Point.x, clash.Point.y, clash.Point.z, clash.Speed, clash.Heard ? " and heard" : "");
    }
#endif
}

void CharacterService::OnNotifyClash(const NotifyClash& acMessage) const noexcept
{
    // Every player near it is told; the one whose weapon it was feels it.
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto it = std::find_if(view.begin(), view.end(), [&view](auto entity) { return view.get<FormIdComponent>(entity).Id == 0x14; });
    const bool cOurs = it != view.end() && view.get<LocalComponent>(*it).Id == acMessage.OtherId;
    const glm::vec3 point(acMessage.Point);
    const int64_t cAgo = static_cast<int64_t>(m_transport.GetClock().GetCurrentTick()) - static_cast<int64_t>(acMessage.Tick);
    spdlog::info("Clash from server {}: their {} met {} {} weapon at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units a second, {} ms ago", acMessage.FromId,
                 acMessage.OwnSide == 0 ? "left hand" : acMessage.OwnSide == 1 ? "right hand" : "hand or weapon", cOurs ? "our" : fmt::format("{}'s", acMessage.OtherId),
                 acMessage.OtherSide ? "right" : "left", point.x, point.y, point.z, acMessage.Speed, cAgo);
#ifdef SKYRIMVR
    // Heard by everyone near it, felt by the one whose weapon it was.
    const bool cHeard = VRBodySync::PlayClashSound(point);
    VRBodySync::PlayClashSparks(point);
    if (cOurs)
        VRBodySync::PulseClashHaptics(acMessage.OtherSide);
    spdlog::info("Clash from server {}: {}{}", acMessage.FromId, cHeard ? "heard" : "not heard (no sound)", cOurs ? ", felt" : "");
#endif
}

void CharacterService::OnNotifyArrowNocked(const NotifyArrowNocked& acMessage) const noexcept
{
    auto view = m_world.view<RemoteComponent, FormIdComponent>();
    const auto it = std::find_if(view.begin(), view.end(), [&view, &acMessage](auto entity) { return view.get<RemoteComponent>(entity).Id == acMessage.ServerId; });
    if (it == view.end())
        return;

    Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(*it).Id));
    if (!pActor || !pActor->GetNiNode() || pActor->IsDead())
        return;

    // The arrow is the game's own: it takes the copy's equipped ammo, so none means nothing to put on the string.
    const bool cWanted = acMessage.Nocked || acMessage.InHand;
    if (cWanted && (!pActor->GetEquippedAmmo() || !pActor->actorState.IsWeaponDrawn()))
    {
        spdlog::info("ArrowNocked: {:X} (server {:X}) has an arrow {} over there, but this copy has {}; not attached", pActor->formID, acMessage.ServerId,
                     acMessage.Nocked ? "on the string" : "in the hand", pActor->GetEquippedAmmo() ? "its weapon sheathed" : "no arrows equipped");
        return;
    }

    // Actor::AttachArrow and DetachArrow take the actor's biped (TESObjectREFR::GetBiped2, slot 0x7F on every runtime).
    // They are called by slot because Actor.h declares the SE slot (0xCD) and VR has them two further on.
    // CommonLibVR: RelocateVirtual(0x0CD, 0x0CF) and (0x0CE, 0x0D0).
#ifdef SKYRIMVR
    constexpr size_t cAttachSlot = 0xCF;
    constexpr size_t cDetachSlot = 0xD0;
#else
    constexpr size_t cAttachSlot = 0xCD;
    constexpr size_t cDetachSlot = 0xCE;
#endif
    constexpr size_t cGetBiped2Slot = 0x7F;
    using TGetBiped = void* (*)(Actor*);
    using TArrow = void (*)(Actor*, void*);
    void** pVtable = *reinterpret_cast<void***>(pActor);
    void* pBipedHolder = reinterpret_cast<TGetBiped>(pVtable[cGetBiped2Slot])(pActor);
    if (!pBipedHolder || !*static_cast<void**>(pBipedHolder))
        return;
#ifdef SKYRIMVR
    size_t before = 0;
    VRBodySync::NodesNamedLike(pActor, "arrow", 0, before);
    // An arrow going from the hand to the string, or back, is the same arrow: only how it is held changes.
    const bool cHad = VRBodySync::HasCopyArrow(pActor->formID);
    VRBodySync::ArrowHold hold{};
    hold.InHand = acMessage.InHand && !acMessage.Nocked;
    hold.Side = acMessage.HandSide;
    hold.Offset = acMessage.HandOffset;
    hold.Rotation = acMessage.HandRotation;
    VRBodySync::SetArrowNocked(pActor->formID, acMessage.Nocked, hold);
    if (cWanted != cHad)
        reinterpret_cast<TArrow>(pVtable[cWanted ? cAttachSlot : cDetachSlot])(pActor, pBipedHolder);
#else
    reinterpret_cast<TArrow>(pVtable[cWanted ? cAttachSlot : cDetachSlot])(pActor, pBipedHolder);
#endif
    spdlog::info("ArrowNocked: {:X} (server {:X}): arrow {}", pActor->formID, acMessage.ServerId,
                 acMessage.Nocked ? "put on the string" : acMessage.InHand ? "held in the hand" : "taken off the string");
#ifdef SKYRIMVR
    // TEMPORARY: which arrow nodes the copy has, until the arrow is checked in a session.
    size_t after = 0;
    const std::string names = VRBodySync::NodesNamedLike(pActor, "arrow", 16, after);
    spdlog::info("ArrowNocked: {:X} has {} node(s) named like an arrow, {} before{}{}", pActor->formID, after, before, names.empty() ? "" : ": ", names);
#endif
}
