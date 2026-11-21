#pragma once

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
 * Friends in the same game (Skyrim VR, or flat Skyrim) showing one, or the launcher's code, go to the menu every few seconds.
 * Steam shares rich presence only within one game, so a flat and a VR player join each other through the launcher's codes.
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

    /**
     * Show Steam friends where this player plays, in this game's rich presence, and log each change.
     * A server reached through a Steam friend's share is not passed on, and one line says why when that starts.
     * Main thread, every few seconds.
     */
    void Publish() noexcept;

    /**
     * Decide what this game's share shows now.
     *
     * @param acEndpoint The server this game is on, as the transport reached it.
     * @param aPort Set to the server's port when its address reads.
     * @return Nothing while sharing is off or this game is offline; otherwise what SteamJoin::ChooseShareContent decides for that server.
     */
    SteamJoin::ShareContent CurrentShareContent(const std::string& acEndpoint, uint16_t& aPort) const noexcept;

    /**
     * Build the share that shows what was decided, with this PC's public address and the server's password setting.
     *
     * @param aContent What the share shows.
     * @param acEndpoint The server this game is on, as the transport reached it: shown when the share holds that server.
     * @param aPort This player's own server's port: shown when the share holds that server.
     * @return The share; not valid when it shows nothing.
     */
    SteamJoin::Share BuildShare(SteamJoin::ShareContent aContent, const std::string& acEndpoint, uint16_t aPort) const noexcept;

    /**
     * Log what Steam friends see since the rich presence last changed.
     *
     * @param acValue The rich presence value just set, empty when friends see nothing.
     * @param aContent What that value shows.
     * @param acShare The share the value was written from.
     */
    static void LogPublished(const std::string& acValue, SteamJoin::ShareContent aContent, const SteamJoin::Share& acShare) noexcept;

    void SendFriends(std::chrono::steady_clock::time_point aNow) noexcept;
    void AskPublicAddress() noexcept;
    void SendToPage(const char* apEvent, const std::string& acText) const noexcept;

    World& m_world;
    TransportService& m_transport;
    std::unique_ptr<Steam> m_steam;
    bool m_triedSteam{false};
    bool m_sharing{false};
    std::string m_published;
    // What the last look decided the share shows, so the line about a withheld friend's server is written once.
    SteamJoin::ShareContent m_shareContent{SteamJoin::ShareContent::Nothing};
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
