#include <Services/PlayerService.h>
#include <PerfScope.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/PlayerDialogueEvent.h>
#include <Events/PlayerLevelEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/BeastFormChangeEvent.h>

#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/PlayerDownedRequest.h>
#include <Messages/NotifyDownedState.h>
#include <Messages/SafePointRequest.h>
#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/PlayerPlaceRequest.h>

#include <Structs/ServerSettings.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <CrashHandler.h>
#include <Forms/TESGlobal.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <AI/AIProcess.h>
#include <EquipManager.h>
#include <Forms/TESRace.h>
#include <Interface/UI.h>
#include <Services/PartyService.h>

namespace
{
// A third of the health back: enough to get away, not enough to fight on as if nothing happened.
constexpr float cRevivedHealthShare = 0.3f;

// The cell of a safe point: the interior itself, or the exterior cell at its position in its worldspace, which may need loading.
TESObjectCELL* FindSafePointCell(World& aWorld, const GameId& acCellId, const GameId& acWorldSpaceId, const glm::vec3& acPosition) noexcept
{
    auto& modSystem = aWorld.GetModSystem();
    if (TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(modSystem.GetGameId(acCellId))))
        return pCell;
    if (!acWorldSpaceId)
        return nullptr;
    TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(modSystem.GetGameId(acWorldSpaceId)));
    if (!pWorldSpace)
        return nullptr;
    const GridCellCoords cCoordinates = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return pWorldSpace->LoadCell(cCoordinates.X, cCoordinates.Y);
}
} // namespace

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&PlayerService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&PlayerService::OnDisconnected>(this);
    m_settingsConnection = m_dispatcher.sink<ServerSettings>().connect<&PlayerService::OnServerSettingsReceived>(this);
    m_notifyRespawnConnection = m_dispatcher.sink<NotifyPlayerRespawn>().connect<&PlayerService::OnNotifyPlayerRespawn>(this);
    m_downedStateConnection = m_dispatcher.sink<NotifyDownedState>().connect<&PlayerService::OnNotifyDownedState>(this);
    m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&PlayerService::OnGridCellChangeEvent>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&PlayerService::OnCellChangeEvent>(this);
    m_playerDialogueConnection = m_dispatcher.sink<PlayerDialogueEvent>().connect<&PlayerService::OnPlayerDialogueEvent>(this);
    m_playerLevelConnection = m_dispatcher.sink<PlayerLevelEvent>().connect<&PlayerService::OnPlayerLevelEvent>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PlayerService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PlayerService::OnPartyLeftEvent>(this);
}

void PlayerService::OnUpdate(const UpdateEvent&) noexcept
{
    PerfScope perfScope("PlayerService::OnUpdate");

    RunSafePointUpdates();
    RunRespawnUpdates();
    RunPostDeathUpdates();
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunBeastFormDetection();
    RunPlaceUpdates();
    RunCrashPlaceNote();
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // Every connection starts with the place unknown to the server: send it at the next check.
    m_lastPlace.clear();
    m_nextPlaceCheck = {};

    // The server keeps no safe point from an earlier connection: where the player stands now is the first, as a loaded save is.
    m_safePointAt = std::chrono::steady_clock::now() + 3s;

    // TODO: SkyrimTogether.esm
    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 0.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->f = 0.f;
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);
    m_serverDifficulty = m_previousDifficulty = 6;

    // Down or respawning as the connection goes: health above zero first, or the game kills the player once it is no longer essential and loads the last save (live-down-disconnect).
    // Then the game's own bleedout recovery stands the player up where it lies; nothing here waits for a revive any more.
    if (m_downState != DownState::kUp)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, std::max(1.f, pPlayer->GetActorPermanentValue(ActorValueInfo::kHealth) * cRevivedHealthShare));
        if (m_downState == DownState::kRespawning)
            FadeOutGame(false, true, 0.5f, true, 2.f);
        spdlog::info("PlayerService: the connection dropped while the player was down; up again where it lies");
    }
    ToggleDeathSystem(false);
    m_downState = DownState::kUp;
    m_standingUp = false;
    m_safePointAt = {};

    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 1.f;

    // Restore to the default value (150 in skyrim, 175 in fallout 4)
    float* greetDistance = Settings::GetGreetDistance();
    *greetDistance = 150.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->f = 1.f;
}

