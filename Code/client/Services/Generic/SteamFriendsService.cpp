#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <Services/SteamFriendsService.h>
#include <Services/JsonText.h>
#include <Services/OverlayService.h>
#include <Services/TransportService.h>
#include <Services/VRConnectService.h>
#include <OverlayApp.hpp>

#include <Events/UpdateEvent.h>

#include <Utils.h>
#include <World.h>

// windows.h brings the first sockets API unless the build leaves it out; either one has what one UDP question needs.
#ifndef _WINSOCKAPI_
#include <winsock2.h>
#endif
#include <iphlpapi.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <random>
#include <thread>

namespace
{
using SteamJoin::Network;

constexpr auto kLookEvery = 5s;
constexpr auto kNetworksEvery = 30s;
constexpr auto kRequestEvery = 30s;
constexpr auto kResendFriendsEvery = 30s;
constexpr auto kPublicAgainAfter = 5min;
constexpr uint32_t kSkyrimVrApp = 611670;
constexpr int kImmediateFriends = 0x04;
constexpr size_t kMaxFriends = 50;
constexpr ULONG kAddressFamilyV6 = 23; // AF_INET6, which the first sockets API leaves out
constexpr const char* kShareKey = "ursovngarde";
// The launcher's, while it hosts.
constexpr const char* kCodeKey = "ursovngarde_code";

// MIB_UDP6ROW_OWNER_PID and its table, which the headers give only with the second sockets API.
struct Udp6Row
{
    UCHAR LocalAddr[16];
    DWORD LocalScopeId;
    DWORD LocalPort;
    DWORD OwningPid;
};
struct Udp6Table
{
    DWORD NumEntries;
    Udp6Row Rows[1];
};
static_assert(sizeof(Udp6Row) == 28);

// Steam's FriendGameInfo_t.
struct FriendGameInfo
{
    uint64_t GameId;
    uint32_t GameIp;
    uint16_t GamePort;
    uint16_t QueryPort;
    uint64_t Lobby;
};
static_assert(sizeof(FriendGameInfo) == 24);

std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return aText;
}

uint8_t PrefixOf(const uint32_t aMask)
{
    uint8_t prefix = 0;
    while (prefix < 32 && (aMask & 0x80000000u >> prefix))
        ++prefix;
    return prefix;
}

const char* RouteName(const SteamJoin::Route aRoute)
{
    switch (aRoute)
    {
    case SteamJoin::Route::Server: return "server";
    case SteamJoin::Route::Lan: return "lan";
    case SteamJoin::Route::Vlan: return "vlan";
    case SteamJoin::Route::Public: return "public";
    }
    return "";
}

