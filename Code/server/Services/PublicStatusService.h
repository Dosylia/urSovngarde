#pragma once

#include <Events/PacketEvent.h>

#include <condition_variable>
#include <random>
#include <thread>

struct World;
struct UpdateEvent;
struct PlayerPlaceRequest;

/**
 * @brief Pushes this server's status (name, players and their places) to the website's public server page.
 * Only with LiveServices:bPublicStatus=true; sent from its own thread so a slow hub never holds up the game.
 */
struct PublicStatusService
{
    PublicStatusService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PublicStatusService() noexcept;

    TP_NOCOPYMOVE(PublicStatusService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerPlace(const PacketEvent<PlayerPlaceRequest>& acMessage) const noexcept;

private:
    [[nodiscard]] std::string BuildStatus() noexcept;
    [[nodiscard]] std::string BuildOfflineStatus() const noexcept;
    [[nodiscard]] const std::string& TokenOf(uint32_t aPlayerId) noexcept;
    void QueueStatus(std::string aBody) noexcept;
    void SendLoop() noexcept;
    bool PostStatus(const std::string& acBody, std::chrono::seconds aTimeout) noexcept;

    World& m_world;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerPlaceConnection;

    bool m_enabled{false};
    std::string m_key;
    std::string m_origin;
    std::string m_path;
    std::string m_startedAt;
    // The unchanging fields as last sent, for the shutdown push when GameServer::Get() is already null. Game thread only.
    std::string m_header;
    std::chrono::steady_clock::time_point m_lastPush{};
    std::chrono::steady_clock::time_point m_nextPush{};
    uint32_t m_lastPlayerCount{0};
    // A random token per connection, so the page can move a marker; the player id would reveal how many joined before.
    TiltedPhoques::Map<uint32_t, std::string> m_tokens;
    std::mt19937 m_random;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::optional<std::string> m_pending;
    bool m_stop{false};

    // Read and written only by the sending thread.
    bool m_confirmed{false};
    bool m_failing{false};
    std::chrono::steady_clock::time_point m_lastFailureLog{};
};