void PlayerService::OnServerSettingsReceived(const ServerSettings& acSettings) noexcept
{
    m_previousDifficulty = *Settings::GetDifficulty();
    PlayerCharacter::Get()->SetDifficulty(acSettings.Difficulty);
    m_serverDifficulty = acSettings.Difficulty;

    if (!acSettings.GreetingsEnabled)
    {
        float* greetDistance = Settings::GetGreetDistance();
        *greetDistance = 0.f;
    }

    ToggleDeathSystem(acSettings.DeathSystemEnabled);
}

void PlayerService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept
{
    PlayerCharacter::Get()->PayGold(acMessage.GoldLost);

    std::string message = fmt::format("You died and lost {} gold.", acMessage.GoldLost);
    Utils::ShowHudMessage(String(message));
}

// Only this player's own state; the party's downed members are ReviveService's.
void PlayerService::OnNotifyDownedState(const NotifyDownedState& acMessage) noexcept
{
    if (acMessage.PlayerId != m_transport.GetLocalPlayerId())
        return;

    // Where the server says the player respawns; without its word, this game's own last safe point.
    SafePoint where = m_ownSafePoint;
    if (acMessage.HasRespawnPoint)
    {
        where.Known = true;
        where.CellId = acMessage.RespawnCellId;
        where.WorldSpaceId = acMessage.RespawnWorldSpaceId;
        where.Position = acMessage.RespawnPosition;
    }
    const char* cpComeBack = where.Known ? "you come back at the last safe point" : "you come back nearby";
    const char* cpLogWhere = where.Known ? "at the safe point" : "at the game's own spot";
    const auto& cParty = m_world.GetPartyService();
    const bool cAlone = !cParty.IsInParty() || cParty.GetPartyMembers().size() < 2;

    switch (acMessage.Change)
    {
    case NotifyDownedState::kDown:
        if (m_downState != DownState::kDown)
            return;
        // The server ends the wait (kDied); this game steps in only if that word never comes.
        m_respawnDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(acMessage.SecondsLeft) + 10s;
        spdlog::info("PlayerService: the server gives {} s to be revived", acMessage.SecondsLeft);
        if (acMessage.SecondsLeft > 0)
            Utils::ShowHudMessage(String(fmt::format("urSovngarde: you are down; your party has {} seconds to revive you", acMessage.SecondsLeft)));
        break;
    case NotifyDownedState::kRevived:
        if (m_downState != DownState::kDown)
        {
            spdlog::info("PlayerService: revived by player {}, but already respawning; the respawn goes on", acMessage.ReviverId);
            return;
        }
        StandUp(acMessage.ReviverId);
        break;
    case NotifyDownedState::kDied:
        if (m_downState != DownState::kDown)
            return;
        spdlog::info("PlayerService: {}; respawning {}", cAlone ? "down alone" : "not revived in time", cpLogWhere);
        Utils::ShowHudMessage(String(fmt::format("urSovngarde: {}; {}", cAlone ? "you died" : "nobody revived you in time", cpComeBack)));
        StartRespawn(where);
        break;
    case NotifyDownedState::kPartyFallen:
        // A party that lost its standing members leaves this player alone: nobody is left, rather than a party fallen.
        Utils::ShowHudMessage(String(fmt::format("urSovngarde: {}; {}", cAlone ? "nobody is left to revive you" : "your party has fallen", cpComeBack)));
        if (m_downState == DownState::kDown)
        {
            spdlog::info("PlayerService: {}; respawning {}", cAlone ? "nobody is left to revive the player" : "the whole party is down", cpLogWhere);
            StartRespawn(where);
        }
        break;
    case NotifyDownedState::kRespawned:
    case NotifyDownedState::kStoodUp:
    case NotifyDownedState::kReviveRefused: break;
    }
}

void PlayerService::OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept
{
    uint32_t baseId = 0;
    uint32_t modId = 0;

    if (m_world.GetModSystem().GetServerModId(acEvent.WorldSpaceId, modId, baseId))
    {
        ShiftGridCellRequest request;
        request.WorldSpaceId = GameId(modId, baseId);
        request.PlayerCell = acEvent.PlayerCell;
        request.CenterCoords = acEvent.CenterCoords;
        request.Cells = acEvent.Cells;

        m_transport.Send(request);
    }
}

