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
