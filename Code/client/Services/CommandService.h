#pragma once

struct World;
struct TransportService;
struct SetTimeCommandEvent;
struct TeleportCommandResponse;

/**
 * @brief Responsible for processing, sending and receiving chat commands.
 *
 * Parses and sends appropriate commands to the server,
 * and receives and processes command results processed by the server.
 */
struct CommandService
{
public:
    CommandService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher);
    ~CommandService() noexcept = default;

    TP_NOCOPYMOVE(CommandService);

protected:
    void OnSetTimeCommand(const SetTimeCommandEvent&) const noexcept;
    /**
     * Move the player to the player the chat's teleport command named, where the server says they are.
     *
     * @param acMessage The server's answer: the cell, the world space and the position.
     */
    void OnTeleportCommandResponse(const TeleportCommandResponse& acMessage) noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_setTimeConnection;
    entt::scoped_connection m_teleportConnection;
};
