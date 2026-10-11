#include <Services/PublicStatusService.h>

#include <Events/UpdateEvent.h>
#include <Game/Player.h>
#include <Messages/PlayerPlaceRequest.h>
#include <GameServer.h>
#include <World.h>

#include <console/Setting.h>

#include <cmath>
#include <ctime>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

extern Console::Setting<uint32_t> uMaxPlayerCount;

namespace
{
Console::Setting bPublicStatus{"LiveServices:bPublicStatus", "Publish this server's status (name, player count, character names and where they are) on the urSovngarde website's public server page. Needs the URSOVNGARDE_SERVER_KEY environment variable", false};
Console::StringSetting sPublicAddress{"LiveServices:sPublicAddress", "The address players type to join this server (host:port), shown on the public server page", ""};
Console::StringSetting sPublicStatusUrl{"LiveServices:sPublicStatusUrl", "Where the public server's status is sent", "https://ursovngarde-hub.ursovngarde.workers.dev/servers/public/status"};

constexpr char kKeyVariable[] = "URSOVNGARDE_SERVER_KEY";

// The hub and the website count a status older than 180 seconds as an offline server. Ten seconds while someone plays
// keeps the map moving; a minute when nobody is on keeps the hub's free plan for everything else (Emma, 2026-10-10).
constexpr auto kBusyInterval = std::chrono::seconds(10);
constexpr auto kIdleInterval = std::chrono::seconds(60);
// Someone joining or leaving is pushed at once, but never sooner than this after the previous push.
constexpr auto kMinimumGap = std::chrono::seconds(2);
constexpr auto kTimeout = std::chrono::seconds(5);
// The "online": false push at shutdown: short, the server is closing.
constexpr auto kLastTimeout = std::chrono::seconds(2);
constexpr auto kFailureLogInterval = std::chrono::minutes(5);

// The website shows at most 64 players and the hub takes at most 16 KB; player_count still counts everyone.
constexpr size_t kMaxListedPlayers = 64;
constexpr size_t kMaxNameLength = 40;
constexpr size_t kMaxPlaceLength = 60;
// What the server keeps of a place a client sends, before it is cut to kMaxPlaceLength characters.
constexpr size_t kMaxPlaceBytes = 240;

// The exterior worldspaces of Skyrim VR and its DLCs, by plugin and object id, with the editor id the website draws
// each one's map by. Read from the game's own master files on 2026-10-10 (Skyrim.esm, Update.esm, Dawnguard.esm,
// HearthFires.esm, Dragonborn.esm, SkyrimVR.esm: every WRLD record they hold), not typed from memory. A worldspace
// added by another mod is sent as "<plugin>:<object id in hex>".
struct KnownWorldSpace
{
    std::string_view Plugin;
    uint32_t BaseId;
    std::string_view EditorId;
};

constexpr KnownWorldSpace kKnownWorldSpaces[] = {
    {"Skyrim.esm", 0x00003C, "Tamriel"},
    {"Skyrim.esm", 0x01691D, "WindhelmWorld"},
    {"Skyrim.esm", 0x016BB4, "RiftenWorld"},
    {"Skyrim.esm", 0x016D71, "MarkarthWorld"},
    {"Skyrim.esm", 0x01A26F, "WhiterunWorld"},
    {"Skyrim.esm", 0x01B44A, "CWTestHold"},
    {"Skyrim.esm", 0x01CDD3, "LabyrinthianWorld"},
    {"Skyrim.esm", 0x01CDD9, "LabyrinthianWorld03"},
    {"Skyrim.esm", 0x01E49D, "MossMotherCavernWorld"},
    {"Skyrim.esm", 0x01EE62, "Blackreach"},
    {"Skyrim.esm", 0x01FAE2, "BloatedMansGrottoWorld"},
    {"Skyrim.esm", 0x0204C7, "FallowstoneCaveWorldEnd"},
    {"Skyrim.esm", 0x020BFE, "SouthfringeWorld"},
    {"Skyrim.esm", 0x020DCB, "BlindCliffCaveWorld"},
    {"Skyrim.esm", 0x021EDB, "LabyrinthianWorld04"},
    {"Skyrim.esm", 0x0243DE, "FallowstoneCaveWorldStart"},
    {"Skyrim.esm", 0x0278DD, "SkuldafnWorld"},
    {"Skyrim.esm", 0x029AB7, "DeepwoodRedoubtWorld"},
    {"Skyrim.esm", 0x02A9D8, "ShadowgreenCavernWorld"},
    {"Skyrim.esm", 0x02B101, "RedEagleRedoubtWorld"},
    {"Skyrim.esm", 0x02C965, "DarkwaterWorld"},
    {"Skyrim.esm", 0x02EE41, "Sovngarde"},
    {"Skyrim.esm", 0x034240, "BluePalaceWingWorld"},
    {"Skyrim.esm", 0x035699, "KarthspireRedoubtWorld"},
    {"Skyrim.esm", 0x037EDF, "SolitudeWorld"},
    {"Skyrim.esm", 0x03A9D6, "EldergleamSanctuaryWorld"},
    {"Skyrim.esm", 0x0419E1, "CWSiegeTestWorld"},
    {"Skyrim.esm", 0x046033, "WhiterunDragonsreachWorld"},
    {"Skyrim.esm", 0x04F838, "EastEmpireWarehouse"},
    {"Skyrim.esm", 0x050015, "BrinewaterGrottoWorld"},
    {"Skyrim.esm", 0x069857, "AlftandWorld"},
    {"Skyrim.esm", 0x06ED38, "FXLightWorldSpace"},
    {"Skyrim.esm", 0x094B35, "JaphetsFollyWorld"},
    {"Skyrim.esm", 0x0C350D, "KatariahWorld"},
    {"Skyrim.esm", 0x0C97EB, "FrostmereCryptWorld"},
    {"Skyrim.esm", 0x0D45F0, "WindhelmPitWorldspace"},
    {"Skyrim.esm", 0x104217, "LabyrinthianMazeWorld"},
    {"Update.esm", 0x00293A, "TestSnow"},
    {"Dawnguard.esm", 0x000BB5, "DLC01FalmerValley"},
    {"Dawnguard.esm", 0x001408, "DLC01SoulCairn"},
    {"Dawnguard.esm", 0x001DB8, "DLC1HunterHQWorld"},
    {"Dawnguard.esm", 0x002F64, "DLC1ForebearsHoldout"},
    {"Dawnguard.esm", 0x0042C2, "TestMeganWorld"},
    {"Dawnguard.esm", 0x0048C7, "DLC1AncestorsGladeWorld"},
    {"Dawnguard.esm", 0x004BEA, "DLC1DarkfallPassageWorld"},
    {"Dawnguard.esm", 0x00528D, "DLC01Boneyard"},
    {"Dawnguard.esm", 0x007202, "DLC1VampireCastleCourtyard"},
    {"Dragonborn.esm", 0x000800, "DLC2SolstheimWorld"},
    {"Dragonborn.esm", 0x01C0B2, "DLC2ApocryphaWorld"},
    {"SkyrimVR.esm", 0x0008B8, "VRPlayroom"},
};

// Worldspaces of their own, with their own coordinates, that stand at one place of a bigger map: a player there is
// sent at that place, in the bigger map's coordinates, under their own editor id. Each place is where their doors
// come out, read from Dawnguard.esm on 2026-10-10:
// - Fort Dawnguard's courtyard, on Tamriel's map: its road door (0x110B5);
// - Castle Volkihar's courtyard, on Tamriel's map: the middle of the island's three doors (0xD15B, 0x2888, 0x2889);
// - the Boneyard, on the Soul Cairn's map (DLC01SoulCairn): the Soul Cairn's door to it (0x2FE1).
struct PlacedElsewhere
{
    std::string_view EditorId;
    float X;
    float Y;
};

constexpr PlacedElsewhere kPlacedElsewhere[] = {
    {"DLC1HunterHQWorld", 200505.f, -107812.f},
    {"DLC1VampireCastleCourtyard", -172340.f, 150683.f},
    {"DLC01Boneyard", 8321.f, 11729.f},
};

// Windows-1252's 0x80 to 0x9F; 0 where it has no character.
constexpr char16_t kWindows1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                                       0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};