void PlayerService::OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept
{
    if (acEvent.WorldSpaceId)
    {
        EnterExteriorCellRequest message;
        message.CellId = acEvent.CellId;
        message.WorldSpaceId = acEvent.WorldSpaceId;
        message.CurrentCoords = acEvent.CurrentCoords;

        m_transport.Send(message);
    }
    else
    {
        EnterInteriorCellRequest message;
        message.CellId = acEvent.CellId;

        m_transport.Send(message);
    }
}

void PlayerService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty())
        return;

    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;

    m_transport.Send(request);
}

void PlayerService::OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerLevelRequest request{};
    request.NewLevel = PlayerCharacter::Get()->GetLevel();

    m_transport.Send(request);
}

void PlayerService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (acEvent.IsLeader)
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 1.f;
    }
}

void PlayerService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (World::Get().GetTransport().IsConnected())
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 0.f;
    }
}

// The respawn steps are timed with a steady clock, not frame deltas, so a stutter cannot leave the camera stuck.
// Upstream fix for issue #878.
void PlayerService::RunRespawnUpdates() noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto cNow = std::chrono::steady_clock::now();

    if (m_downState == DownState::kUp)
    {
        // Revived and still getting up: that bleedout is ending, not starting (RunPostDeathUpdates).
        if (m_standingUp)
            return;

        if (!pPlayer->actorState.IsBleedingOut())
        {
            m_cachedMainSpellId = pPlayer->magicItems[0] ? pPlayer->magicItems[0]->formID : 0;
            m_cachedSecondarySpellId = pPlayer->magicItems[1] ? pPlayer->magicItems[1]->formID : 0;
            m_cachedPowerId = pPlayer->equippedShout ? pPlayer->equippedShout->formID : 0;
            return;
        }

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        // Below 0 too (a blow that took more than was left): a revive then gives the same share of health whatever the last hit was.
        if (pPlayer->GetActorValue(ActorValueInfo::kHealth) != 0.f)
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 0);

        // The server answers with how long the party has to revive the player, or at once that it respawns (alone) (OnNotifyDownedState).
        // Until it does, the wait is the old respawn delay, so a lost answer cannot leave the player down for good.
        m_downState = DownState::kDown;
        m_downedAt = cNow;
        m_respawnDeadline = cNow + 5s;
        m_standingUp = false;
        m_loadingSeen = false;
        m_transport.Send(PlayerDownedRequest());
        spdlog::info("PlayerService: player is down, waiting for a revive");
        return;
    }

    if (m_downState == DownState::kDown)
    {
        if (!pPlayer->actorState.IsBleedingOut())
        {
            // After a loading screen the player is up in a loaded save: no respawn, as when playing alone.
            if (m_loadingSeen)
            {
                spdlog::info("PlayerService: a save was loaded while down; up again without a respawn");
                m_downState = DownState::kUp;
                PlayerDownedRequest up{};
                up.IsDown = false;
                m_transport.Send(up);
                return;
            }

            // A bleedout that ends by itself cannot be revived any more; the player respawns as it always did.
            spdlog::info("PlayerService: the bleedout ended by itself {} ms after going down; respawning",
                         std::chrono::duration_cast<std::chrono::milliseconds>(cNow - m_downedAt).count());
            StartRespawn(m_ownSafePoint);
        }
        else if (cNow >= m_respawnDeadline)
        {
            spdlog::info("PlayerService: no word from the server on the wait; respawning");
            StartRespawn(m_ownSafePoint);
        }
        return;
    }

    // A save loaded during the fade: the player is in that save now, so the respawn is called off and the screen comes back.
    if (m_loadingSeen)
    {
        spdlog::info("PlayerService: a save was loaded during the respawn; the respawn is called off");
        m_downState = DownState::kUp;
        FadeOutGame(false, true, 0.5f, true, 2.f);
        PlayerDownedRequest up{};
        up.IsDown = false;
        m_transport.Send(up);
        return;
    }

    // Once the screen is fading to black, finish the respawn even if bleedout ends early, as it does on VR.
    if (cNow >= m_respawnAt)
    {
        spdlog::info("PlayerService: respawning player");
        TESObjectCELL* pSafeCell = nullptr;
        if (m_respawnPoint.Known)
        {
            pSafeCell = FindSafePointCell(m_world, m_respawnPoint.CellId, m_respawnPoint.WorldSpaceId, m_respawnPoint.Position);
            if (!pSafeCell)
                spdlog::warn("PlayerService: the safe point's cell is not in this game; respawning at the game's own spot");
        }
        pPlayer->RespawnPlayer(pSafeCell, m_respawnPoint.Position);

        m_knockdownDeadline = std::chrono::steady_clock::now() + 1500ms;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        m_downState = DownState::kUp;

        auto* pEquipManager = EquipManager::Get();
        TESForm* pSpell = TESForm::GetById(m_cachedMainSpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 0);
        pSpell = TESForm::GetById(m_cachedSecondarySpellId);
        if (pSpell)
            pEquipManager->EquipSpell(pPlayer, pSpell, 1);
        pSpell = TESForm::GetById(m_cachedPowerId);
        if (pSpell)
            pEquipManager->EquipShout(pPlayer, pSpell);

        m_knockdownDeadline = std::chrono::steady_clock::now() + 1500ms;
        m_knockdownStart = true;
    }
}

