#pragma once

#include <Structs/GameId.h>

struct World;
struct TransportService;

struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ServerSettings;
struct GridCellChangeEvent;
struct CellChangeEvent;
struct PlayerDialogueEvent;
struct PlayerLevelEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;

struct NotifyPlayerRespawn;
struct NotifyDownedState;

/**
 * @brief Handles logic related to the local player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnServerSettingsReceived(const ServerSettings& acSettings) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept;
    void OnNotifyDownedState(const NotifyDownedState& acMessage) noexcept;
    void OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept;
    void OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept;
    void OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;

private:
    //! Where the game would have saved last; the server keeps the last one each player sends (SafePointRequest).
    struct SafePoint
    {
        bool Known{};
        GameId CellId{};
        GameId WorldSpaceId{};
        glm::vec3 Position{};
    };

    /**
     * @brief Takes the player from down to revived or respawned: waits for a revive, then fades and respawns.
     */
    void RunRespawnUpdates() noexcept;
    void RunPostDeathUpdates() noexcept;
    /**
     * @brief A safe point once a loading screen (a load door, fast travel, a loaded save) or a sleep or wait has ended, as the game's autosaves do.
     * Also notes every loading screen, which tells a save loaded while down from a bleedout that ended by itself.
     */
    void RunSafePointUpdates() noexcept;
    //! Here, unless the player is in combat or down; sent to the server.
    void RecordSafePoint() noexcept;
    //! Fades out and respawns the player once the screen is black, at that point when known.
    void StartRespawn(const SafePoint& acWhere) noexcept;
    //! A party member revived the player: back on its feet where it lies, with part of its health.
    void StandUp(uint32_t aReviverId) noexcept;
    /**
     * @brief Make sure difficulty doesn't get changed while connected
     */
    void RunDifficultyUpdates() const noexcept;
    void RunLevelUpdates() const noexcept;
    void RunBeastFormDetection() const noexcept;
    void RunPlaceUpdates() noexcept;
    void RunCrashPlaceNote() noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    enum class DownState
    {
        kUp,
        kDown,       // in bleedout, waiting for a revive until m_respawnDeadline
        kRespawning, // fading out, respawning at m_respawnAt
    };
    DownState m_downState = DownState::kUp;
    std::chrono::steady_clock::time_point m_downedAt{};
    std::chrono::steady_clock::time_point m_respawnDeadline{};
    std::chrono::steady_clock::time_point m_respawnAt{};
    SafePoint m_respawnPoint{};
    SafePoint m_ownSafePoint{};
    // For RunSafePointUpdates: the loading screens counted and the rest menu as last seen, when to record the next safe point, and whether a loading screen showed since going down or starting the respawn.
    uint32_t m_loadingScreens = 0;
    bool m_restMenuOpen = false;
    bool m_loadingSeen = false;
    std::chrono::steady_clock::time_point m_nextMenuCheck{};
    std::chrono::steady_clock::time_point m_safePointAt{};
    // After a revive, bleedout recovery is blocked again once the player has stood up (or the wait gives up).
    bool m_standingUp = false;
    float m_revivedHealth = 0.f;
    std::chrono::steady_clock::time_point m_standUpDeadline{};
    int32_t m_serverDifficulty = 6;
    int32_t m_previousDifficulty = 6;

    bool m_isDeathSystemEnabled = true;

    bool m_knockdownStart = false;
    std::chrono::steady_clock::time_point m_knockdownDeadline{};

    bool m_godmodeStart = false;
    std::chrono::steady_clock::time_point m_godmodeDeadline{};

    uint32_t m_cachedMainSpellId = 0;
    uint32_t m_cachedSecondarySpellId = 0;
    uint32_t m_cachedPowerId = 0;

    // The last place sent for the public server page, and when to check again.
    String m_lastPlace;
    std::chrono::steady_clock::time_point m_nextPlaceCheck{};

    // For RunCrashPlaceNote: when to note again, and where the player last stood outside.
    std::chrono::steady_clock::time_point m_nextCrashPlaceNote{};
    String m_outsideWorldSpace; // its editor id (hex form id when it has none); empty before the first exterior
    float m_outsideX = 0.f;
    float m_outsideY = 0.f;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_downedStateConnection;
    entt::scoped_connection m_gridCellChangeConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};
