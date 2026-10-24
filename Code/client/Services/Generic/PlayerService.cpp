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
#include <Games/Skyrim/VRHaptics.h>
#include <Games/Skyrim/VRBanner.h>
#include <Games/Skyrim/DeathCause.h>
#include <Games/Skyrim/DeathSentence.h>
#include <Components.h>

namespace
{
// A third of the health back: enough to get away, not enough to fight on as if nothing happened.
constexpr float cRevivedHealthShare = 0.3f;

// While down: the game's own low-health heartbeat (Skyrim.esm UIHealthHeartbeatALPSD), a loop that plays until stopped.
constexpr uint32_t cHeartbeatSound = 0x4DAAD;
// Revived: the restoration spell's cast (Skyrim.esm MAGRestorationFFFireSD).
constexpr uint32_t cReviveSound = 0x3F361;
// Going down: the sneak attack's sting (Skyrim.esm UISneakAttack), heard at once so the down is not taken for a freeze.
constexpr uint32_t cDownedSound = 0xC06CE;
// The unarmed "weapon" a creature's or a fist's blow comes from: not worth naming.
constexpr uint32_t cUnarmed = 0x1F4;
// A drop this far within this long before going down is a fall; a hit in the last moments before it is what downed the player instead.
constexpr float cFallDrop = 500.f;
constexpr auto cFallWindow = 1200ms;
constexpr auto cKillingBlow = 500ms;
// The blow that downed the player is the last one this long before it went down.
constexpr auto cBlowsCounted = 10s;
// The heartbeat felt in both hands: two beats, then a pause, about 55 a minute.
constexpr auto cHeartbeatCycle = 1100ms;
constexpr auto cFirstBeat = 70ms;
constexpr auto cSecondBeatAt = 250ms;
constexpr auto cSecondBeat = 50ms;
constexpr auto cRevivePulse = 300ms;
// Both grips held this long while down gives up the wait.
constexpr auto cGiveUpHold = 3s;
// "YOU DIED" in a dark red as the screen fades out; the place's name in gold as it fades back in.
constexpr uint32_t cDiedColour = 0xA31919;
constexpr uint32_t cPlaceColour = 0xD4AF37;

// The name a player gives where it stands: its location ("Riverwood"), else its cell's, else its worldspace's.
const char* PlaceNameOf(const PlayerCharacter* apPlayer) noexcept
{
    const auto nameOf = [](const TESForm* apForm) -> const char*
    {
        const char* pName = apForm ? apForm->GetName() : nullptr;
        return pName && *pName ? pName : nullptr;
    };
    const char* pPlace = nameOf(apPlayer->locationForm);
    if (!pPlace)
        pPlace = nameOf(apPlayer->GetParentCellEx());
    if (!pPlace)
        pPlace = nameOf(apPlayer->GetWorldSpace());
    return pPlace;
}

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

// The name a reviver goes by in the party, as the banner shows it.
String ReviverName(const PartyService& acParty, const uint32_t aReviverId) noexcept
{
    const auto& cPlayers = acParty.GetPlayers();
    const auto cName = cPlayers.find(aReviverId);
    return cName != cPlayers.end() ? cName->second : String("A party member");
}
} // namespace

#ifdef SKYRIMVR
#include <Interface/Menus/MapMenu.h>
#include <Interface/UI.h>
#endif

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

    DeathCause::Start();
}

void PlayerService::OnUpdate(const UpdateEvent&) noexcept
{
    PerfScope perfScope("PlayerService::OnUpdate");

    RunSafePointUpdates();
    RunRespawnUpdates();
    RunPostDeathUpdates();
    RunDownedCues();
    VRBanner::Update();
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunBeastFormDetection();
    RunPlaceUpdates();
    RunCrashPlaceNote();
#ifdef SKYRIMVR
    RunMapCloseUpdates();
#endif
}

#ifdef SKYRIMVR
// The map does not pause the game online (UI.cpp kAllowList), so a player can be worn down while reading it.
// Any health loss closes it, a poison drain included; regeneration and potions only raise it.
// This only asks, because it runs on a worker thread.
void PlayerService::RunMapCloseUpdates() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const float cHealth = pPlayer->GetActorValue(ActorValueInfo::kHealth);

    static const BSFixedString s_mapName("MapMenu");
    if (m_lastHealth >= 0.f && cHealth < m_lastHealth && IsMenuOpenUnpaused(s_mapName))
        CloseMapMenu();

    m_lastHealth = cHealth;
}
#endif

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
    StopHeartbeat();
    m_giveUpSince = {};
    m_revivePulseUntil = {};
    VRBanner::Hide();
    // What a revive or a respawn left running is finished now: with the death system off, RunPostDeathUpdates no longer runs.
    if (m_godmodeStart)
    {
        PlayerCharacter::SetGodMode(false);
        m_godmodeStart = false;
    }
    if (m_knockdownStart)
    {
        FadeOutGame(false, true, 0.5f, true, 2.f);
        m_knockdownStart = false;
    }
    ToggleDeathSystem(false);
    m_downState = DownState::kUp;
    m_standingUp = false;
    m_safePointAt = {};
    // The server forgets safe points between connections, and so does this game: the next save may be elsewhere entirely.
    m_ownSafePoint = {};

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
#ifdef SKYRIMVR
        if (acMessage.SecondsLeft > 0)
            Utils::ShowHudMessage(String(fmt::format("urSovngarde: you are down; your party has {} seconds to revive you, or hold both grips to give up", acMessage.SecondsLeft)));
