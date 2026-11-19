#include <TiltedOnlinePCH.h>

#include <Services/VRConnectService.h>
#include <Services/ConnectFile.h>
#include <Services/TransportService.h>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>

#include <Games/TES.h>
#include <Interface/UI.h>
#include <Forms/TESNPC.h>
#include <PlayerCharacter.h>
#include <Copies/PlayerLook.h>
#include <ScriptExtender.h>
#include <Utils.h>
#include <World.h>

namespace
{
constexpr auto kAutoConnectDelay = 5s;    // after a save is loaded, so the cell has settled, before the install checks and the connection or the F6 hint
constexpr auto kAttemptTimeout = 30s;     // an attempt that never reports back is abandoned
constexpr std::array<std::chrono::seconds, 5> kRetryDelays{5s, 10s, 20s, 30s, 60s};
#ifdef SKYRIMVR
constexpr const char* kScriptExtender = "SKSE VR";
constexpr const char* kNoServerHint = "urSovngarde: no server set, join from the urSovngarde launcher";
#else
constexpr const char* kScriptExtender = "SKSE";
// A flat player also picks a server in the menu.
constexpr const char* kNoServerHint = "urSovngarde: no server set, pick one in the menu (F2) or join from the urSovngarde launcher";
#endif

// The name the server knows this player by: TransportService sends it when connecting.
TiltedPhoques::String PlayerName() noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    return pNpc ? TiltedPhoques::String(pNpc->fullName.value.AsAscii()) : TiltedPhoques::String();
}
} // namespace

bool VRConnectService::LoadConfig(Config& aConfig) noexcept
{
    // The working directory is the game's, so under Mod Organizer this path shows the mods.
    ConnectFile::Contents contents;
    if (!ConnectFile::Load(ConnectFile::PlayerFolder() / "connect.txt", "Data\\SKSE\\Plugins\\urSovngarde\\connect.txt", contents))
        return false;

    aConfig.Address = contents.Address.c_str();
    aConfig.Password = contents.Password.c_str();
    aConfig.AutoConnect = contents.AutoConnect;
    if (contents.FromMod)
        spdlog::info("VRConnectService: the server comes from a mod's connect file");
    return true;
}

VRConnectService::VRConnectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRConnectService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRConnectService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRConnectService::OnDisconnected>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&VRConnectService::OnConnectionError>(this);
    EventDispatcherManager::Get()->loadGameEvent.RegisterSink(this);
}

BSTEventResult VRConnectService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    ++m_saveLoads;
    return BSTEventResult::kOk;
}

void VRConnectService::Toggle() noexcept
{
    if (m_state == State::kOnline || m_state == State::kConnecting)
    {
        Stop();
        return;
    }

    m_failedAttempts = 0;
    StartAttempt();
}

void VRConnectService::Stop() noexcept
{
    // A friend's server never reached is given up; one left on purpose stays F6's.
    if (m_state != State::kOnline)
    {
        m_chosen.reset();
        m_chosenNext.clear();
    }

    const bool cWasTrying = m_state != State::kIdle;
    // Set before closing: Close() reports the disconnection synchronously, and it must not schedule a retry.
    m_state = State::kIdle;
    m_transport.Close();
    if (!cWasTrying)
        return;

    spdlog::info("VRConnectService: disconnecting, asked for here (F6 or the menu)");
    Utils::ShowHudMessage("urSovngarde: disconnected (F6 to connect again)");
}

void VRConnectService::ConnectNow() noexcept
{
    spdlog::info("VRConnectService: joining the server the launcher just set, asked for in the menu");
    m_chosen.reset();
    m_chosenNext.clear();
    ReconnectNow();
}

void VRConnectService::ConnectTo(const std::vector<std::string>& acAddresses, const std::string& acPassword) noexcept
{
    if (acAddresses.empty())
        return;

    spdlog::info("VRConnectService: joining a Steam friend's server, asked for in the menu");
    m_chosen = Config{acAddresses.front().c_str(), acPassword.c_str(), false};
    m_chosenNext.assign(acAddresses.begin() + 1, acAddresses.end());
    m_chosenIsFriend = true;
    ReconnectNow();
}