// The networks this PC's server is reached on: home networks (an adapter with a gateway) and virtual LANs.
// Other adapters without a gateway (Hyper-V, WSL, VirtualBox) are left out: two PCs often share their ranges.
void ReadNetworks(std::vector<Network>& aLan, std::vector<Network>& aVlan)
{
    aLan.clear();
    aVlan.clear();

    std::vector<uint8_t> buffer;
    ULONG size = 16 * 1024;
    DWORD result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries)
    {
        buffer.resize(size);
        result = GetAdaptersInfo(reinterpret_cast<IP_ADAPTER_INFO*>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
        return;

    for (const auto* pAdapter = reinterpret_cast<const IP_ADAPTER_INFO*>(buffer.data()); pAdapter; pAdapter = pAdapter->Next)
    {
        const std::string cDescription = Lower(pAdapter->Description);
        const bool cVirtual = cDescription.find("radmin") != std::string::npos || cDescription.find("zerotier") != std::string::npos ||
                              cDescription.find("tailscale") != std::string::npos || cDescription.find("hamachi") != std::string::npos;
        uint32_t gateway = 0;
        const bool cHome = SteamJoin::ParseIp(pAdapter->GatewayList.IpAddress.String, gateway) && gateway != 0;
        if (!cVirtual && !cHome)
            continue;

        for (const auto* pAddress = &pAdapter->IpAddressList; pAddress; pAddress = pAddress->Next)
        {
            uint32_t ip = 0;
            uint32_t mask = 0;
            if (!SteamJoin::ParseIp(pAddress->IpAddress.String, ip) || !SteamJoin::ParseIp(pAddress->IpMask.String, mask) || ip == 0 || ip >> 16 == 0xA9FE)
                continue;

            auto& networks = cVirtual ? aVlan : aLan;
            if (networks.size() < 4)
                networks.push_back({ip, PrefixOf(mask)});
        }
    }
}

bool IsOwnAddress(const uint32_t aIp, const std::vector<Network>& acLan, const std::vector<Network>& acVlan, const uint32_t aPublic)
{
    const auto cSame = [aIp](const Network& acNetwork) { return acNetwork.Ip == aIp; };
    return aIp >> 24 == 127 || (aPublic && aIp == aPublic) || std::any_of(acLan.begin(), acLan.end(), cSame) || std::any_of(acVlan.begin(), acVlan.end(), cSame);
}

template <class TTable> std::vector<uint8_t> UdpTable(const ULONG aFamily)
{
    std::vector<uint8_t> buffer;
    DWORD size = 64 * 1024;
    DWORD result = ERROR_INSUFFICIENT_BUFFER;
    for (int tries = 0; tries < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++tries)
    {
        buffer.resize(size);
        result = GetExtendedUdpTable(buffer.data(), &size, FALSE, aFamily, UDP_TABLE_OWNER_PID, 0);
    }
    if (result != NO_ERROR || buffer.size() < sizeof(TTable))
        return {};
    return buffer;
}

// A server on this PC that the network reaches: a UDP socket on that port, bound to every address or to one of this PC's.
// The launcher's tunnel for a friend's code is bound to 127.0.0.1 alone, so it is not this player's server.
bool ServerListens(const uint16_t aPort, const std::vector<Network>& acLan, const std::vector<Network>& acVlan)
{
    const auto cV4 = UdpTable<MIB_UDPTABLE_OWNER_PID>(AF_INET);
    if (!cV4.empty())
    {
        const auto* pTable = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(cV4.data());
        for (DWORD i = 0; i < pTable->dwNumEntries; ++i)
        {
            const auto& row = pTable->table[i];
            if (ntohs(static_cast<u_short>(row.dwLocalPort)) != aPort)
                continue;
            const uint32_t cAddress = ntohl(row.dwLocalAddr);
            if (cAddress == 0 || (cAddress >> 24 != 127 && IsOwnAddress(cAddress, acLan, acVlan, 0)))
                return true;
        }
    }

    // A dual-stack server listens on the IPv6 table's "any" address.
    const auto cV6 = UdpTable<Udp6Table>(kAddressFamilyV6);
    if (!cV6.empty())
    {
        const auto* pTable = reinterpret_cast<const Udp6Table*>(cV6.data());
        for (DWORD i = 0; i < pTable->NumEntries; ++i)
        {
            const auto& row = pTable->Rows[i];
            if (ntohs(static_cast<u_short>(row.LocalPort)) == aPort &&
                std::all_of(std::begin(row.LocalAddr), std::end(row.LocalAddr), [](const UCHAR c) { return c == 0; }))
                return true;
        }
    }
    return false;
}

// The address the internet sees, from a public STUN server: one small UDP question, the answer within 2 s.
uint32_t AskStun() noexcept
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return 0;

    std::random_device random;
    uint32_t found = 0;
    const std::pair<const char*, uint16_t> cServers[] = {{"stun.l.google.com", 19302}, {"stun.cloudflare.com", 3478}};
    for (const auto& [cHost, cPort] : cServers)
    {
#pragma warning(suppress : 4996)
        const hostent* pHost = gethostbyname(cHost);
        if (!pHost || pHost->h_addrtype != AF_INET || !pHost->h_addr_list[0])
            continue;

        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(cPort);
        std::memcpy(&to.sin_addr, pHost->h_addr_list[0], 4);

        const SOCKET cSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (cSocket == INVALID_SOCKET)
            continue;
        const DWORD cTimeout = 2000;
        setsockopt(cSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&cTimeout), sizeof(cTimeout));

        uint8_t transaction[12];
        for (auto& byte : transaction)
            byte = static_cast<uint8_t>(random());
        uint8_t request[20];
        SteamJoin::StunRequest(transaction, request);

        if (sendto(cSocket, reinterpret_cast<const char*>(request), sizeof(request), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == sizeof(request))
        {
            uint8_t answer[512];
            const int cSize = recv(cSocket, reinterpret_cast<char*>(answer), sizeof(answer), 0);
            if (cSize > 0)
                found = SteamJoin::StunAnswer(answer, static_cast<size_t>(cSize), transaction);
        }
        closesocket(cSocket);
        if (found)
            break;
    }

    WSACleanup();
    return found;
}
} // namespace

