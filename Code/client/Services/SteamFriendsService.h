#pragma once

#ifdef SKYRIMVR

#include <Services/SteamJoin.h>

#include <atomic>
#include <memory>

struct World;
struct UpdateEvent;
struct TransportService;

/**
 * @brief Steam friends in the menu: where this player plays, shown to them, and where they play, with a Join.
 *
 * It uses the game's own steam_api64.dll, as the voice chat does: no launcher and no hub.
 * This game's rich presence holds a SteamJoin share: the server it is on, or, when it hosts, the networks its server is reached on.
 * Friends in Skyrim VR showing one, or the launcher's code, go to the menu every few seconds.
 * While sharing is on, the address the internet sees comes from a public STUN server, asked when hosting or when a friend hosts.
 */
struct SteamFriendsService
{
    SteamFriendsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~SteamFriendsService() noexcept;

    TP_NOCOPYMOVE(SteamFriendsService);

    // The menu's setting: Steam friends see where this player plays.
    void SetSharing(bool aSharing) noexcept;
    // Joins the server a friend shows, by their Steam id as text, with the password the menu asked for.
    void JoinFriend(const std::string& acFriendId, const std::string& acPassword) noexcept;

private:
    struct Steam;
    // The STUN question runs on its own thread, which may outlive this service at quit: it only touches this.
    struct PublicLookup
    {
        std::atomic<uint32_t> Address{0};
        std::atomic<bool> Busy{false};
    };

    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void Publish() noexcept;
    void SendFriends(std::chrono::steady_clock::time_point aNow) noexcept;
    void AskPublicAddress() noexcept;
    void SendToPage(const char* apEvent, const std::string& acText) const noexcept;

    World& m_world;
    TransportService& m_transport;
    std::unique_ptr<Steam> m_steam;
    bool m_triedSteam{false};
    bool m_sharing{false};
    std::string m_published;
    std::string m_sentFriends;
    size_t m_friendsLogged{0};
    std::vector<SteamJoin::Network> m_lan;
    std::vector<SteamJoin::Network> m_vlan;
    uint32_t m_public{0};
    std::shared_ptr<PublicLookup> m_lookup{std::make_shared<PublicLookup>()};
    std::chrono::steady_clock::time_point m_nextPublicAsk{};
    std::chrono::steady_clock::time_point m_nextLook{};
    std::chrono::steady_clock::time_point m_nextNetworks{};
    std::chrono::steady_clock::time_point m_nextRequest{};
    std::chrono::steady_clock::time_point m_resendFriends{};

    entt::scoped_connection m_updateConnection;
};

#endif
