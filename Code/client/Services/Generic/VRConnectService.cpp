#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

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
#include <ScriptExtender.h>
#include <Utils.h>
#include <World.h>

namespace
{
constexpr auto kAutoConnectDelay = 5s;    // after a save is loaded, so the cell has settled, before the install checks and the connection or the F6 hint
constexpr auto kAttemptTimeout = 30s;     // an attempt that never reports back is abandoned
constexpr std::array<std::chrono::seconds, 5> kRetryDelays{5s, 10s, 20s, 30s, 60s};

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
    char localAppData[MAX_PATH] = "";
    GetEnvironmentVariableA("LOCALAPPDATA", localAppData, sizeof(localAppData));

    // The working directory is the game's, so under Mod Organizer this path shows the mods.
    ConnectFile::Contents contents;
    if (!ConnectFile::Load(std::string(localAppData) + "\\SkyrimTogetherVR\\connect.txt", "Data\\SKSE\\Plugins\\urSovngarde\\connect.txt", contents))
        return false;

    aConfig.Address = contents.Address.c_str();
    aConfig.Password = contents.Password.c_str();
    aConfig.AutoConnect = contents.AutoConnect;
    if (contents.FromMod)
        spdlog::info("VRConnectService: the server comes from a mod's connect file");
    return true;
}

VRConnectService::VRConnectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&VRConnectService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&VRConnectService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&VRConnectService::OnDisconnected>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&VRConnectService::OnConnectionError>(this);
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
        spdlog::error("VRConnectService: SKSE VR is not loaded");
        Utils::ShowHudMessage("urSovngarde: SKSE VR is not loaded, check the SKSE VR install");
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
        spdlog::warn("VRConnectService: no server in %LOCALAPPDATA%\\SkyrimTogetherVR\\connect.txt or Data\\SKSE\\Plugins\\urSovngarde\\connect.txt");
        Utils::ShowHudMessage("urSovngarde: no server set, join from the urSovngarde launcher");
        m_state = State::kIdle;
        return;
    }

    // A Steam friend's address stays off the HUD and out of the log: both can end up on a stream or in a crash report.
    if (m_chosen)
    {
        spdlog::info("VRConnectService: connecting to a Steam friend's server (attempt {})", m_failedAttempts + 1);
        Utils::ShowHudMessage("urSovngarde: connecting to your friend's server");
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
                Utils::ShowHudMessage("urSovngarde: no server set, join from the urSovngarde launcher or run setup-connect.bat in the mod's folder");
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
        // Another character loaded while connected: reconnect, or the others keep seeing the old one.
        // The server keeps the name and body it was given when connecting.
        // The name is compared rather than the race, which changes in a werewolf's fight.
        // Only once after each loading screen, when the cell has settled: a quest or a mod may rename the player in play.
        // A name that changes in play is not another character, and each reconnect sends every actor again.
        if (m_nameCheckDue && now - m_inGameSince >= kAutoConnectDelay)
        {
            m_nameCheckDue = false;
            const auto name = PlayerName();
            if (!name.empty() && !m_connectedName.empty() && name != m_connectedName)
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

#endif
