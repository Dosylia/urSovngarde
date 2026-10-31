
#include <Services/TransportService.h>
#include <steam/isteamnetworkingutils.h>
#include <PerfScope.h>

#include <Events/ConnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>

#include <TimeManager.h>

#include <Forms/TESNPC.h>
#include <TiltedOnlinePCH.h>
#include <World.h>

#include <Services/SendBuffer.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/NotifySettingsChange.h>
#include <Packet.hpp>

#include <ScriptExtender.h>
#include <steam/isteamnetworkingutils.h>

#pragma comment(lib, "advapi32.lib")

// The launcher's "Hide me from the public server page" setting, stored beside the game path (oobe/PathSelection.cpp).
// Read at every connection, so a change applies at the next one.
static bool IsHiddenFromPublicPage() noexcept
{
#ifdef SKYRIMVR
    constexpr wchar_t kKey[] = LR"(Software\TiltedPhoques\TiltedEvolution\Skyrim VR)";
#else
    constexpr wchar_t kKey[] = LR"(Software\TiltedPhoques\TiltedEvolution\Skyrim Special Edition)";
#endif
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kKey, L"HideFromPublicPage", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value != 0;
}
#include <Services/DiscordService.h>

// #include <imgui_internal.h>

static constexpr wchar_t kMO2DllName[] = L"usvfs_x64.dll";

using TiltedPhoques::Packet;

TransportService::TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
{
    // The networking library fixes each connection's send rate at connect, 128 KiB/s for any ping over about 33 ms.
    // Movement at 30 Hz with VR poses and NPCs in a fight passes that, and a full queue then refuses every send, reliable ones too.
    if (ISteamNetworkingUtils* pUtils = SteamNetworkingUtils())
    {
        pUtils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 512 * 1024);
        pUtils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 2 * 1024 * 1024);
    }

    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&TransportService::HandleUpdate>(this);
    m_settingsChangeConnection = m_dispatcher.sink<NotifySettingsChange>().connect<&TransportService::HandleNotifySettingsChange>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&TransportService::HandleConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&TransportService::HandleDisconnected>(this);

    m_connected = false;

    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
        {
            const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage));
            m_dispatcher.trigger(*pRealMessage);
        };

        return false;
    };

    ServerMessageFactory::Visit(handlerGenerator);

    // Override authentication response
    m_messageHandlers[AuthenticationResponse::Opcode] = [this](UniquePtr<ServerMessage>& apMessage)
    {
        const auto pRealMessage = TiltedPhoques::CastUnique<AuthenticationResponse>(std::move(apMessage));
        HandleAuthenticationResponse(*pRealMessage);
    };
}

bool TransportService::Send(const ClientMessage& acMessage, TiltedPhoques::EPacketFlags aFlags) const noexcept
{
    static thread_local ScratchAllocator s_allocator(SendBuffer::kScratchBytes);

    struct ScopedReset
    {
        ~ScopedReset() { s_allocator.Reset(); }
    } allocatorGuard;

    if (IsConnected())
    {
        ScopedAllocator _{s_allocator};

        Buffer buffer(SendBuffer::kBufferBytes);
        Buffer::Writer writer(&buffer);
        if (!SendBuffer::Write(acMessage, buffer, writer))
        {
            spdlog::error("TransportService: message {} did not fit in {} bytes; not sent", acMessage.GetOpcode(), buffer.GetSize());
            return false;
        }

        TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

        Client::Send(&packet, aFlags);

        return true;
    }

    return false;
}

bool TransportService::SendUnreliable(const ClientMessage& acMessage) const noexcept
{
    if (!IsConnected())
        return false;

    Buffer buffer(1 << 14);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Write first byte as packet needs it

    acMessage.Serialize(writer);
    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());

    Client::Send(&packet, TiltedPhoques::kUnreliable);
    return true;
}

void TransportService::OnConsume(const void* apData, uint32_t aSize)
{
    ServerMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from server");
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}

void TransportService::OnConnected()
{
    AuthenticationRequest request{};
    request.Version = BUILD_COMMIT;
    request.Protocol = BUILD_PROTOCOL;
    request.SKSEActive = IsScriptExtenderLoaded();
    request.MO2Active = GetModuleHandleW(kMO2DllName);

    request.Token = m_serverPassword;
    m_serverPassword = "";

    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    // null if discord is not active
    // TODO: think about user opt out
    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;
    auto* pNpc = Cast<TESNPC>(pPlayer->baseForm);
    if (pNpc)
    {
        request.Username = pNpc->fullName.value.AsAscii();
    }
    else
    {
        request.Username = "Some dragon boi";
    }

    auto* const cpModManager = ModManager::Get();

    for (auto* pMod : cpModManager->mods)
    {
        if (!pMod->IsLoaded())
            continue;

        auto& entry = request.UserMods.ModList.emplace_back();
        entry.Id = pMod->GetId();
        entry.IsLite = pMod->IsLite();
        entry.Filename = pMod->filename;
    }

    auto& modSystem = m_world.GetModSystem();
    if (pPlayer->GetWorldSpace())
        modSystem.GetServerModId(pPlayer->GetWorldSpace()->formID, request.WorldSpaceId);

    // A connect can complete after the player left for the main menu, where the player has no cell.
    if (pPlayer->parentCell)
        modSystem.GetServerModId(pPlayer->parentCell->formID, request.CellId);

    request.Level = pPlayer->GetLevel();

    request.HideFromPublicPage = IsHiddenFromPublicPage();
    if (request.HideFromPublicPage)
        spdlog::info("Hidden from the public server page (launcher setting)");

    auto* pGameTime = TimeData::Get();
    request.PlayerTime.TimeScale = pGameTime->TimeScale->f;
    request.PlayerTime.Time = pGameTime->GameHour->f;
    request.PlayerTime.Year = pGameTime->GameYear->f;
    request.PlayerTime.Month = pGameTime->GameMonth->f;
    request.PlayerTime.Day = pGameTime->GameDay->f;

    Send(request);
}