// Steam's friends functions from the game's own steam_api64.dll (SteamFriends015, the version it was built with).
struct SteamFriendsService::Steam
{
    using TSteamClient = intptr_t (*)();
    using THandle = int32_t (*)();
    using TGetInterface = intptr_t (*)(intptr_t, int32_t, int32_t, const char*);
    using TSetRichPresence = bool (*)(intptr_t, const char*, const char*);
    using TGetFriendCount = int (*)(intptr_t, int);
    using TGetFriendByIndex = uint64_t (*)(intptr_t, int, int);
    using TGetFriendGamePlayed = bool (*)(intptr_t, uint64_t, FriendGameInfo*);
    using TGetFriendRichPresence = const char* (*)(intptr_t, uint64_t, const char*);
    using TGetFriendPersonaName = const char* (*)(intptr_t, uint64_t);
    using TRequestFriendRichPresence = void (*)(intptr_t, uint64_t);

    intptr_t Friends = 0;
    TSetRichPresence SetRichPresence = nullptr;
    TGetFriendCount GetFriendCount = nullptr;
    TGetFriendByIndex GetFriendByIndex = nullptr;
    TGetFriendGamePlayed GetFriendGamePlayed = nullptr;
    TGetFriendRichPresence GetFriendRichPresence = nullptr;
    TGetFriendPersonaName GetFriendPersonaName = nullptr;
    TRequestFriendRichPresence RequestFriendRichPresence = nullptr;

    // A copy: Steam may reuse the buffer it returns at the next call.
    std::string RichPresence(const uint64_t aId, const char* apKey) const
    {
        const char* pText = GetFriendRichPresence(Friends, aId, apKey);
        return pText ? pText : "";
    }

    static std::unique_ptr<Steam> Load() noexcept
    {
        const HMODULE cModule = GetModuleHandleW(L"steam_api64.dll");
        if (!cModule)
        {
            spdlog::warn("SteamFriends: steam_api64.dll is not loaded in the game; no Steam friends in the menu");
            return nullptr;
        }

        const auto pSteamClient = reinterpret_cast<TSteamClient>(GetProcAddress(cModule, "SteamClient"));
        const auto pGetUser = reinterpret_cast<THandle>(GetProcAddress(cModule, "SteamAPI_GetHSteamUser"));
        const auto pGetPipe = reinterpret_cast<THandle>(GetProcAddress(cModule, "SteamAPI_GetHSteamPipe"));
        const auto pGetFriends = reinterpret_cast<TGetInterface>(GetProcAddress(cModule, "SteamAPI_ISteamClient_GetISteamFriends"));

        auto steam = std::make_unique<Steam>();
        steam->SetRichPresence = reinterpret_cast<TSetRichPresence>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_SetRichPresence"));
        steam->GetFriendCount = reinterpret_cast<TGetFriendCount>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_GetFriendCount"));
        steam->GetFriendByIndex = reinterpret_cast<TGetFriendByIndex>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_GetFriendByIndex"));
        steam->GetFriendGamePlayed = reinterpret_cast<TGetFriendGamePlayed>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_GetFriendGamePlayed"));
        steam->GetFriendRichPresence = reinterpret_cast<TGetFriendRichPresence>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_GetFriendRichPresence"));
        steam->GetFriendPersonaName = reinterpret_cast<TGetFriendPersonaName>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_GetFriendPersonaName"));
        steam->RequestFriendRichPresence = reinterpret_cast<TRequestFriendRichPresence>(GetProcAddress(cModule, "SteamAPI_ISteamFriends_RequestFriendRichPresence"));

        if (!pSteamClient || !pGetUser || !pGetPipe || !pGetFriends || !steam->SetRichPresence || !steam->GetFriendCount || !steam->GetFriendByIndex ||
            !steam->GetFriendGamePlayed || !steam->GetFriendRichPresence || !steam->GetFriendPersonaName || !steam->RequestFriendRichPresence)
        {
            spdlog::warn("SteamFriends: a Steam friends function is missing from steam_api64.dll; no Steam friends in the menu");
            return nullptr;
        }

        const intptr_t cClient = pSteamClient();
        const int32_t cUser = pGetUser();
        const int32_t cPipe = pGetPipe();
        if (!cClient || !cUser || !cPipe)
        {
            spdlog::warn("SteamFriends: Steam is not initialised in the game; no Steam friends in the menu");
            return nullptr;
        }

        steam->Friends = pGetFriends(cClient, cUser, cPipe, "SteamFriends015");
        if (!steam->Friends)
        {
            spdlog::warn("SteamFriends: Steam has no SteamFriends015; no Steam friends in the menu");
            return nullptr;
        }
        return steam;
    }
};

