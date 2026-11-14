#pragma once

struct World;
struct TransportService;
struct UpdateEvent;

/**
 * @brief Tells the server every 2 s which characters this game has loaded, and as what: its own, a copy or a stand-in.
 * The server hands characters over by these reports, and passes the fight of each one this game runs to the next owner.
 */
struct PresenceService
{
    PresenceService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

private:
    World& m_world;
    TransportService& m_transport;
    std::chrono::steady_clock::time_point m_nextReport{};

    entt::scoped_connection m_updateConnection;
};