void PlayerService::StartRespawn(const SafePoint& acWhere) noexcept
{
    m_downState = DownState::kRespawning;
    m_respawnPoint = acWhere;
    m_loadingSeen = false;
    // The screen is black after 5 s (2 s, then a 3 s fade): the respawn happens then.
    m_respawnAt = std::chrono::steady_clock::now() + 5s;
    FadeOutGame(true, true, 3.0f, true, 2.0f);
    PlayerCharacter::Get()->PayCrimeGoldToAllFactions();
}

void PlayerService::RunSafePointUpdates() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextMenuCheck)
        return;
    m_nextMenuCheck = cNow + 250ms;

    const UI* pUI = UI::Get();
    if (!pUI)
        return;
    // A loading screen counts from when the game queues it (UI::LoadingScreensQueued): this update may not run while one shows.
    const uint32_t cLoadingScreens = UI::LoadingScreensQueued();
    const bool cLoading = cLoadingScreens != m_loadingScreens || pUI->GetMenuOpen(BSFixedString("Loading Menu"));
    m_loadingScreens = cLoadingScreens;
    const bool cResting = pUI->GetMenuOpen(BSFixedString("Sleep/Wait Menu"));
    // The game autosaves as a loading screen or a sleep or wait ends; two seconds after the last sign of one, the player stands where it was placed.
    if (cLoading)
    {
        m_loadingSeen = true;
        m_safePointAt = cNow + 2s;
    }
    if (m_restMenuOpen && !cResting)
        m_safePointAt = cNow + 2s;
    m_restMenuOpen = cResting;

    if (m_safePointAt == std::chrono::steady_clock::time_point{} || cNow < m_safePointAt)
        return;
    m_safePointAt = {};
    RecordSafePoint();
}

void PlayerService::RecordSafePoint() noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
    if (!pCell || m_downState != DownState::kUp || m_standingUp || pPlayer->actorState.IsBleedingOut())
        return;
    // A fight here would greet the player again as it came back; the last safe point stays.
    if (pPlayer->IsInCombat())
    {
        spdlog::info("PlayerService: no safe point here, the player is in combat");
        return;
    }

    auto& modSystem = m_world.GetModSystem();
    SafePointRequest request{};
    if (!modSystem.GetServerModId(pCell->formID, request.CellId))
        return;
    if (const TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
    request.Position = pPlayer->position;
    m_transport.Send(request);

    m_ownSafePoint.Known = true;
    m_ownSafePoint.CellId = request.CellId;
    m_ownSafePoint.WorldSpaceId = request.WorldSpaceId;
    m_ownSafePoint.Position = pPlayer->position;
    const char* cpEditorId = pCell->GetFormEditorID();
    spdlog::info("PlayerService: safe point in cell {:X} '{}' at ({:.0f}, {:.0f}, {:.0f})", pCell->formID, cpEditorId ? cpEditorId : "", pPlayer->position.x, pPlayer->position.y,
                 pPlayer->position.z);
}