void VRConnectService::ConnectToServer(const std::string& acAddress, const std::string& acPassword) noexcept
{
    spdlog::info("VRConnectService: joining the server chosen in the menu");
    m_chosen = Config{acAddress.c_str(), acPassword.c_str(), false};
    m_chosenNext.clear();
    m_chosenIsFriend = false;
    ReconnectNow();
}

void VRConnectService::ReconnectNow() noexcept
{
    // Waiting before closing: the disconnection then schedules nothing, and the next update connects.
    // Before a save is loaded, that update comes once the game is in.
    m_state = State::kWaiting;
    m_failedAttempts = 0;
    m_nextAttempt = std::chrono::steady_clock::now();
    m_transport.Close();
}

bool VRConnectService::CheckInstall() noexcept
{
    // Reports install problems in the headset once a save is loaded, before they show up as a refused connection.
    bool ok = true;

    if (!IsScriptExtenderLoaded())
    {
        spdlog::error("VRConnectService: {} is not loaded", kScriptExtender);
        Utils::ShowHudMessage(fmt::format("urSovngarde: {0} is not loaded, check the {0} install", kScriptExtender).c_str());
    }

    auto* pGrids = INISettingCollection::Get()->GetSetting("uGridsToLoad:General");
    if (pGrids && pGrids->data != 5)
    {
        spdlog::error("VRConnectService: uGridsToLoad is {}, the server requires 5", pGrids->data);
        Utils::ShowHudMessage("urSovngarde: set uGridsToLoad=5 in SkyrimPrefs.ini, the server refuses other values");
        ok = false;
    }

    return ok;
}

bool VRConnectService::IsInGame() noexcept
{
    // Connecting reads the player's cell, which only exists once a save is loaded.
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    return pPlayer && pPlayer->parentCell && pPlayer->GetNiNode();
}

void VRConnectService::StartAttempt() noexcept
{
    if (!IsInGame())
    {
        Utils::ShowHudMessage("urSovngarde: load a save before connecting");
        m_state = State::kIdle;
        return;
    }

    Config config;
    if (m_chosen)
        config = *m_chosen;
    else if (!LoadConfig(config))
    {
        // The folder by name only: its full path holds the Windows user name, and logs end up in crash reports.
        spdlog::warn("VRConnectService: no server in %LOCALAPPDATA%\\{}\\connect.txt or Data\\SKSE\\Plugins\\urSovngarde\\connect.txt", ConnectFile::kFolderName);
        Utils::ShowHudMessage(kNoServerHint);
        m_state = State::kIdle;
        return;
    }

    // A server picked in the menu stays off the HUD and out of the log: it can be a friend's home, and both can end up on a stream or in a crash report.
    if (m_chosen)
    {
        spdlog::info("VRConnectService: connecting to {} (attempt {})", m_chosenIsFriend ? "a Steam friend's server" : "the server chosen in the menu", m_failedAttempts + 1);
        Utils::ShowHudMessage(m_chosenIsFriend ? "urSovngarde: connecting to your friend's server" : "urSovngarde: connecting to the server you chose");
    }
    else
    {
        spdlog::info("VRConnectService: connecting to {} (attempt {})", config.Address.c_str(), m_failedAttempts + 1);
        Utils::ShowHudMessage(TiltedPhoques::String("urSovngarde: connecting to ") + config.Address);
    }

    // Any leftover connection (say a dashboard connect still trying) is closed while the state is still idle or waiting.
    // Its disconnection then schedules no retry; closed after the state change, it would read as this attempt failing.
    m_transport.Close();

    m_state = State::kConnecting;
    m_attemptStarted = std::chrono::steady_clock::now();
    m_transport.SetServerPassword(config.Password.c_str());
    m_transport.Connect(std::string(config.Address.c_str()));
}

