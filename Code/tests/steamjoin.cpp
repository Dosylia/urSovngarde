#include <catch2/catch.hpp>

#include <Services/SteamJoin.h>

#include <algorithm>
#include <iterator>
#include <string>

using namespace SteamJoin;

namespace
{
Network Net(const char* apIp, const uint8_t aPrefix)
{
    Network network;
    REQUIRE(ParseIp(apIp, network.Ip));
    network.Prefix = aPrefix;
    return network;
}

uint32_t Ip(const char* apIp)
{
    uint32_t ip = 0;
    REQUIRE(ParseIp(apIp, ip));
    return ip;
}

Share Hosting()
{
    Share share;
    share.Valid = true;
    share.Port = 10578;
    share.Lan = {Net("192.168.1.10", 24)};
    share.Vlan = {Net("26.12.34.56", 8)};
    share.Public = Ip("81.2.3.4");
    return share;
}

Share On(const char* apServer, const char* apPublic = nullptr)
{
    Share share;
    share.Valid = true;
    share.Server = apServer;
    share.Public = apPublic ? Ip(apPublic) : 0;
    return share;
}

// The addresses in the order they are tried.
std::string Order(const std::vector<Choice>& acChoices)
{
    std::string text;
    for (const auto& choice : acChoices)
        text += (text.empty() ? "" : " ") + choice.Address;
    return text;
}
} // namespace

TEST_CASE("A shared server survives Steam's text", "[steamjoin]")
{
    Share hosting = Hosting();
    hosting.Password = true;
    const Share read = Read(Write(hosting));
    REQUIRE(read.Valid);
    REQUIRE(read.Port == 10578);
    REQUIRE(read.Password);
    REQUIRE(read.Lan.size() == 1);
    REQUIRE(read.Lan[0].Ip == Ip("192.168.1.10"));
    REQUIRE(read.Lan[0].Prefix == 24);
    REQUIRE(read.Vlan.size() == 1);
    REQUIRE(read.Public == Ip("81.2.3.4"));

    const Share elsewhere = Read(Write(On("play.example.org:10578", "90.9.9.9")));
    REQUIRE(elsewhere.Server == "play.example.org:10578");
    REQUIRE(elsewhere.Public == Ip("90.9.9.9"));
}

TEST_CASE("A friend's malformed value is not valid", "[steamjoin]")
{
    REQUIRE_FALSE(Read("").Valid);
    REQUIRE_FALSE(Read("v1").Valid);
    REQUIRE_FALSE(Read("v2 port=10578").Valid);
    REQUIRE_FALSE(Read("v1 port=0").Valid);
    REQUIRE_FALSE(Read("v1 port=99999").Valid);
    REQUIRE_FALSE(Read("v1 port=10578 lan=192.168.1.300/24").Valid);
    REQUIRE_FALSE(Read("v1 port=10578 lan=192.168.1.1/33").Valid);
    REQUIRE_FALSE(Read("v1 server=evil;rm:1").Valid);
    REQUIRE_FALSE(Read("v1  port=10578").Valid);
    REQUIRE_FALSE(Read("v1 port=10578 lan=1.1.1.1/8,2.2.2.2/8,3.3.3.3/8,4.4.4.4/8,5.5.5.5/8").Valid);
    REQUIRE_FALSE(Read(std::string(300, 'x')).Valid);
    // A key from a newer version does not spoil the rest.
    REQUIRE(Read("v1 port=10578 later=yes").Valid);
}

TEST_CASE("Two players in one house meet on the home network", "[steamjoin]")
{
    const auto cChoices = Choose(Hosting(), {Net("192.168.1.20", 24)}, {}, Ip("81.2.3.4"));
    REQUIRE(Order(cChoices) == "192.168.1.10:10578");
    REQUIRE(cChoices[0].Kind == Route::Lan);
}

TEST_CASE("Two houses with the same home network use the public address", "[steamjoin]")
{
    const auto cChoices = Choose(Hosting(), {Net("192.168.1.20", 24)}, {}, Ip("90.9.9.9"));
    REQUIRE(Order(cChoices) == "81.2.3.4:10578");
    REQUIRE(cChoices[0].Kind == Route::Public);
}