bool TransportService::Connect(const std::string& acEndpoint) noexcept
{
    // TiltedConnect's Connect overwrites the old connection's handle without closing it.
    // The networking library keeps the old one alive, so the server sees a player who is there but never sends anything.
    // Close() does nothing when there is no connection.
    Close();
    return Client::Connect(acEndpoint);
}

void TransportService::OnDisconnected(EDisconnectReason aReason)
{
    m_connected = false;

    spdlog::warn("Disconnected from server {}", aReason);

    m_dispatcher.trigger(DisconnectedEvent());
}

void TransportService::OnUpdate()
{
}

void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    PerfScope perfScope("TransportService::HandleUpdate");

    m_lastPollTime = static_cast<double>(SteamNetworkingUtils()->GetLocalTimestamp()) / 1000.0;
    Update();

    // Every 30 s: whether updates that come late are this connection's doing.
    static std::chrono::steady_clock::time_point s_nextQualityLog;
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow >= s_nextQualityLog && IsConnected())
    {
        s_nextQualityLog = cNow + std::chrono::seconds(30);
        const auto cStatus = GetConnectionStatus();
        spdlog::info("Connection: ping {} ms, quality {:.2f} here and {:.2f} at the other end, {:.0f} packets a second out, {:.0f} in, {} bytes waiting to be acknowledged, {} queued, "
                     "a message queued now waits {} us",
                     cStatus.m_nPing, cStatus.m_flConnectionQualityLocal, cStatus.m_flConnectionQualityRemote, cStatus.m_flOutPacketsPerSec, cStatus.m_flInPacketsPerSec,
                     cStatus.m_cbSentUnackedReliable, cStatus.m_cbPendingReliable, static_cast<int64_t>(cStatus.m_usecQueueTime));
    }
}

double TransportService::GetMessageReceiveTime() const noexcept
{
    return static_cast<double>(GetCurrentMessageReceiveTime()) / 1000.0;
}

void TransportService::HandleConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
}

void TransportService::HandleDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_localPlayerId = NULL;
}

void TransportService::HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept
{
    using AR = AuthenticationResponse::ResponseType;
    if (acMessage.Type == AR::kAccepted)
    {
        m_connected = true;
        // The rig (Tools/VR/headless.ps1) waits for this line.
        spdlog::info("Connected: the server accepted this player (id {})", acMessage.PlayerId);

        m_world.SetServerSettings(acMessage.Settings);

        m_dispatcher.trigger(acMessage.UserMods);
        m_dispatcher.trigger(acMessage.Settings);
        m_dispatcher.trigger(ConnectedEvent(acMessage.PlayerId));
        return; // quit the function here.
    }

    // error finding

    TiltedPhoques::String ErrorInfo;

    ErrorInfo = "{";

    switch (acMessage.Type)
    {
    case AR::kWrongVersion:
        ErrorInfo += "\"error\": \"wrong_version\", \"data\": {";
        ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT " (protocol " BUILD_PROTOCOL ")");
        ErrorInfo += "}";
        break;
    case AR::kModsMismatch:
    {
        ErrorInfo += "\"error\": \"mods_mismatch\", \"data\": {\"mods\": [";
        bool first = true;
        for (const auto& m : acMessage.UserMods.ModList)
        {
            if (!first)
                ErrorInfo += ",";
            ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
            first = false;
        }
        ErrorInfo += "]}";
        break;
    }
    case AR::kClientModsDisallowed:
    {
        ErrorInfo += "\"error\": \"client_mods_disallowed\", \"data\": { \"mods\": [";
        if (acMessage.SKSEActive)
            ErrorInfo += "\"SKSE\"";
        if (acMessage.MO2Active)
            if (acMessage.SKSEActive)
                ErrorInfo += ",";
        ErrorInfo += "\"MO2\"";
        ErrorInfo += "]}";
        break;
    }
    case AR::kWrongPassword:
    {
        ErrorInfo += "\"error\": \"wrong_password\"";
        break;
    }
    case AR::kServerFull:
    {
        ErrorInfo += "\"error\": \"server_full\"";
        break;
    }
    default: ErrorInfo += "\"error\": \"no_reason\""; break;
    }

    ErrorInfo += "}";

    ConnectionErrorEvent errorEvent;
    if (!ErrorInfo.empty())
    {
        spdlog::error(ErrorInfo.c_str());
        errorEvent.ErrorDetail = std::move(ErrorInfo);
    }

    m_dispatcher.trigger(errorEvent);
}

void TransportService::HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept
{
    m_world.SetServerSettings(acMessage.Settings);
    m_dispatcher.trigger(acMessage.Settings);
}