#else
        if (acMessage.SecondsLeft > 0)
            Utils::ShowHudMessage(String(fmt::format("urSovngarde: you are down; your party has {} seconds to revive you", acMessage.SecondsLeft)));
#endif
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
        if (m_downState != DownState::kDown)
            return;
        // A party that lost its standing members leaves this player alone: nobody is left, rather than a party fallen.
        Utils::ShowHudMessage(String(fmt::format("urSovngarde: {}; {}", cAlone ? "nobody is left to revive you" : "your party has fallen", cpComeBack)));
        spdlog::info("PlayerService: {}; respawning {}", cAlone ? "nobody is left to revive the player" : "the whole party is down", cpLogWhere);
        StartRespawn(where);
        break;
    case NotifyDownedState::kRespawned:
        // The server's answer to this game stopping the wait (StartRespawn): where the respawn under way puts the player.
        if (m_downState == DownState::kRespawning && acMessage.HasRespawnPoint)
        {
            m_respawnPoint = where;
            spdlog::info("PlayerService: the server sends the respawn to the party's safe point");
        }
        break;
    case NotifyDownedState::kReviveHeld:
    {
        if (m_downState != DownState::kDown)
            return;
        std::erase(m_revivers, acMessage.ReviverId);
        m_revivers.push_back(acMessage.ReviverId);
        const String cWho = ReviverName(cParty, acMessage.ReviverId);
        spdlog::info("PlayerService: player {} '{}' is reviving this player", acMessage.ReviverId, cWho.c_str());
        VRBanner::Show(std::string(cWho.c_str()) + " is reviving you", cPlaceColour, 72, 8.f);
        Utils::ShowHudMessage(String("urSovngarde: ") + cWho + " is reviving you");
        break;
    }
    case NotifyDownedState::kReviveLetGo:
    {
        if (m_downState != DownState::kDown)
            return;
        spdlog::info("PlayerService: player {} stopped reviving this player", acMessage.ReviverId);
        // Another player still holding keeps the banner up, and it names one of them if it named the one who let go.
        const bool cWasNamed = !m_revivers.empty() && m_revivers.back() == acMessage.ReviverId;
        std::erase(m_revivers, acMessage.ReviverId);
        // A reviver who left the party or the server sends no let-go, so such a player is dropped here rather than named.
        const auto& cMembers = cParty.GetPartyMembers();
        std::erase_if(m_revivers, [&cMembers](const uint32_t acId) { return std::find(cMembers.begin(), cMembers.end(), acId) == cMembers.end(); });
        if (m_revivers.empty())
            VRBanner::Hide();
        else if (cWasNamed)
        {
            const String cWho = ReviverName(cParty, m_revivers.back());
            spdlog::info("PlayerService: player {} '{}' is still reviving this player", m_revivers.back(), cWho.c_str());
            VRBanner::Show(std::string(cWho.c_str()) + " is reviving you", cPlaceColour, 72, 8.f);
        }
        break;
    }
    case NotifyDownedState::kStoodUp:
    case NotifyDownedState::kReviveRefused:
    case NotifyDownedState::kLeft: break;
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
            SampleHeight(cNow);
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
        m_revivers.clear();
        m_downingCause.reset();
        m_downedAt = cNow;
        m_respawnDeadline = cNow + 5s;
        m_standingUp = false;
        m_loadingSeen = false;
        m_giveUpSince = {};
        m_transport.Send(PlayerDownedRequest());
        float highest = pPlayer->position.z;
        for (const auto& [cAt, cHeight] : m_heights)
            if (cAt != std::chrono::steady_clock::time_point{} && cNow - cAt <= cFallWindow)
                highest = std::max(highest, cHeight);
        m_downFell = highest - pPlayer->position.z >= cFallDrop;
        m_heights = {};
        const bool cDownSound = GameSound::Play(cDownedSound, nullptr);
        spdlog::info("PlayerService: player is down, waiting for a revive; dropped {:.0f} just before; the down sound {}", highest - pPlayer->position.z,
                     cDownSound ? "playing" : "NOT playing");
        StartHeartbeat();
        return;
    }

    if (m_downState == DownState::kDown)
    {
        if (!m_downingCause && cNow >= m_downedAt + cKillingBlow)
            m_downingCause = DowningCause();

        if (!pPlayer->actorState.IsBleedingOut())
        {
            // After a loading screen the player is up in a loaded save: no respawn, as when playing alone.
            if (m_loadingSeen)
            {
                spdlog::info("PlayerService: a save was loaded while down; up again without a respawn");
                StopHeartbeat();
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
        else if (HeldToGiveUp(cNow))
        {
            // The server ends the down and answers with the party's respawn point (StartRespawn, kRespawned).
            spdlog::info("PlayerService: the player gave up the wait (both grips held {} s); respawning", std::chrono::duration_cast<std::chrono::seconds>(cGiveUpHold).count());
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
    StopHeartbeat();
    m_giveUpSince = {};
    // Held through the fade and the respawn; the place's name replaces it as the screen comes back (RunPostDeathUpdates).
    const std::string cLine = DeathLine();
    spdlog::info("PlayerService: death card: {}", cLine);
    VRBanner::ShowDeath("YOU DIED", DeathSentence::Capitals(cLine), cDiedColour, 5.f);
    Utils::ShowHudMessage(String(cLine.c_str()));
    m_downState = DownState::kRespawning;
    m_respawnPoint = acWhere;
    m_loadingSeen = false;
    // The screen is black after 5 s (2 s, then a 3 s fade): the respawn happens then.
    m_respawnAt = std::chrono::steady_clock::now() + 5s;
    FadeOutGame(true, true, 3.0f, true, 2.0f);
    PlayerCharacter::Get()->PayCrimeGoldToAllFactions();

    // When this game stopped waiting on its own, the server must end the down now: a revive accepted during the fade would be ignored here.
    // It answers where the respawn goes (kRespawned); when the server ended the down itself, it ignores this.
    if (m_transport.IsConnected())
    {
        PlayerDownedRequest respawning{};
        respawning.IsDown = false;
        respawning.Respawning = true;
        m_transport.Send(respawning);
    }
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

    // Felt and heard: one strong pulse in both hands and the restoration spell's sound, where the player lies.
    StopHeartbeat();
    m_revivePulseUntil = cNow + cRevivePulse;
    if (!GameSound::Play(cReviveSound, &pPlayer->position))
        spdlog::warn("PlayerService: the revive sound did not play");

    const auto& players = m_world.GetPartyService().GetPlayers();
    const auto name = players.find(aReviverId);
    const String who = name != players.end() ? name->second : String("A party member");
    spdlog::info("PlayerService: revived by player {} '{}' after {} ms down; health {:.0f}", aReviverId, who.c_str(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(cNow - m_downedAt).count(), pPlayer->GetActorValue(ActorValueInfo::kHealth));
    Utils::ShowHudMessage(String("urSovngarde: ") + who + " revived you");
    VRBanner::Hide();
}

void PlayerService::RunDownedCues() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_revivePulseUntil)
    {
        VRHaptics::Pulse(false, 3999);
        VRHaptics::Pulse(true, 3999);
        return;
    }
    if (m_downState != DownState::kDown)
        return;

    // Both grips held: a buzz rising towards the moment the player gives up, in place of the heartbeat.
    if (m_giveUpSince != std::chrono::steady_clock::time_point{})
    {
        const float cShare = std::min(1.f, std::chrono::duration<float>(cNow - m_giveUpSince) / std::chrono::duration<float>(cGiveUpHold));
        const auto cLength = static_cast<uint16_t>(400.f + cShare * 3500.f);
        VRHaptics::Pulse(false, cLength);
        VRHaptics::Pulse(true, cLength);
        return;
    }

    const auto cInCycle = (cNow - m_heartbeatSince) % cHeartbeatCycle;
    if (cInCycle < cFirstBeat || (cInCycle >= cSecondBeatAt && cInCycle < cSecondBeatAt + cSecondBeat))
    {
        VRHaptics::Pulse(false, 3999);
        VRHaptics::Pulse(true, 3999);
    }
}

bool PlayerService::HeldToGiveUp(const std::chrono::steady_clock::time_point aNow) noexcept
{
    const VRHaptics::Grips cGrips = VRHaptics::GripsHeld();
    if (!cGrips.Both())
    {
        if (m_giveUpSince != std::chrono::steady_clock::time_point{})
            spdlog::info("PlayerService: both grips let go before giving up");
        m_giveUpSince = {};
        return false;
    }
    if (m_giveUpSince == std::chrono::steady_clock::time_point{})
    {
        m_giveUpSince = aNow;
        spdlog::info("PlayerService: both grips held while down (OpenVR {}, game {}); giving up unless let go", cGrips.OpenVR, cGrips.Game);
    }
    return aNow - m_giveUpSince >= cGiveUpHold;
}

void PlayerService::SampleHeight(const std::chrono::steady_clock::time_point aNow) noexcept
{
    const auto& cLast = m_heights[(m_nextHeight + m_heights.size() - 1) % m_heights.size()];
    if (aNow - cLast.first < 50ms)
        return;
    m_heights[m_nextHeight] = {aNow, PlayerCharacter::Get()->position.z};
    m_nextHeight = (m_nextHeight + 1) % m_heights.size();
}

std::string PlayerService::DeathLine() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const char* pVictim = pPlayer->baseForm ? pPlayer->baseForm->GetName() : nullptr;
    const std::string cVictim = pVictim && *pVictim ? pVictim : "The Dragonborn";

    // A respawn sooner than the cause was told (down alone) tells it now.
    return DeathSentence::Describe(cVictim, m_downingCause ? *m_downingCause : DowningCause());
}

DeathSentence::Cause PlayerService::DowningCause() const noexcept
{
    // Blows on the player once down do not count; the game may tell of the last one a moment after.
    DeathSentence::Cause cause{};
    const auto cHit = DeathCause::LastBetween(m_downedAt - cBlowsCounted, m_downedAt + cKillingBlow);
    cause.Fell = m_downFell && (!cHit || cHit->At < m_downedAt - cKillingBlow);
    if (!cHit)
        return cause;

    // Another player is named as the party knows them.
    const auto& cPlayers = m_world.GetPartyService().GetPlayers();
    uint32_t playerId = cHit->AttackerPlayerId;
    Actor* pAttacker = Cast<Actor>(TESForm::GetById(cHit->AttackerFormId));
    if (!playerId && pAttacker && pAttacker->GetExtension() && pAttacker->GetExtension()->IsRemotePlayer())
    {
        auto view = m_world.view<FormIdComponent, PlayerComponent>();
        for (const auto cEntity : view)
            if (view.get<FormIdComponent>(cEntity).Id == pAttacker->formID)
                playerId = view.get<PlayerComponent>(cEntity).Id;
    }
    if (const auto cName = cPlayers.find(playerId); playerId && cName != cPlayers.end())
        cause.Killer = cName->second.c_str();
    else if (pAttacker && pAttacker->baseForm && pAttacker->baseForm->GetName())
        cause.Killer = pAttacker->baseForm->GetName();
    else if (playerId)
        cause.Killer = "another player";

    // What it was done with, and how.
    const TESForm* pSource = cHit->SourceFormId ? TESForm::GetById(cHit->SourceFormId) : nullptr;
    const FormType cType = pSource ? pSource->formType : FormType::Count;
    if (cType == FormType::Spell || cType == FormType::Scroll || cType == FormType::Enchantment || cType == FormType::Shout)
        cause.Kind = DeathSentence::How::kMagic;
    else if (cHit->ProjectileFormId)
        cause.Kind = DeathSentence::How::kShot;
    else if (cHit->Flags & DeathCause::kBashFlag)
        cause.Kind = DeathSentence::How::kBash;
    if (pSource && pSource->formID != cUnarmed && pSource->GetName())
        cause.With = pSource->GetName();
    spdlog::info("PlayerService: the last blow before going down: attacker {:X}, player {}, source {:X}, projectile {:X}, flags {}, {} ms before", cHit->AttackerFormId,
                 playerId, cHit->SourceFormId, cHit->ProjectileFormId, cHit->Flags, std::chrono::duration_cast<std::chrono::milliseconds>(m_downedAt - cHit->At).count());
    return cause;
}

void PlayerService::StartHeartbeat() noexcept
{
    m_heartbeatSince = std::chrono::steady_clock::now();
    if (m_heartbeatSound.SoundId != 0xFFFFFFFF)
        return;
    const bool cPlaying = GameSound::Play(cHeartbeatSound, nullptr, &m_heartbeatSound);
    spdlog::info("PlayerService: down: the heartbeat in both hands, and its sound {}", cPlaying ? "playing" : "NOT playing");
}

void PlayerService::StopHeartbeat() noexcept
{
    if (m_heartbeatSound.SoundId == 0xFFFFFFFF)
        return;
    const bool cStopped = GameSound::Stop(m_heartbeatSound);
    spdlog::info("PlayerService: the heartbeat sound {}", cStopped ? "stopped" : "would not stop");
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
        if (const char* pPlace = PlaceNameOf(PlayerCharacter::Get()))
            VRBanner::Show(pPlace, cPlaceColour, 84, 3.f);
        else
            VRBanner::Hide();

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
    const TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
    const TESObjectCELL* pCell = pPlayer->GetParentCellEx();
    const char* pPlace = PlaceNameOf(pPlayer);
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