void VRConnectService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto now = std::chrono::steady_clock::now();

    // A loading screen since the last update counts as a load.
    // Another character's save can load with the player in a cell and drawn at every update in between.
    // So seeing the player out of the game is not enough (live-load-other-character).
    const uint32_t cLoadingScreens = UI::LoadingScreensQueued();
    if (cLoadingScreens != m_loadingScreensSeen)
    {
        m_loadingScreensSeen = cLoadingScreens;
        m_inGameSince = {};
        m_nameCheckDue = true;
    }

    // A trip through the main menu counts as a save load: a New Game started there raises no load event, and is another character all the same.
    static const BSFixedString s_mainMenu("Main Menu");
    const UI* pUI = UI::Get();
    const bool cAtMainMenu = pUI && pUI->GetMenuOpen(s_mainMenu);
    if (cAtMainMenu && !m_atMainMenu)
        ++m_saveLoads;
    m_atMainMenu = cAtMainMenu;

    // Character creation, or a race menu later, changes the look and often the name with no loading screen: both go at once.
    static const BSFixedString s_raceMenu("RaceSex Menu");
    const bool cInRaceMenu = pUI && pUI->GetMenuOpen(s_raceMenu);
    if (m_inRaceMenu && !cInRaceMenu && m_state == State::kOnline)
    {
        // After a save load the name check has not seen, that check decides: a New Game's race menu belongs to another character.
        if (m_saveLoads != m_saveLoadsChecked)
            m_nameCheckDue = true;
        else
        {
            spdlog::info("VRConnectService: the race menu closed; the character's look and name '{}' sent, still connected", PlayerName().c_str());
            m_lookPending = true;
        }
    }
    m_inRaceMenu = cInRaceMenu;

    // Tried again each update until it goes: right after connecting the player has no server character yet.
    if (m_lookPending && m_state == State::kOnline && PlayerLook::SendRenamed(m_world, m_transport))
    {
        m_lookPending = false;
        if (const auto cName = PlayerName(); !cName.empty())
            m_connectedName = cName;
    }

    if (!IsInGame())
    {
        m_inGameSince = {};
        m_nameCheckDue = true;
        return;
    }
    if (m_inGameSince == std::chrono::steady_clock::time_point{})
        m_inGameSince = now;

    switch (m_state)
    {
    case State::kIdle:
        // Once per session, a few seconds after the first load, the install checks run.
        // Then the game connects by itself only when the launcher's setting says so; otherwise it waits for F6.
        if (m_autoConnectDone || now - m_inGameSince < kAutoConnectDelay)
            return;

        m_autoConnectDone = true;
        if (!CheckInstall())
            return;
        {
            Config config;
            if (!LoadConfig(config))
            {
#ifdef SKYRIMVR
                Utils::ShowHudMessage("urSovngarde: no server set, join from the urSovngarde launcher or run setup-connect.bat in the mod's folder");
#endif
                return;
            }
            if (!config.AutoConnect)
            {
                spdlog::info("VRConnectService: ready to connect to {}; waiting for F6", config.Address.c_str());
                Utils::ShowHudMessage(TiltedPhoques::String("urSovngarde: press F6 to connect to ") + config.Address);
                return;
            }
        }
        StartAttempt();
        break;

    case State::kConnecting:
        if (now - m_attemptStarted > kAttemptTimeout)
        {
            spdlog::warn("VRConnectService: connection attempt timed out");
            m_transport.Close(); // reports a disconnection, which schedules the retry
            if (m_state == State::kConnecting)
                OnDisconnected({});
        }
        break;

    case State::kWaiting:
        if (now >= m_nextAttempt)
            StartAttempt();
        break;

    case State::kOnline:
        // Checked once after each loading screen, when the cell has settled.
        // A new name after a save load is another character: reconnect, as the server keeps the name and body it was given when connecting.
        // A new name with no save loaded is the same character renamed in play (character creation, a quest, a mod): its look and name are sent instead.
        // A reconnect hands away every NPC this game runs, mid-scene if need be, and sends every actor again.
        // The name is compared rather than the race, which changes in a werewolf's fight.
        if (m_nameCheckDue && now - m_inGameSince >= kAutoConnectDelay)
        {
            m_nameCheckDue = false;
            const auto name = PlayerName();
            const bool cSaveLoaded = m_saveLoads != m_saveLoadsChecked;
            m_saveLoadsChecked = m_saveLoads;
            if (!name.empty() && !m_connectedName.empty() && name != m_connectedName && !cSaveLoaded)
            {
                // Renamed in play (character creation, a quest, a mod): the same character, who keeps their id, party and NPCs.
                spdlog::info("VRConnectService: the character was renamed '{}' in play, was '{}'; its name and look sent, still connected", name.c_str(),
                             m_connectedName.c_str());
                m_lookPending = true;
            }
            else if (!name.empty() && !m_connectedName.empty() && name != m_connectedName)
            {
                spdlog::info("VRConnectService: the character is now '{}', was '{}' when connecting; connecting again", name.c_str(), m_connectedName.c_str());
                Utils::ShowHudMessage("urSovngarde: another character was loaded, connecting again");
                // Waiting before closing: the disconnection then schedules nothing, and the next update connects.
                m_state = State::kWaiting;
                m_failedAttempts = 0;
                m_nextAttempt = now;
                m_transport.Close();
            }
        }
        break;
    }
}

