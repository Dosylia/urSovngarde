#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Where a player plays, as their game shows it to Steam friends, and where a friend's game reaches it from.
// Plain values in and out, so the rules have unit tests (Code/tests/steamjoin.cpp).
namespace SteamJoin
{
// An IPv4 network: an address and its prefix length, in host order.
struct Network
{
    uint32_t Ip{0};
    uint8_t Prefix{32};
};

// What one game shows: the server it is on, or, when it hosts, the networks its server can be reached on.
struct Share
{
    bool Valid{false};
    // Another player's server this one is on, as "host:port".
    std::string Server;
    // This player's own server, when hosting.
    uint16_t Port{0};
    std::vector<Network> Lan;  // adapters with a gateway: the home network
    std::vector<Network> Vlan; // a virtual LAN: Radmin VPN, ZeroTier, Tailscale, Hamachi
    uint32_t Public{0};        // the address the internet sees, 0 when unknown
    bool Password{false};
};

// How a friend's server is reached.
enum class Route
{
    Server,  // the server the friend is on, where it is
    Lan,     // the friend's home network
    Vlan,    // a virtual LAN both are on
    Public,  // the friend's public address: only with their port open
};

struct Choice
{
    std::string Address;
    Route Kind;
};

/**
 * Where the server this game is on runs, as this PC sees its address.
 */
enum class ServerPlace
{
    Unreadable,  // an IPv6 address, or a port that does not read
    OwnServer,   // this PC, with its own server listening on that port
    LocalTunnel, // this PC with no server of its own on that port: the launcher's tunnel for a friend's code
    Elsewhere,   // another PC
};

/**
 * How this game came to the server it is on.
 */
enum class ServerSource
{
    Picked,      // connect.txt, the launcher, or the menu's connect form or server list
    FriendShare, // a Steam friend's share, joined from the menu's list of Steam friends
};

/**
 * What this game's share shows Steam friends about the server it is on.
 */
enum class ShareContent
{
    Nothing,
    OwnServer,             // this player's own server: its port and the networks it is reached on
    ServerItIsOn,          // the server this player is on, at the address it was reached by
    WithheldFriendsServer, // nothing: the address came from a friend's share, and it is that friend's to give
};

/**
 * Decide what this game's share shows Steam friends about the server it is on.
 * Another PC's server reached through a friend's share is never shown.
 *
 * @param aPlace Where the server runs, as this PC sees its address.
 * @param aSource How this game came to the server.
 * @return OwnServer for this player's own server, ServerItIsOn for another PC's server, WithheldFriendsServer when that one came from a friend's share, else Nothing.
 */
ShareContent ChooseShareContent(ServerPlace aPlace, ServerSource aSource);

// The Steam rich presence value, at most 255 characters; empty when it would not fit.
std::string Write(const Share& acShare);
// A friend's value, as Steam gives it: anything malformed reads as not valid.
Share Read(std::string_view aText);
// Where this PC may reach the friend's server, best first, at most three: the next is tried when one does not answer.
// Empty when it cannot be reached from here.
std::vector<Choice> Choose(const Share& acFriend, const std::vector<Network>& acOwnLan, const std::vector<Network>& acOwnVlan, uint32_t aOwnPublic);
// An address only a home network or a virtual LAN reaches: private, shared (CGNAT), Hamachi's or Radmin's.
bool IsPrivate(uint32_t aIp);

// A STUN binding request (RFC 5389) with this transaction id, and the address its answer names (0 when it names none).
void StunRequest(const uint8_t (&acTransaction)[12], uint8_t (&aRequest)[20]);
uint32_t StunAnswer(const uint8_t* apAnswer, size_t aSize, const uint8_t (&acTransaction)[12]);

std::string ToText(uint32_t aIp);
bool ParseIp(std::string_view aText, uint32_t& aIp);
} // namespace SteamJoin