// Text from a game is not always UTF-8 (string files and typed names can be Windows-1252), and the hub wants JSON,
// which is. A valid UTF-8 sequence passes as it is; any other byte is read as Windows-1252. Cut to aMaxCharacters.
std::string ToUtf8(std::string_view aText, size_t aMaxCharacters)
{
    std::string out;
    size_t characters = 0;
    for (size_t i = 0; i < aText.size() && characters < aMaxCharacters; ++characters)
    {
        const auto byte = [&](size_t aAt) { return aAt < aText.size() ? static_cast<unsigned char>(aText[aAt]) : 0u; };
        const auto continuation = [&](size_t aAt) { return (byte(aAt) & 0xC0) == 0x80; };
        const auto c = static_cast<unsigned char>(byte(i));

        size_t length = 0;
        if (c < 0x80)
            length = 1;
        else if (c >= 0xC2 && c <= 0xDF && continuation(i + 1))
            length = 2;
        else if (c >= 0xE0 && c <= 0xEF && continuation(i + 1) && continuation(i + 2) && !(c == 0xE0 && byte(i + 1) < 0xA0) && !(c == 0xED && byte(i + 1) > 0x9F))
            length = 3;
        else if (c >= 0xF0 && c <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3) && !(c == 0xF0 && byte(i + 1) < 0x90) &&
                 !(c == 0xF4 && byte(i + 1) > 0x8F))
            length = 4;

        if (length)
        {
            out.append(aText.substr(i, length));
            i += length;
            continue;
        }

        const char16_t code = c >= 0xA0 ? c : kWindows1252[c - 0x80];
        if (code == 0)
            out += '?';
        else if (code < 0x800)
        {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        ++i;
    }
    return out;
}