void VRConnectService::OnConnected(const ConnectedEvent&) noexcept
{
    // Connected somewhere else from the menu: a drop from there must not come back to the friend's server.
    if (m_chosen && m_transport.GetServerEndpoint() != m_chosen->Address.c_str())
        m_chosen.reset();
    m_state = State::kOnline;
    m_failedAttempts = 0;
    m_autoConnectDone = true;
    m_connectedName = PlayerName();
    m_nameCheckDue = false;
    m_loadingScreensSeen = UI::LoadingScreensQueued();
    m_saveLoadsChecked = m_saveLoads;
    m_lookPending = false;
}

void VRConnectService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // Idle: disconnected on purpose. Waiting: a retry is already scheduled (a cancelled attempt reports late).
    if (m_state == State::kIdle || m_state == State::kWaiting)
        return;

    const bool wasOnline = m_state == State::kOnline;

    // A friend's server that did not answer at one address is tried at the next one at once.
    if (!wasOnline && m_chosen && !m_chosenNext.empty())
    {
        m_chosen->Address = m_chosenNext.front().c_str();
        m_chosenNext.erase(m_chosenNext.begin());
        m_state = State::kWaiting;
        m_nextAttempt = std::chrono::steady_clock::now();
        spdlog::info("VRConnectService: no answer from the friend's server there; trying its next address");
        return;
    }

    const auto delay = kRetryDelays[std::min<size_t>(m_failedAttempts, kRetryDelays.size() - 1)];
    ++m_failedAttempts;

    m_state = State::kWaiting;
    m_nextAttempt = std::chrono::steady_clock::now() + delay;

    const auto message = fmt::format("urSovngarde: {}, trying again in {} s", wasOnline ? "connection lost" : "server not reachable", delay.count());
    spdlog::info("VRConnectService: {}", message);
    Utils::ShowHudMessage(message.c_str());
}

void VRConnectService::OnConnectionError(const ConnectionErrorEvent&) noexcept
{
    // A refusal arrives while connecting, before the connected event; OverlayService shows the reason.
    // The same request would be refused again, so stop until the player presses F6.
    // Errors raised once online (the uGridsToLoad check) don't end the session and must not turn reconnection off.
    if (m_state != State::kConnecting)
        return;

    spdlog::info("VRConnectService: connection refused by the server, not retrying");
    m_state = State::kIdle;
    m_chosen.reset();
    m_chosenNext.clear();
}
