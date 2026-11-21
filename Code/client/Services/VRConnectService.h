#pragma once

#include <optional>

#include <Events/EventDispatcher.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ConnectionErrorEvent;

/**
 * @brief Connects players without a keyboard or a menu: in VR, and on flat from the launcher.
 *
 * The server comes from connect.txt in the player's folder (ConnectFile::PlayerFolder): "address:port", then an optional password line.
 * When that file names no server, it comes from the same file in a mod: Data\SKSE\Plugins\urSovngarde\connect.txt.
 * A few seconds after a save is loaded the client connects on its own if the file says "autoconnect=on", and otherwise waits for F6.
 * A dropped connection is retried with a growing delay.
 * F6 toggles the connection by hand, and disconnecting with it stops the retries.
 * A refused connection (wrong version, wrong password, ...) stops them too, because trying again would give the same answer.
 */
struct VRConnectService final : BSTEventSink<TESLoadGameEvent>
{
    struct Config
    {
        TiltedPhoques::String Address;
        TiltedPhoques::String Password;
        // The launcher's "connect automatically" setting: an "autoconnect=on" line after the address.
        bool AutoConnect = false;
    };

    // False when neither the player's file nor a mod's names a server.
    static bool LoadConfig(Config& aConfig) noexcept;

    VRConnectService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~VRConnectService() noexcept = default;

    TP_NOCOPYMOVE(VRConnectService);

    // F6: connect when offline, otherwise disconnect and stop reconnecting.
    void Toggle() noexcept;
    // The launcher just put a friend's server in connect.txt: connect to it now, leaving any server or attempt.
    void ConnectNow() noexcept;
    // A Steam friend's server, picked in the menu: connect to it now, and after a drop, until connect.txt is chosen again.
    // Its addresses come best first; one that does not answer gives way to the next.
    // connect.txt stays as it was, so the next game starts with the player's own server.
    void ConnectTo(const std::vector<std::string>& acAddresses, const std::string& acPassword) noexcept;
    // A server picked in the menu's own connect form or server list, the same way: connect.txt stays as it was.
    void ConnectToServer(const std::string& acAddress, const std::string& acPassword) noexcept;
    // The menu's Disconnect or Cancel: closes the connection or the attempt, and stops the retries.
    void Stop() noexcept;

    /**
     * Check if the server this game connects to, or is on, came from a Steam friend's share.
     * Such an address is the friend's to give, so it is not passed on to this player's own Steam friends.
     *
     * @return True while the menu's pick of a Steam friend's server is in use; false for connect.txt, the launcher and the menu's own server list.
     */
    [[nodiscard]] bool IsServerFromFriendsShare() const noexcept;

    BSTEventResult OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) override;

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
    // Leaves any server or attempt; the next update connects.
    void ReconnectNow() noexcept;
    // Reports install problems on the HUD. False when the server would refuse the connection (uGridsToLoad).
    static bool CheckInstall() noexcept;
    static bool IsInGame() noexcept;

    World& m_world;
    TransportService& m_transport;

    State m_state{State::kIdle};
    // A server picked in the menu (a Steam friend's, or one from its connect form), used instead of connect.txt while set.
    // A friend's other addresses still to try.
    std::optional<Config> m_chosen;
    std::vector<std::string> m_chosenNext;
    bool m_chosenIsFriend{false};
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
    // Saves loaded since the game started, and as of the last name check: a name changed with no load between is a rename in play.
    std::atomic<uint32_t> m_saveLoads{0};
    uint32_t m_saveLoadsChecked{0};
    bool m_atMainMenu{false};
    bool m_inRaceMenu{false};
    // The player's look and name, to send to the server as soon as it can take them.
    bool m_lookPending{false};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
};