// aText is UTF-8 (ToUtf8): only quotes, backslashes and control characters need escaping.
void AppendJsonString(std::string& aOut, std::string_view aText)
{
    aOut += '"';
    for (const unsigned char c : aText)
    {
        switch (c)
        {
        case '"': aOut += "\\\""; break;
        case '\\': aOut += "\\\\"; break;
        case '\n': aOut += "\\n"; break;
        case '\r': aOut += "\\r"; break;
        case '\t': aOut += "\\t"; break;
        default:
            if (c < 0x20)
                aOut += fmt::format("\\u{:04x}", c);
            else
                aOut += static_cast<char>(c);
        }
    }
    aOut += '"';
}

std::string IsoTime(std::chrono::system_clock::time_point aTime)
{
    const std::time_t time = std::chrono::system_clock::to_time_t(aTime);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string EnvironmentVariable(const char* acpName)
{
#ifdef _WIN32
    char* pValue = nullptr;
    size_t length = 0;
    std::string value;
    if (_dupenv_s(&pValue, &length, acpName) == 0 && pValue)
        value = pValue;
    free(pValue);
    return value;
#else
    const char* pValue = std::getenv(acpName);
    return pValue ? pValue : "";
#endif
}

// Where this system keeps its trusted certificates. The Linux server links its own OpenSSL, built on CI, which looks
// for them where they were on that machine; on Seen's Debian VPS (2026-10-10) every push failed with "SSL server
// verification failed". SSL_CERT_FILE, when set, still wins. Windows reads its own store (httplib).
std::string SystemCaBundle()
{
#ifdef _WIN32
    return {};
#else
    if (!EnvironmentVariable("SSL_CERT_FILE").empty())
        return {};
    for (const char* pPath : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem", "/etc/ssl/ca-bundle.pem"})
    {
        if (std::filesystem::exists(pPath))
            return pPath;
    }
    return {};
#endif
}


bool SameFilename(std::string_view aLeft, std::string_view aRight)
{
    return std::ranges::equal(aLeft, aRight, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

// The website's name for an exterior worldspace: its editor id, or "<plugin>:<object id>" for one no master file has.
// A worldspace id is in this server's mod ids, which are handed out as clients join, so its plugin is looked up by
// that id. Empty when the plugin is not known (then no position is sent).
std::string WorldSpaceName(World& aWorld, const GameId& acWorldSpaceId)
{
    for (const auto& entry : aWorld.ctx().at<ModsComponent>().GetStandardMods())
    {
        if (entry.second.id != acWorldSpaceId.ModId)
            continue;

        const std::string_view plugin{entry.first.c_str(), entry.first.size()};
        for (const auto& known : kKnownWorldSpaces)
        {
            if (known.BaseId == acWorldSpaceId.BaseId && SameFilename(plugin, known.Plugin))
                return std::string(known.EditorId);
        }
        return fmt::format("{}:{:X}", plugin, acWorldSpaceId.BaseId);
    }
    return {};
}

// "https://host/path" as httplib wants it: the scheme and host for the client, the path for the request.
bool SplitUrl(std::string_view aUrl, std::string& aOrigin, std::string& aPath)
{
    const auto scheme = aUrl.find("://");
    if (scheme == std::string_view::npos)
        return false;
    const auto path = aUrl.find('/', scheme + 3);
    aOrigin = std::string(aUrl.substr(0, path));
    aPath = path == std::string_view::npos ? "/" : std::string(aUrl.substr(path));
    return aOrigin.size() > scheme + 3;
}
} // namespace

PublicStatusService::PublicStatusService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&PublicStatusService::OnUpdate>(this))
    , m_playerPlaceConnection(aDispatcher.sink<PacketEvent<PlayerPlaceRequest>>().connect<&PublicStatusService::OnPlayerPlace>(this))
    , m_random(std::random_device{}())
{
    if (!bPublicStatus)
        return;

    // From the environment, never from STServer.ini: the ini gets copied around with the server folder.
    m_key = EnvironmentVariable(kKeyVariable);
    if (m_key.empty())
    {
        spdlog::error("bPublicStatus is on but the {} environment variable is not set: this server's status is not published", kKeyVariable);
        return;
    }

    if (!SplitUrl(sPublicStatusUrl.value(), m_origin, m_path))
    {
        spdlog::error("sPublicStatusUrl '{}' is not a URL: this server's status is not published", sPublicStatusUrl.value());
        return;
    }

    if (std::string_view(sPublicAddress.value()).empty())
        spdlog::warn("bPublicStatus is on but sPublicAddress is empty: the public server page will show no address to join");

    m_startedAt = IsoTime(std::chrono::system_clock::now());
    m_enabled = true;
    m_thread = std::thread(&PublicStatusService::Run, this);

    spdlog::info("Publishing this server's status (name, player count, character names and where they are) to {}, every {} s while players are on, every {} s when nobody is",
                 m_origin, kBusyInterval.count(), kIdleInterval.count());
}

PublicStatusService::~PublicStatusService() noexcept
{
    if (!m_thread.joinable())
        return;

    {
        std::scoped_lock lock(m_mutex);
        // Only once a status went out: m_header holds what it said.
        if (!m_header.empty())
            m_pending = BuildOfflineStatus();
        m_stop = true;
    }
    m_wake.notify_one();
    m_thread.join();
}

void PublicStatusService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_enabled)
        return;

    const auto now = std::chrono::steady_clock::now();
    const uint32_t playerCount = m_world.GetPlayerManager().Count();

    // Someone joining or leaving shows on the page at once rather than at the next push.
    if (playerCount != m_lastPlayerCount)
    {
        m_lastPlayerCount = playerCount;
        m_nextPush = std::min(m_nextPush, m_lastPush + kMinimumGap);
    }

    if (now < m_nextPush)
        return;

    m_lastPush = now;
    m_nextPush = now + (playerCount > 0 ? kBusyInterval : kIdleInterval);
    Queue(BuildStatus());
}