void PlayerService::StandUp(const uint32_t aReviverId) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto cNow = std::chrono::steady_clock::now();

    // Bleedout ends once the game may recover the player and its health is above zero.
    // Recovery is blocked again once the player is up (RunPostDeathUpdates), for the next time it goes down.
    pPlayer->SetNoBleedoutRecovery(false);
    // The game's own recovery gives the health back in full as it stands the player up, so the share is applied again once up.
    m_revivedHealth = std::max(1.f, pPlayer->GetActorPermanentValue(ActorValueInfo::kHealth) * cRevivedHealthShare);
    pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, m_revivedHealth);
    m_standingUp = true;
    m_standUpDeadline = cNow + 5s;

    // A moment to get up before the enemies can take the player down again.
    PlayerCharacter::SetGodMode(true);
    m_godmodeStart = true;
    m_godmodeDeadline = cNow + 3s;

    m_downState = DownState::kUp;

    const auto& players = m_world.GetPartyService().GetPlayers();
    const auto name = players.find(aReviverId);
    const String who = name != players.end() ? name->second : String("A party member");
    spdlog::info("PlayerService: revived by player {} '{}' after {} ms down; health {:.0f}", aReviverId, who.c_str(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(cNow - m_downedAt).count(), pPlayer->GetActorValue(ActorValueInfo::kHealth));
    Utils::ShowHudMessage(String("urSovngarde: ") + who + " revived you");
}

// Doesn't seem to respawn quite yet
void PlayerService::RunPostDeathUpdates() noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    // If a player dies in ragdoll, it gets stuck.
    // This code ragdolls the player again upon respawning.
    // It also makes the player invincible for 10 seconds.
    const auto now = std::chrono::steady_clock::now();
    if (m_knockdownStart && now >= m_knockdownDeadline)
    {
        spdlog::info("PlayerService: respawn done, fading back in");

        // Fade in first, so the player can see again even if something below fails.
        FadeOutGame(false, true, 0.5f, true, 2.f);

        PlayerCharacter::SetGodMode(true);
        m_godmodeStart = true;
        m_godmodeDeadline = now + 10s;

        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        // TEMPORARY: logs whether a respawn gives the health back, here and when its god mode ends.
        spdlog::info("PlayerService: health {:.0f} after the respawn, bleeding out {}", pPlayer->GetActorValue(ActorValueInfo::kHealth),
                     pPlayer->actorState.IsBleedingOut());
#ifndef SKYRIMVR
        // On VR this knock throws the player thousands of units from where it respawned (live-revive-timeout, "spot back").
        if (pPlayer->currentProcess)
            pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);
#endif

        m_knockdownStart = false;
    }

    if (m_godmodeStart && now >= m_godmodeDeadline)
    {
        PlayerCharacter::SetGodMode(false);
        m_godmodeStart = false;
        const PlayerCharacter* pPlayer = PlayerCharacter::Get();
        spdlog::info("PlayerService: god mode of the respawn over; health {:.0f}, bleeding out {}", pPlayer->GetActorValue(ActorValueInfo::kHealth),
                     pPlayer->actorState.IsBleedingOut());
    }

    // A revived player: once up, the next zero health must be a bleedout it cannot get out of by itself again.
    // Still down when the wait ends, the revive did not take, and the player respawns rather than lie there for good.
    if (m_standingUp)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        if (!pPlayer->actorState.IsBleedingOut())
        {
            m_standingUp = false;
            pPlayer->SetNoBleedoutRecovery(true);
            const float cGiven = pPlayer->GetActorValue(ActorValueInfo::kHealth);
            if (cGiven > m_revivedHealth)
                pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, m_revivedHealth);
            spdlog::info("PlayerService: the revived player is up; health {:.0f} (the game gave {:.0f})", pPlayer->GetActorValue(ActorValueInfo::kHealth), cGiven);
        }
        else if (now >= m_standUpDeadline)
        {
            m_standingUp = false;
            pPlayer->SetNoBleedoutRecovery(true);
            spdlog::warn("PlayerService: revived, but still in bleedout 5 s later; respawning");
            StartRespawn(m_ownSafePoint);
        }
    }
}

void PlayerService::RunDifficultyUpdates() const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter::Get()->SetDifficulty(m_serverDifficulty);
}