SteamFriendsService::SteamFriendsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SteamFriendsService::OnUpdate>(this);
}

// Nothing is said to Steam here: this may run after the game has shut Steam down, and Steam clears a game's rich presence itself.
SteamFriendsService::~SteamFriendsService() noexcept = default;

void SteamFriendsService::SetSharing(const bool aSharing) noexcept
{
    if (aSharing != m_sharing)
        spdlog::info("SteamFriends: Steam friends {} where this player plays (menu setting)", aSharing ? "see" : "don't see");
    m_sharing = aSharing;
    m_nextLook = {};
}

void SteamFriendsService::JoinFriend(const std::string& acFriendId, const std::string& acPassword) noexcept
{
    uint64_t id = 0;
    const auto [cEnd, cError] = std::from_chars(acFriendId.data(), acFriendId.data() + acFriendId.size(), id);
    std::vector<SteamJoin::Choice> choices;
    if (m_steam && cError == std::errc() && cEnd == acFriendId.data() + acFriendId.size())
        choices = SteamJoin::Choose(SteamJoin::Read(m_steam->RichPresence(id, kShareKey)), m_lan, m_vlan, m_public);

    if (choices.empty())
    {
        spdlog::info("SteamFriends: a Steam friend's server can't be reached from here (no network in common, no public address)");
        Utils::ShowHudMessage("urSovngarde: your friend's server can't be reached from here; a virtual LAN or an open port is needed");
        SendToPage("steamJoinFailed", "notReachable");
        return;
    }

    std::vector<std::string> addresses;
    std::string routes;
    for (const auto& choice : choices)
    {
        addresses.push_back(choice.Address);
        routes += (routes.empty() ? "" : ", then ") + std::string(RouteName(choice.Kind));
    }
    spdlog::info("SteamFriends: joining a Steam friend's server by {}", routes);
    m_world.ctx().at<VRConnectService>().ConnectTo(addresses, acPassword);
}

void SteamFriendsService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextLook)
        return;
    m_nextLook = cNow + kLookEvery;

    if (!m_triedSteam)
    {
        m_triedSteam = true;
        m_steam = Steam::Load();
    }
    if (!m_steam)
        return;

    if (cNow >= m_nextNetworks)
    {
        ReadNetworks(m_lan, m_vlan);
        m_nextNetworks = cNow + kNetworksEvery;
    }

    if (const uint32_t cFound = m_lookup->Address.load(); cFound && cFound != m_public)
    {
        m_public = cFound;
        spdlog::info("SteamFriends: the public address is known");
    }

    Publish();
    SendFriends(cNow);
}

void SteamFriendsService::Publish() noexcept
{
    std::string value;
    bool hosting = false;
    SteamJoin::Share share;
    if (m_sharing && m_transport.IsOnline())
    {
        const std::string& cEndpoint = m_transport.GetServerEndpoint();
        const size_t cColon = cEndpoint.rfind(':');
        const std::string cHost = Lower(cEndpoint.substr(0, cColon));
        uint32_t port = 10578;
        bool portOk = true;
        if (cColon != std::string::npos)
        {
            const auto [cPortEnd, cPortError] = std::from_chars(cEndpoint.data() + cColon + 1, cEndpoint.data() + cEndpoint.size(), port);
            portOk = cPortError == std::errc() && cPortEnd == cEndpoint.data() + cEndpoint.size() && port > 0 && port <= 65535;
        }

        uint32_t ip = 0;
        const bool cIpv4 = SteamJoin::ParseIp(cHost, ip);
        const bool cOwn = cHost == "localhost" || (cIpv4 && IsOwnAddress(ip, m_lan, m_vlan, m_public));
        // An IPv6 address or a port that does not read is left out, and so is a local address no server of this PC listens on:
        // the launcher's tunnel for a friend's code.
        if (portOk && cHost.find(':') == std::string::npos && cHost.find('[') == std::string::npos)
        {
            hosting = cOwn && ServerListens(static_cast<uint16_t>(port), m_lan, m_vlan);
            share.Valid = hosting || !cOwn;
        }

        if (share.Valid)
        {
            share.Password = m_transport.HasServerPassword();
            share.Public = m_public;
            if (hosting)
            {
                share.Port = static_cast<uint16_t>(port);
                share.Lan = m_lan;
                share.Vlan = m_vlan;
            }
            else
                share.Server = cEndpoint;
            AskPublicAddress();
            value = SteamJoin::Write(share);
        }
    }

    if (value == m_published)
        return;
    m_steam->SetRichPresence(m_steam->Friends, kShareKey, value.empty() ? nullptr : value.c_str());
    m_published = value;

    if (value.empty())
        spdlog::info("SteamFriends: Steam friends no longer see where this player plays");
    else if (hosting)
        spdlog::info("SteamFriends: Steam friends see this player's own server: {} home and {} virtual LAN networks, public address {}", share.Lan.size(), share.Vlan.size(),
                     share.Public ? "known" : "not known yet");
    else
        spdlog::info("SteamFriends: Steam friends see the server this player is on");
}