void PublicStatusService::OnPlayerPlace(const PacketEvent<PlayerPlaceRequest>& acMessage) const noexcept
{
    // Kept even while nothing is published: it costs a short string per player.
    const auto& place = acMessage.Packet.Place;
    acMessage.pPlayer->SetPlace(String(place.c_str(), std::min(place.size(), kMaxPlaceBytes)));
    // The client sends only when it changes, so one line per move between places.
    spdlog::info("Player {} is now in '{}'", acMessage.pPlayer->GetId(), ToUtf8({place.c_str(), std::min(place.size(), kMaxPlaceBytes)}, 120));
}

std::string PublicStatusService::BuildStatus() noexcept
{
    const auto* pServer = GameServer::Get();

    std::string header = "\"name\":";
    AppendJsonString(header, ToUtf8({pServer->GetInfo().name.c_str(), pServer->GetInfo().name.size()}, 80));
    header += ",\"address\":";
    AppendJsonString(header, ToUtf8(sPublicAddress.value(), 100));
    header += fmt::format(",\"version\":\"{}.{}.{}\",\"protocol\":", BUILD_MAJOR, BUILD_MINOR, BUILD_PATCH);
    AppendJsonString(header, BUILD_PROTOCOL);
    header += fmt::format(",\"password\":{},\"max_players\":{},\"started_at\":\"{}\"", pServer->IsPasswordProtected() ? "true" : "false",
                          uMaxPlayerCount.value_as<uint32_t>(), m_startedAt);

    std::string players;
    size_t listed = 0;
    uint32_t playerCount = 0;
    TiltedPhoques::Set<uint32_t> present;

    m_world.GetPlayerManager().ForEach([&](Player* pPlayer) {
        ++playerCount;
        present.insert(pPlayer->GetId());
        // Counted, never named or placed.
        if (pPlayer->IsHiddenFromPublicPage() || listed >= kMaxListedPlayers)
            return;
        ++listed;

        if (!players.empty())
            players += ',';

        // The character's name, as the client sent it at connect (TransportService::OnConnected). Never the endpoint,
        // the Discord id or anything else that tells who the person is.
        const auto& username = pPlayer->GetUsername();
        players += "{\"id\":\"" + TokenOf(pPlayer->GetId()) + "\",\"name\":";
        AppendJsonString(players, ToUtf8({username.c_str(), username.size()}, kMaxNameLength));

        if (const auto& place = pPlayer->GetPlace(); !place.empty())
        {
            players += ",\"location\":";
            AppendJsonString(players, ToUtf8({place.c_str(), place.size()}, kMaxPlaceLength));
        }

        // A position only outdoors, in any worldspace (Tamriel, Solstheim, the Soul Cairn, Blackreach...), named by
        // its editor id so the page can pick that world's map. Indoors (no worldspace) the player is listed without a
        // dot on any map. A hidden player never gets this far.
        const auto& cell = pPlayer->GetCellComponent();
        const auto character = pPlayer->GetCharacter();
        if (character && m_world.valid(*character) && cell.WorldSpaceId)
        {
            const std::string worldSpace = WorldSpaceName(m_world, cell.WorldSpaceId);
            const auto* pMovement = m_world.try_get<MovementComponent>(*character);
            if (pMovement && !worldSpace.empty())
            {
                float x = pMovement->Position.x;
                float y = pMovement->Position.y;
                for (const auto& placed : kPlacedElsewhere)
                {
                    if (placed.EditorId == worldSpace)
                    {
                        x = placed.X;
                        y = placed.Y;
                    }
                }

                // Rotation.z is the game's heading in radians: 0 faces north, and it turns clockwise.
                const long heading = std::lround(glm::degrees(pMovement->Rotation.z));
                players += ",\"worldspace\":";
                AppendJsonString(players, ToUtf8(worldSpace, 120));
                players += fmt::format(",\"x\":{},\"y\":{},\"heading\":{}", std::lround(x), std::lround(y), ((heading % 360) + 360) % 360);
            }
        }
        players += '}';
    });

    for (auto itor = m_tokens.begin(); itor != m_tokens.end();)
    {
        if (present.count(itor->first) != 0)
            ++itor;
        else
            itor = m_tokens.erase(itor);
    }

    m_header = header;

    return fmt::format("{{\"online\":true,{},\"player_count\":{},\"server_time\":\"{}\",\"players\":[{}]}}", header, playerCount,
                       IsoTime(std::chrono::system_clock::now()), players);
}

