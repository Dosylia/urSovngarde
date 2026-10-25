#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct PartyLeftEvent;
struct NotifyDownedState;
struct Actor;

/**
 * @brief The party members who are down, and reviving one: a hand held on a downed player's body asks the server to revive them.
 * This player's own down state is PlayerService's.
 */
struct ReviveService
{
    ReviveService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~ReviveService() noexcept = default;

    TP_NOCOPYMOVE(ReviveService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPartyLeft(const PartyLeftEvent& acEvent) noexcept;
    void OnNotifyDownedState(const NotifyDownedState& acMessage) noexcept;

private:
    //! That player's character in this game, or null when it is not here.
    Actor* FindCopy(uint32_t aPlayerId) const noexcept;
    //! Which of this player's hands touches that downed copy (0 left, 1 right), or -1; on flat Skyrim, standing next to it counts as the right hand.
    //! aHeld: a hold on this copy has started, so the hand may stray a little further before it counts as taken off.
    //! aDistance is how far the nearest hand (or the player) is from the copy's body, for the log.
    int TouchingHand(Actor* apCopy, bool aHeld, float& aDistance) const noexcept;
    String NameOf(uint32_t aPlayerId) const noexcept;
    void StopHolding() noexcept;
    //! TEMPORARY: how the copies of party members revived 5 s ago stand here, for a report of a copy still down after a revive.
    void LogRevivedCopies(std::chrono::steady_clock::time_point aNow) noexcept;

    World& m_world;
    TransportService& m_transport;

    //! Downed party members, by player id.
    Vector<uint32_t> m_downed;
    //! The downed player a hand is held on, 0 for none, since when, and whether the revive was asked for.
    uint32_t m_holdTarget{0};
    float m_holdDistance{0.f}; // how far the hand was from the body at the last check
    std::chrono::steady_clock::time_point m_holdSince{};
    bool m_asked{false};
    std::chrono::steady_clock::time_point m_nextCheck{};
    //! TEMPORARY: revived party members whose copy is looked at once the time comes (LogRevivedCopies).
    Vector<std::pair<uint32_t, std::chrono::steady_clock::time_point>> m_revivedChecks;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_downedStateConnection;
};
