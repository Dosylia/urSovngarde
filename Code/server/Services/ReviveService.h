#pragma once

#include <Events/PacketEvent.h>
#include <Messages/NotifyDownedState.h>

struct World;
struct Player;
struct UpdateEvent;
struct PlayerLeaveEvent;
struct PlayerDownedRequest;
struct ReviveRequest;
struct SafePointRequest;
struct PlayerRespawnRequest;

/**
 * @brief Keeps which players are down and until when, decides revives, and ends a down: not revived in time, or nobody left standing to revive them.
 * It also keeps each player's last safe point: a player who is not revived respawns at the party leader's.
 * The downed player's own game stands it up or respawns it; this service only tells it when, and where.
 */
struct ReviveService
{
    ReviveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ReviveService() noexcept = default;

    TP_NOCOPYMOVE(ReviveService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerDowned(const PacketEvent<PlayerDownedRequest>& acMessage) noexcept;
    void OnRevive(const PacketEvent<ReviveRequest>& acMessage) noexcept;
    void OnSafePoint(const PacketEvent<SafePointRequest>& acMessage) noexcept;
    void OnPlayerRespawn(const PacketEvent<PlayerRespawnRequest>& acMessage) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

private:
    struct Downed
    {
        std::chrono::steady_clock::time_point Until{};
        //! The party members told this player is down; one who joins later is told by CheckDowned.
        Vector<uint32_t> Told;
    };

    struct SafePoint
    {
        GameId CellId{};
        GameId WorldSpaceId{};
        Vector3_NetQuantize Position{};
    };

    bool IsDown(uint32_t aPlayerId) const noexcept;
    //! Ends the player's down here; returns who was told it was down, or nothing when it was not down.
    std::optional<Vector<uint32_t>> TakeDowned(uint32_t aPlayerId) noexcept;
    //! Tells the members who were told this player was down, and are no longer in its party, that it left (kLeft).
    //! Without that they would go on trying to revive it.
    void TellFormerMembers(Player* apPlayer, const Vector<uint32_t>& acTold) const noexcept;
    //! The player's party, or the player alone when not in one.
    Vector<Player*> PartyOf(Player* apPlayer) const noexcept;
    //! To the player and every member of their party.
    void SendToPartyOf(Player* apPlayer, const NotifyDownedState& acMessage) const noexcept;
    //! The player respawns now (kDied, kPartyFallen, or kRespawned when its own game stopped waiting), at the point ReviveRules::ChooseRespawnPoint picks; acWhy is for the log.
    void EndDown(Player* apPlayer, NotifyDownedState::State aChange, const char* acWhy) noexcept;
    //! Nobody in this player's party is left standing: every member respawns. False when someone still stands.
    bool EndFallenParty(Player* apPlayer) noexcept;
    //! Once a second: the time ran out, nobody is left standing (a member left), or a member joined who was not told yet.
    void CheckDowned() noexcept;

    World& m_world;
    //! Downed players by player id.
    TiltedPhoques::Map<uint32_t, Downed> m_downed;
    //! Each player's last safe point by player id (SafePointRequest).
    TiltedPhoques::Map<uint32_t, SafePoint> m_safePoints;
    std::chrono::steady_clock::time_point m_nextCheck{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_downedConnection;
    entt::scoped_connection m_reviveConnection;
    entt::scoped_connection m_safePointConnection;
    entt::scoped_connection m_respawnConnection;
    entt::scoped_connection m_leaveConnection;
};
