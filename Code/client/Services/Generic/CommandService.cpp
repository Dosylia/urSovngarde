
#include <Services/CommandService.h>
#include <Services/TransportService.h>

#include <World.h>

#include <Events/SetTimeCommandEvent.h>

#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include "Messages/SetTimeCommandRequest.h"

#include <Services/PlayerTeleport.h>

CommandService::CommandService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_setTimeConnection = aDispatcher.sink<SetTimeCommandEvent>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<TeleportCommandResponse>().connect<&CommandService::OnTeleportCommandResponse>(this);
}

void CommandService::OnSetTimeCommand(const SetTimeCommandEvent& acEvent) const noexcept
{
    SetTimeCommandRequest request{};
    request.Hours = acEvent.Hours;
    request.Minutes = acEvent.Minutes;
    request.PlayerId = acEvent.PlayerId;
    m_transport.Send(request);
}

/**
 * Move the player to the player the chat's teleport command named, where the server says they are.
 *
 * @param acMessage The server's answer: the cell, the world space and the position.
 */
void CommandService::OnTeleportCommandResponse(const TeleportCommandResponse& acMessage) noexcept
{
    spdlog::info("TeleportCommandResponse: {:X}:{:X} at position {}", acMessage.CellId.ModId, acMessage.CellId.BaseId, acMessage.Position);
    PlayerTeleport::MoveTo(m_world, acMessage.CellId, acMessage.WorldSpaceId, acMessage.Position);
}