void PlayerService::RunLevelUpdates() const noexcept
{
    // The LevelUp hook is kinda weird, so ehh, just check periodically, doesn't really cost anything.

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static uint16_t oldLevel = PlayerCharacter::Get()->GetLevel();

    uint16_t newLevel = PlayerCharacter::Get()->GetLevel();
    if (newLevel != oldLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = newLevel;

        m_transport.Send(request);

        oldLevel = newLevel;
    }
}

// Sends where the player is for the public server page, when it changes (checked once a second).
// Indoors the room's name, outdoors the location's, else the worldspace's.
void PlayerService::RunPlaceUpdates() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextPlaceCheck || !m_transport.IsConnected())
        return;
    m_nextPlaceCheck = now + std::chrono::seconds(1);

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto nameOf = [](const TESForm* apForm) -> const char* {
        const char* pName = apForm ? apForm->GetName() : nullptr;
        return pName && *pName ? pName : nullptr;
    };

    TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
    const char* pPlace = pWorldSpace ? nullptr : nameOf(pPlayer->GetParentCellEx());
    if (!pPlace)
        pPlace = nameOf(pPlayer->locationForm);
    if (!pPlace)
        pPlace = nameOf(pWorldSpace);

    const String place = pPlace ? pPlace : "";
    if (place == m_lastPlace)
        return;
    m_lastPlace = place;

    PlayerPlaceRequest request{};
    request.Place = place;
    m_transport.Send(request);
}

// Notes where the player is once a second, as the line a crash will log (CrashPlace).
// The launcher parses this line, so keep its words. Its two forms:
//   worldspace Tamriel, cell 961B, place 'Whiterun', at (17310, -14345)
//   interior cell 1A27F, place 'Dragonsreach', outside Tamriel (19447, -7409)
// "outside" is where the player last stood in an exterior this session.
// A worldspace is named by its editor id, which does not change with the load order; a hex form id if it has none.
void PlayerService::RunCrashPlaceNote() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextCrashPlaceNote)
        return;
    m_nextCrashPlaceNote = now + std::chrono::seconds(1);

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
    const auto nameOf = [](const TESForm* apForm) -> const char* {
        const char* pName = apForm ? apForm->GetName() : nullptr;
        return pName && *pName ? pName : nullptr;
    };
    const TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
    const TESObjectCELL* pCell = pPlayer->GetParentCellEx();
    const char* pPlace = nameOf(pPlayer->locationForm);
    if (!pPlace)
        pPlace = nameOf(pCell);
    if (!pPlace)
        pPlace = nameOf(pWorldSpace);
    const std::string place = pPlace ? fmt::format("'{}'", pPlace) : std::string("none");
    const uint32_t cellId = pCell ? pCell->formID : 0;

    std::string line;
    if (pWorldSpace)
    {
        const char* pEditorId = const_cast<TESWorldSpace*>(pWorldSpace)->GetFormEditorID();
        m_outsideWorldSpace = pEditorId && *pEditorId ? String(pEditorId) : String(fmt::format("{:X}", pWorldSpace->formID).c_str());
        m_outsideX = pPlayer->position.x;
        m_outsideY = pPlayer->position.y;
        line = fmt::format("worldspace {}, cell {:X}, place {}, at ({:.0f}, {:.0f})", m_outsideWorldSpace, cellId, place, pPlayer->position.x, pPlayer->position.y);
    }
    else if (!m_outsideWorldSpace.empty())
        line = fmt::format("interior cell {:X}, place {}, outside {} ({:.0f}, {:.0f})", cellId, place, m_outsideWorldSpace, m_outsideX, m_outsideY);
    else
        line = fmt::format("interior cell {:X}, place {}", cellId, place);
    CrashPlace::Note(line.c_str());
}

void PlayerService::RunBeastFormDetection() const noexcept
{
    static uint32_t lastRaceFormID = 0;
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer->race)
        return;

    if (pPlayer->race->formID == lastRaceFormID)
        return;

    if (pPlayer->race->formID == 0x200283A || pPlayer->race->formID == 0xCDD84)
        m_world.GetDispatcher().trigger(BeastFormChangeEvent());

    lastRaceFormID = pPlayer->race->formID;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    PlayerCharacter::Get()->SetPlayerRespawnMode(aSet);
}
