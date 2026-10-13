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

    ToggleDeathSystem(false);

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

    static bool s_startTimer = false;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    // Once the screen is fading to black, finish the respawn even if bleedout ends early, as it does on VR.
    if (!pPlayer->actorState.IsBleedingOut() && !s_startTimer)
    {
        m_cachedMainSpellId = pPlayer->magicItems[0] ? pPlayer->magicItems[0]->formID : 0;
        m_cachedSecondarySpellId = pPlayer->magicItems[1] ? pPlayer->magicItems[1]->formID : 0;
        m_cachedPowerId = pPlayer->equippedShout ? pPlayer->equippedShout->formID : 0;

        return;
    }

    if (!s_startTimer)
    {
        spdlog::info("PlayerService: player is down, respawning in 5 seconds");

        s_startTimer = true;
        m_respawnDeadline = std::chrono::steady_clock::now() + 5s;
        FadeOutGame(true, true, 3.0f, true, 2.0f);

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        if (pPlayer->GetActorValue(ActorValueInfo::kHealth) > 0.f)
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 0);

        pPlayer->PayCrimeGoldToAllFactions();
    }

    const auto cNow = std::chrono::steady_clock::now();
    if (cNow >= m_respawnDeadline)
    {
        spdlog::info("PlayerService: respawning player");
        pPlayer->RespawnPlayer();

        m_knockdownDeadline = std::chrono::steady_clock::now() + 1500ms;
        m_knockdownStart = true;

        m_transport.Send(PlayerRespawnRequest());

        s_startTimer = false;

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
        if (pPlayer->currentProcess)
            pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);

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
