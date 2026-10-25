#pragma once

struct World;
struct ImguiService;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyPlayerList;
struct NotifyPartyInfo;
struct NotifyPartyInvite;
struct NotifyPartyJoined;
struct NotifyPartyLeft;

/**
 * @brief Manages the party of the local player.
 */
struct PartyService
{
    PartyService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransportService) noexcept;
    ~PartyService() = default;

    TP_NOCOPYMOVE(PartyService);

    [[nodiscard]] bool IsInParty() const noexcept { return m_inParty; }
    [[nodiscard]] bool IsLeader() const noexcept { return m_isLeader; }
    [[nodiscard]] uint32_t GetLeaderPlayerId() const noexcept { return m_leaderPlayerId; }

    const Vector<uint32_t>& GetPartyMembers() const noexcept { return m_partyMembers; }
    //! The party's players within 50 m of this one, this one included: what enemies are scaled to (GameplayFormulas::PartyEnemyHealth).
    [[nodiscard]] uint32_t GetPlayersNearby() const noexcept { return m_playersNearby; }
    const Map<uint32_t, String>& GetPlayers() const noexcept { return m_players; }
    Map<uint32_t, uint64_t>& GetInvitations() noexcept { return m_invitations; }

    void CreateParty() const noexcept;
    void LeaveParty() const noexcept;
    void CreateInvite(const uint32_t aPlayerId) const noexcept;
    void AcceptInvite(const uint32_t aInviterId) const noexcept;
    void KickPartyMember(const uint32_t aPlayerId) const noexcept;
    void ChangePartyLeader(const uint32_t aPlayerId) const noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPlayerList(const NotifyPlayerList& acPlayerList) noexcept;
    void OnPartyInfo(const NotifyPartyInfo& acPartyInfo) noexcept;
    void OnPartyInvite(const NotifyPartyInvite& acPartyInvite) noexcept;
    void OnPartyJoined(const NotifyPartyJoined& acPartyJoined) noexcept;
    void OnPartyLeft(const NotifyPartyLeft& acPartyLeft) noexcept;

private:
    void DestroyParty() noexcept;
    void CountPlayersNearby() noexcept;
    void SetPlayersNearby(uint32_t aPlayers) noexcept;

    Map<uint32_t, String> m_players;
    Map<uint32_t, uint64_t> m_invitations;
    uint64_t m_nextUpdate{0};

    bool m_inParty = false;
    bool m_isLeader = false;
    uint32_t m_leaderPlayerId;
    Vector<uint32_t> m_partyMembers;
    uint32_t m_playersNearby = 1;

    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_partyInfoConnection;
    entt::scoped_connection m_partyInviteConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};