void SteamFriendsService::SendFriends(const std::chrono::steady_clock::time_point aNow) noexcept
{
    const bool cRequest = aNow >= m_nextRequest;
    if (cRequest)
        m_nextRequest = aNow + kRequestEvery;

    std::string items;
    size_t shown = 0;
    bool friendHosts = false;
    const int cCount = m_steam->GetFriendCount(m_steam->Friends, kImmediateFriends);
    for (int i = 0; i < cCount && shown < kMaxFriends; ++i)
    {
        const uint64_t cId = m_steam->GetFriendByIndex(m_steam->Friends, i, kImmediateFriends);
        FriendGameInfo game{};
        if (!m_steam->GetFriendGamePlayed(m_steam->Friends, cId, &game) || (game.GameId & 0xFFFFFF) != kSkyrimVrApp || (game.GameId >> 24 & 0xFF) != 0)
            continue;
        if (cRequest)
            m_steam->RequestFriendRichPresence(m_steam->Friends, cId);

        const std::string cShareText = m_steam->RichPresence(cId, kShareKey);
        const std::string cCode = m_steam->RichPresence(cId, kCodeKey).substr(0, 16);
        if (cShareText.empty() && cCode.empty())
            continue;

        const SteamJoin::Share cShare = SteamJoin::Read(cShareText);
        const bool cHosting = cShare.Valid && cShare.Server.empty();
        friendHosts |= cHosting || !cCode.empty();
        const auto cChoices = SteamJoin::Choose(cShare, m_lan, m_vlan, m_public);
        const char* pName = m_steam->GetFriendPersonaName(m_steam->Friends, cId);

        items += fmt::format(
            "{}{{\"id\":\"{}\",\"name\":{},\"hosting\":{},\"onServer\":{},\"reachable\":{},\"route\":\"{}\",\"password\":{},\"code\":{}}}", shown ? "," : "", cId,
            JsonQuoted(pName ? pName : ""), cHosting, !cShare.Server.empty(), !cChoices.empty(), cChoices.empty() ? "" : RouteName(cChoices[0].Kind),
            cShare.Password, JsonQuoted(cCode));
        ++shown;
    }

    // Choosing between a friend's home and public addresses needs this PC's own.
    if (friendHosts)
        AskPublicAddress();

    if (shown != m_friendsLogged)
    {
        spdlog::info("SteamFriends: {} Steam friends playing urSovngarde show where they play", shown);
        m_friendsLogged = shown;
    }

    // Sent again now and then, for a page loaded since.
    const std::string cText = "{\"friends\":[" + items + "]}";
    if (cText == m_sentFriends && aNow < m_resendFriends)
        return;
    m_sentFriends = cText;
    m_resendFriends = aNow + kResendFriendsEvery;
    SendToPage("steamFriends", cText);
}

void SteamFriendsService::AskPublicAddress() noexcept
{
    // Only while sharing is on, once known, and again a while after no STUN server answered.
    const auto cNow = std::chrono::steady_clock::now();
    if (!m_sharing || m_public || cNow < m_nextPublicAsk || m_lookup->Busy.exchange(true))
        return;
    m_nextPublicAsk = cNow + kPublicAgainAfter;

    std::thread(
        [lookup = m_lookup]()
        {
            lookup->Address = AskStun();
            lookup->Busy = false;
        })
        .detach();
}

void SteamFriendsService::SendToPage(const char* apEvent, const std::string& acText) const noexcept
{
    auto* pApp = m_world.ctx().at<OverlayService>().GetOverlayApp();
    if (!pApp)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetString(0, acText);
    pApp->ExecuteAsync(apEvent, pArguments);
}

#endif
