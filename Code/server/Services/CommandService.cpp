#include <Services/CommandService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/SetTimeCommandRequest.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>

#include <Setting.h>

extern Console::Setting<bool> bPublicStatus;

namespace
{
Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", "Whether to list the server on the public server list", false};

// A server strangers join: listed upstream, or on the urSovngarde website's public server page.
bool IsPublicServer() noexcept
{
    return bAnnounceServer || bPublicStatus;
}

bool IsAdmin(const Player* apPlayer) noexcept
{
    for (const auto cSession : GameServer::Get()->GetAdminSessions())
        if (const Player* pAdmin = PlayerManager::Get()->GetByConnectionId(cSession); pAdmin && pAdmin == apPlayer)
            return true;
    return false;
}
}

CommandService::CommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_setTimeConnection = aDispatcher.sink<PacketEvent<SetTimeCommandRequest>>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportCommandRequest>>().connect<&CommandService::OnTeleportCommandRequest>(this);
}

void CommandService::OnSetTimeCommand(const PacketEvent<SetTimeCommandRequest>& acMessage) const noexcept
{
    NotifySetTimeResult response{};

    const auto cPlayerId = static_cast<uint32_t>(acMessage.Packet.PlayerId);

    // Admin override: always allowed
    for (const auto session : GameServer::Get()->GetAdminSessions())
    {
        if (PlayerManager::Get()->GetByConnectionId(session)->GetId() == cPlayerId)
        {
            const auto cHours = static_cast<int>(acMessage.Packet.Hours);
            const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

            m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale());

            response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
            acMessage.pPlayer->Send(response);

            return;
        }
    }

    // Party leader allowed on private servers only
    const auto* pPartyService = &m_world.GetPartyService();
    if (pPartyService->IsPlayerLeader(acMessage.pPlayer) && !bAnnounceServer)
    {
        const auto cHours = static_cast<int>(acMessage.Packet.Hours);
        const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

        m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale());

        response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
        acMessage.pPlayer->Send(response);

        return;
    }

    response.Result = NotifySetTimeResult::SetTimeResult::kNoPermission;
    acMessage.pPlayer->Send(response);
}

void CommandService::OnTeleportCommandRequest(const PacketEvent<TeleportCommandRequest>& acMessage) const noexcept
{
    Player* pTargetPlayer = nullptr;
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        if (pPlayer->GetUsername() == acMessage.Packet.TargetPlayer)
            pTargetPlayer = pPlayer;
    }

    TeleportCommandResponse response{};

    // On a public server, going to a player is for admins and for that player's party only (Emma, 2026-10-11: only
    // admins use commands there). Anyone else's request was answered with the player's position, so a stranger could
    // land on anyone by typing their name. Refused, the answer carries no cell and the game says the teleport failed.
    if (pTargetPlayer && IsPublicServer() && !IsAdmin(acMessage.pPlayer))
    {
        auto& partyService = m_world.GetPartyService();
        const auto* pParty = partyService.GetPlayerParty(acMessage.pPlayer);
        const bool cSameParty = pParty && std::find(pParty->Members.begin(), pParty->Members.end(), pTargetPlayer) != pParty->Members.end();
        if (!cSameParty)
        {
            spdlog::info("Teleport of player {} to '{}' refused: a public server, and neither an admin nor in that player's party", acMessage.pPlayer->GetId(),
                         acMessage.Packet.TargetPlayer.c_str());
            pTargetPlayer = nullptr;
        }
    }

    if (pTargetPlayer)
    {
        auto character = pTargetPlayer->GetCharacter();
        if (character)
        {
            const auto* pMovementComponent = m_world.try_get<MovementComponent>(*character);
            if (pMovementComponent)
            {
                const auto& cellComponent = pTargetPlayer->GetCellComponent();
                response.CellId = cellComponent.Cell;
                response.Position = pMovementComponent->Position;
                response.WorldSpaceId = cellComponent.WorldSpaceId;
            }
        }
    }

    acMessage.pPlayer->Send(response);
}