TEST_CASE("A virtual LAN both seem to be on comes first, the public address next", "[steamjoin]")
{
    // Radmin gives everyone 26/8, so the friend may be in another Radmin network: the public address follows.
    REQUIRE(Order(Choose(Hosting(), {Net("192.168.0.5", 24)}, {Net("26.99.1.2", 8)}, Ip("90.9.9.9"))) == "26.12.34.56:10578 81.2.3.4:10578");

    // Tailscale: a /32 on each side, one tailnet.
    Share tailscale = Hosting();
    tailscale.Vlan = {Net("100.101.102.103", 32)};
    REQUIRE(Order(Choose(tailscale, {}, {Net("100.64.7.8", 32)}, Ip("90.9.9.9"))) == "100.101.102.103:10578 81.2.3.4:10578");
}

TEST_CASE("With a public address unknown the home network is tried last", "[steamjoin]")
{
    // Own public address unknown: the friend's public address first, then the home network in case it is the same house.
    REQUIRE(Order(Choose(Hosting(), {Net("192.168.1.20", 24)}, {}, 0)) == "81.2.3.4:10578 192.168.1.10:10578");

    Share noPublic = Hosting();
    noPublic.Public = 0;
    noPublic.Vlan.clear();
    REQUIRE(Order(Choose(noPublic, {Net("192.168.1.20", 24)}, {}, 0)) == "192.168.1.10:10578");
    REQUIRE(Choose(noPublic, {Net("10.0.0.2", 8)}, {}, 0).empty());
}

TEST_CASE("Another player's server is joined where it is, when it can be reached", "[steamjoin]")
{
    REQUIRE(Order(Choose(On("203.0.113.7:10578"), {}, {}, 0)) == "203.0.113.7:10578");
    REQUIRE(Choose(On("203.0.113.7:10578"), {}, {}, 0)[0].Kind == Route::Server);
    REQUIRE(Order(Choose(On("play.example.org:10578"), {}, {}, 0)) == "play.example.org:10578");

    // This PC, a launcher's tunnel or a link-local address is never the friend's server.
    REQUIRE(Choose(On("127.0.0.1:10611"), {}, {}, 0).empty());
    REQUIRE(Choose(On("LocalHost:10578"), {}, {}, 0).empty());
    REQUIRE(Choose(On("169.254.3.4:10578"), {}, {}, 0).empty());

    // A private address: from another house it cannot be reached, from the same house or virtual LAN it can.
    REQUIRE(Choose(On("192.168.1.10:10578", "81.2.3.4"), {Net("192.168.1.20", 24)}, {}, Ip("90.9.9.9")).empty());
    REQUIRE(Order(Choose(On("192.168.1.10:10578", "81.2.3.4"), {Net("192.168.1.20", 24)}, {}, Ip("81.2.3.4"))) == "192.168.1.10:10578");
    REQUIRE(Order(Choose(On("26.12.34.56:10578", "81.2.3.4"), {}, {Net("26.1.1.1", 8)}, Ip("90.9.9.9"))) == "26.12.34.56:10578");
    REQUIRE(Choose(On("26.12.34.56:10578", "81.2.3.4"), {}, {}, Ip("90.9.9.9")).empty());
}

TEST_CASE("A STUN answer gives the public address", "[steamjoin]")
{
    const uint8_t transaction[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t request[20];
    StunRequest(transaction, request);
    REQUIRE(request[0] == 0x00);
    REQUIRE(request[1] == 0x01);
    REQUIRE(request[4] == 0x21);

    // A binding success with one XOR-MAPPED-ADDRESS: 81.2.3.4 XOR the magic cookie.
    uint8_t answer[32] = {0x01, 0x01, 0x00, 0x0C, 0x21, 0x12, 0xA4, 0x42};
    std::copy(std::begin(transaction), std::end(transaction), answer + 8);
    const uint8_t attribute[12] = {0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 81 ^ 0x21, 2 ^ 0x12, 3 ^ 0xA4, 4 ^ 0x42};
    std::copy(std::begin(attribute), std::end(attribute), answer + 20);
    REQUIRE(StunAnswer(answer, sizeof(answer), transaction) == Ip("81.2.3.4"));

    // Another request's answer is not ours.
    const uint8_t other[12] = {};
    REQUIRE(StunAnswer(answer, sizeof(answer), other) == 0);
    REQUIRE(StunAnswer(answer, 10, transaction) == 0);
}
