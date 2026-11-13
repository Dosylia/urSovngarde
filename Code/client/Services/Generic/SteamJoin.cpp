#include <Services/SteamJoin.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

namespace SteamJoin
{
namespace
{
constexpr size_t kMaxText = 255;
constexpr size_t kMaxNetworks = 4;
constexpr uint32_t kStunCookie = 0x2112A442;

uint32_t Mask(const uint8_t aPrefix) noexcept
{
    return aPrefix == 0 ? 0 : 0xFFFFFFFFu << (32 - aPrefix);
}

// Tailscale gives each address a /32; its peers all sit in 100.64.0.0/10.
bool IsTailnet(const uint32_t aIp) noexcept
{
    return (aIp & Mask(10)) == 0x64400000u;
}

bool SameNetwork(const Network& acA, const Network& acB) noexcept
{
    if (IsTailnet(acA.Ip) && IsTailnet(acB.Ip))
        return true;
    const uint32_t cMask = Mask(std::min(acA.Prefix, acB.Prefix));
    return (acA.Ip & cMask) == (acB.Ip & cMask);
}

const Network* Match(const std::vector<Network>& acTheirs, const std::vector<Network>& acOurs) noexcept
{
    for (const auto& theirs : acTheirs)
        for (const auto& ours : acOurs)
            if (SameNetwork(theirs, ours))
                return &theirs;
    return nullptr;
}

std::string WithPort(const uint32_t aIp, const uint16_t aPort)
{
    return ToText(aIp) + ":" + std::to_string(aPort);
}

bool ParseNumber(const std::string_view aText, const uint32_t aMax, uint32_t& aValue)
{
    if (aText.empty() || aText.size() > 5)
        return false;
    const auto [cEnd, cError] = std::from_chars(aText.data(), aText.data() + aText.size(), aValue);
    return cError == std::errc() && cEnd == aText.data() + aText.size() && aValue <= aMax;
}

bool ParseNetworks(std::string_view aText, std::vector<Network>& aNetworks)
{
    while (!aText.empty())
    {
        const size_t cComma = aText.find(',');
        const std::string_view cItem = aText.substr(0, cComma);
        aText = cComma == std::string_view::npos ? std::string_view{} : aText.substr(cComma + 1);

        const size_t cSlash = cItem.find('/');
        Network network;
        uint32_t prefix = 0;
        if (cSlash == std::string_view::npos || !ParseIp(cItem.substr(0, cSlash), network.Ip) || !ParseNumber(cItem.substr(cSlash + 1), 32, prefix))
            return false;
        if (aNetworks.size() == kMaxNetworks)
            return false;
        network.Prefix = static_cast<uint8_t>(prefix);
        aNetworks.push_back(network);
    }
    return true;
}

// A server another player is on: a name or an address, then a port.
bool IsServer(const std::string_view aText)
{
    const size_t cColon = aText.rfind(':');
    uint32_t port = 0;
    if (cColon == std::string_view::npos || cColon == 0 || cColon > 253 || !ParseNumber(aText.substr(cColon + 1), 65535, port) || port == 0)
        return false;

    const auto cHost = aText.substr(0, cColon);
    return std::all_of(cHost.begin(), cHost.end(), [](const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'; });
}

std::string Join(const std::vector<Network>& acNetworks)
{
    std::string text;
    for (size_t i = 0; i < acNetworks.size() && i < kMaxNetworks; ++i)
        text += (i ? "," : "") + ToText(acNetworks[i].Ip) + "/" + std::to_string(acNetworks[i].Prefix);
    return text;
}

uint16_t Read16(const uint8_t* apData)
{
    return static_cast<uint16_t>(apData[0] << 8 | apData[1]);
}

uint32_t Read32(const uint8_t* apData)
{
    return static_cast<uint32_t>(apData[0]) << 24 | static_cast<uint32_t>(apData[1]) << 16 | static_cast<uint32_t>(apData[2]) << 8 | apData[3];
}
} // namespace

std::string ToText(const uint32_t aIp)
{
    return std::to_string(aIp >> 24) + "." + std::to_string(aIp >> 16 & 0xFF) + "." + std::to_string(aIp >> 8 & 0xFF) + "." + std::to_string(aIp & 0xFF);
}

bool ParseIp(std::string_view aText, uint32_t& aIp)
{
    uint32_t ip = 0;
    for (int part = 0; part < 4; ++part)
    {
        const size_t cDot = aText.find('.');
        if ((part < 3) == (cDot == std::string_view::npos))
            return false;

        uint32_t value = 0;
        if (!ParseNumber(aText.substr(0, cDot), 255, value))
            return false;
        ip = ip << 8 | value;
        aText = part < 3 ? aText.substr(cDot + 1) : std::string_view{};
    }
    aIp = ip;
    return true;
}

std::string Write(const Share& acShare)
{
    std::string text = "v1";
    if (!acShare.Server.empty())
        text += " server=" + acShare.Server;
    else
    {
        text += " port=" + std::to_string(acShare.Port);
        if (!acShare.Lan.empty())
            text += " lan=" + Join(acShare.Lan);
        if (!acShare.Vlan.empty())
            text += " vlan=" + Join(acShare.Vlan);
    }
    // With another player's server too: a friend tells by it whether a private address is in their own house.
    if (acShare.Public)
        text += " pub=" + ToText(acShare.Public);
    text += acShare.Password ? " pw=1" : " pw=0";
    return text.size() <= kMaxText ? text : std::string{};
}

Share Read(std::string_view aText)
{
    if (aText.size() > kMaxText || aText.substr(0, 2) != "v1")
        return {};
    aText.remove_prefix(2);

    Share share;
    bool hasPort = false;
    while (!aText.empty())
    {
        if (aText.front() != ' ')
            return {};
        aText.remove_prefix(1);

        const size_t cEnd = aText.find(' ');
        const std::string_view cItem = aText.substr(0, cEnd);
        aText = cEnd == std::string_view::npos ? std::string_view{} : aText.substr(cEnd);

        const size_t cEqual = cItem.find('=');
        if (cEqual == std::string_view::npos)
            return {};
        const auto cKey = cItem.substr(0, cEqual);
        const auto cValue = cItem.substr(cEqual + 1);

        uint32_t number = 0;
        if (cKey == "server")
        {
            if (!IsServer(cValue))
                return {};
            share.Server = std::string(cValue);
        }
        else if (cKey == "port")
        {
            if (!ParseNumber(cValue, 65535, number) || number == 0)
                return {};
            share.Port = static_cast<uint16_t>(number);
            hasPort = true;
        }
        else if (cKey == "lan" || cKey == "vlan")
        {
            if (!ParseNetworks(cValue, cKey == "lan" ? share.Lan : share.Vlan))
                return {};
        }
        else if (cKey == "pub")
        {
            if (!ParseIp(cValue, share.Public))
                return {};
        }
        else if (cKey == "pw")
            share.Password = cValue == "1";
        // A key from a newer version is left alone.
    }

    share.Valid = !share.Server.empty() || hasPort;
    return share;
}

bool IsPrivate(const uint32_t aIp)
{
    return (aIp & Mask(8)) == 0x0A000000u      // 10/8
           || (aIp & Mask(12)) == 0xAC100000u  // 172.16/12
           || (aIp & Mask(16)) == 0xC0A80000u  // 192.168/16
           || IsTailnet(aIp)                   // 100.64/10, shared by carriers and Tailscale
           || (aIp & Mask(8)) == 0x19000000u   // 25/8, Hamachi
           || (aIp & Mask(8)) == 0x1A000000u;  // 26/8, Radmin VPN
}

std::vector<Choice> Choose(const Share& acFriend, const std::vector<Network>& acOwnLan, const std::vector<Network>& acOwnVlan, const uint32_t aOwnPublic)
{
    if (!acFriend.Valid)
        return {};

    // The same house: both behind one public address, so the home network reaches the server.
    const bool cSameHouse = aOwnPublic && acFriend.Public == aOwnPublic;
    const bool cPublicUnknown = !aOwnPublic || !acFriend.Public;

    if (!acFriend.Server.empty())
    {
        const size_t cColon = acFriend.Server.rfind(':');
        std::string host = acFriend.Server.substr(0, cColon);
        std::transform(host.begin(), host.end(), host.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        uint32_t ip = 0;
        if (!ParseIp(host, ip))
            return host == "localhost" ? std::vector<Choice>{} : std::vector<Choice>{{acFriend.Server, Route::Server}};
        // This PC itself, a link-local address or no address: never the friend's server.
        if (ip >> 24 == 127 || ip >> 24 == 0 || ip >> 16 == 0xA9FE)
            return {};
        if (!IsPrivate(ip))
            return {{acFriend.Server, Route::Server}};

        // A server on a private address: only a network it is on reaches it.
        const Network cServer{ip, 32};
        const auto cOn = [&cServer](const std::vector<Network>& acNetworks) {
            return std::any_of(acNetworks.begin(), acNetworks.end(), [&cServer](const Network& acOwn) { return SameNetwork(cServer, acOwn); });
        };
        if (cOn(acOwnVlan) || ((cSameHouse || cPublicUnknown) && cOn(acOwnLan)))
            return {{acFriend.Server, Route::Server}};
        return {};
    }

    std::vector<Choice> choices;
    const auto cAdd = [&choices, &acFriend](const uint32_t aIp, const Route aKind) {
        const std::string cAddress = WithPort(aIp, acFriend.Port);
        if (choices.size() < 3 && std::none_of(choices.begin(), choices.end(), [&cAddress](const Choice& acChoice) { return acChoice.Address == cAddress; }))
            choices.push_back({cAddress, aKind});
    };

    if (cSameHouse)
    {
        if (const auto* pNetwork = Match(acFriend.Lan, acOwnLan))
            cAdd(pNetwork->Ip, Route::Lan);
    }
    // A virtual LAN both seem to be on: Radmin and Hamachi give everyone the same range, so it may be another network.
    for (const auto& theirs : acFriend.Vlan)
    {
        for (const auto& ours : acOwnVlan)
        {
            if (SameNetwork(theirs, ours))
            {
                cAdd(theirs.Ip, Route::Vlan);
                break;
            }
        }
    }
    // The friend's public address, which works when they opened the port.
    if (acFriend.Public && !cSameHouse)
        cAdd(acFriend.Public, Route::Public);
    // With a public address unknown, a matching home network is the last guess: two homes often use the same one.
    if (cPublicUnknown)
    {
        if (const auto* pNetwork = Match(acFriend.Lan, acOwnLan))
            cAdd(pNetwork->Ip, Route::Lan);
    }
    return choices;
}

void StunRequest(const uint8_t (&acTransaction)[12], uint8_t (&aRequest)[20])
{
    // A binding request with no attributes: type 0x0001, length 0, the magic cookie, the transaction id.
    std::memset(aRequest, 0, sizeof(aRequest));
    aRequest[1] = 0x01;
    aRequest[4] = static_cast<uint8_t>(kStunCookie >> 24);
    aRequest[5] = static_cast<uint8_t>(kStunCookie >> 16);
    aRequest[6] = static_cast<uint8_t>(kStunCookie >> 8);
    aRequest[7] = static_cast<uint8_t>(kStunCookie);
    std::memcpy(aRequest + 8, acTransaction, sizeof(acTransaction));
}

uint32_t StunAnswer(const uint8_t* apAnswer, const size_t aSize, const uint8_t (&acTransaction)[12])
{
    // A binding success (0x0101) for this request, then attributes padded to 4 bytes.
    if (aSize < 20 || Read16(apAnswer) != 0x0101 || Read32(apAnswer + 4) != kStunCookie || std::memcmp(apAnswer + 8, acTransaction, sizeof(acTransaction)) != 0)
        return 0;

    const size_t cEnd = std::min(aSize, static_cast<size_t>(20 + Read16(apAnswer + 2)));
    uint32_t mapped = 0;
    for (size_t at = 20; at + 4 <= cEnd;)
    {
        const uint16_t cType = Read16(apAnswer + at);
        const uint16_t cLength = Read16(apAnswer + at + 2);
        const uint8_t* pValue = apAnswer + at + 4;
        if (at + 4 + cLength > cEnd)
            break;

        // Family 1 is IPv4: a reserved byte, the family, the port, the address.
        if (cLength >= 8 && pValue[1] == 0x01)
        {
            if (cType == 0x0020)
                return Read32(pValue + 4) ^ kStunCookie; // XOR-MAPPED-ADDRESS, preferred
            if (cType == 0x0001)
                mapped = Read32(pValue + 4); // MAPPED-ADDRESS, from older servers
        }
        at += 4 + ((cLength + 3) & ~size_t{3});
    }
    return mapped;
}
} // namespace SteamJoin