std::string PublicStatusService::BuildOfflineStatus() const noexcept
{
    return fmt::format("{{\"online\":false,{},\"player_count\":0,\"server_time\":\"{}\",\"players\":[]}}", m_header, IsoTime(std::chrono::system_clock::now()));
}

const std::string& PublicStatusService::TokenOf(uint32_t aPlayerId) noexcept
{
    auto itor = m_tokens.find(aPlayerId);
    if (itor == m_tokens.end())
        itor = m_tokens.emplace(aPlayerId, fmt::format("{:08x}", static_cast<uint32_t>(m_random()))).first;
    return itor->second;
}

void PublicStatusService::Queue(std::string aBody) noexcept
{
    {
        std::scoped_lock lock(m_mutex);
        // A status still waiting is replaced: only the newest is worth sending.
        m_pending = std::move(aBody);
    }
    m_wake.notify_one();
}

void PublicStatusService::Run() noexcept
{
    std::unique_lock lock(m_mutex);
    while (true)
    {
        m_wake.wait(lock, [this] { return m_pending.has_value() || m_stop; });
        if (!m_pending)
            return;

        const std::string body = std::move(*m_pending);
        m_pending.reset();
        // Once m_stop is set, what is pending is the shutdown's "online": false.
        const bool last = m_stop;

        lock.unlock();
        Post(body, last ? kLastTimeout : kTimeout);
        lock.lock();

        if (last)
            return;
    }
}

