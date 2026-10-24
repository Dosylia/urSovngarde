#pragma once

#ifdef SKYRIMVR

struct World;
struct TransportService;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ConnectionErrorEvent;

/**
 * @brief Connects VR players without a keyboard or a menu.
 *
 * The server comes from %LOCALAPPDATA%\SkyrimTogetherVR\connect.txt: "address:port", then an optional password line.
 * A few seconds after a save is loaded the client connects on its own, and a dropped connection is retried with a growing delay.
 * F6 still toggles the connection by hand, and disconnecting with it stops the retries.
 * A refused connection (wrong version, wrong password, ...) stops them too, because trying again would give the same answer.
 */
struct VRConnectService
{
    struct Config
    {
        TiltedPhoques::String Address;
        TiltedPhoques::String Password;
        // The launcher's "connect automatically" setting: an "autoconnect=on" line after the address.
        bool AutoConnect = false;
    };

    // False when connect.txt is missing or its first line is empty.
    static bool LoadConfig(Config& aConfig) noexcept;

    VRConnectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRConnectService() noexcept = default;

    TP_NOCOPYMOVE(VRConnectService);

    // F6: connect when offline, otherwise disconnect and stop reconnecting.
    void Toggle() noexcept;

private:
    enum class State
    {
        kIdle,       // not trying to connect
        kConnecting, // an attempt is in flight, waiting for its connected or disconnected event
        kOnline,
        kWaiting, // waiting before the next attempt
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acEvent) noexcept;

    void StartAttempt() noexcept;
    // Reports install problems on the HUD. False when the server would refuse the connection (uGridsToLoad).
    static bool CheckInstall() noexcept;
    static bool IsInGame() noexcept;

    TransportService& m_transport;

    State m_state{State::kIdle};
    bool m_autoConnectDone{false};
    uint32_t m_failedAttempts{0};
    std::chrono::steady_clock::time_point m_inGameSince{};
    std::chrono::steady_clock::time_point m_nextAttempt{};
    std::chrono::steady_clock::time_point m_attemptStarted{};
    // The character's name when the connection was made; another one loaded since means connecting again.
    TiltedPhoques::String m_connectedName;
    // A loading screen was seen since the last name check: the name is compared once the game has settled after it.
    bool m_nameCheckDue{false};
    // Loading screens the game had queued by the last update (UI::LoadingScreensQueued).
    uint32_t m_loadingScreensSeen{0};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
};

#endif