bool PublicStatusService::Post(const std::string& acBody, std::chrono::seconds aTimeout) noexcept
{
    httplib::Client client(m_origin);
    client.set_connection_timeout(aTimeout);
    client.set_read_timeout(aTimeout);
    client.set_write_timeout(aTimeout);
    // The certificate is checked, unlike ServerListService's announce: this request carries the key.
    if (static const std::string s_caBundle = SystemCaBundle(); !s_caBundle.empty())
        client.set_ca_cert_path(s_caBundle);

    const httplib::Headers headers{{"Authorization", "Bearer " + m_key}};
    const auto response = client.Post(m_path, headers, acBody, "application/json");

    if (response && response->status >= 200 && response->status < 300)
    {
        if (!m_confirmed)
            spdlog::info("Public status: the hub took this server's status");
        else if (m_failing)
            spdlog::info("Public status: the hub is reachable again");
        m_confirmed = true;
        m_failing = false;
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!m_failing || now - m_lastFailureLog >= kFailureLogInterval)
    {
        if (!response)
            spdlog::warn("Public status: could not reach the hub ({}); trying again at the next push", httplib::to_string(response.error()));
        else if (response->status == 401)
            spdlog::error("Public status: the hub refused the key (401); check {} on this machine against the hub's SERVER_KEY", kKeyVariable);
        else
            spdlog::warn("Public status: the hub answered {} ({}); trying again at the next push", response->status, response->body.substr(0, 200));
        m_lastFailureLog = now;
    }
    m_failing = true;
    return false;
}
