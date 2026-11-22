#pragma once

#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"
#include "Services/ServerPassword.h"

#include <atomic>
#include <Client.hpp>

struct ImguiService;
struct UpdateEvent;
struct ClientMessage;
struct AuthenticationResponse;
struct NotifySettingsChange;

struct World;

using TiltedPhoques::Client;

/**
 * @brief Handles communication with the server.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept = default;

    TP_NOCOPYMOVE(TransportService);

    using Client::Connect;
    // Closes any existing connection first, so it does not linger on the server.
    bool Connect(const std::string& acEndpoint) noexcept;

    bool Send(const ClientMessage& acMessage, TiltedPhoques::EPacketFlags aFlags = TiltedPhoques::kReliable) const noexcept;
    // For voice: a lost packet is a short gap, never a delay behind a resend.
    bool SendUnreliable(const ClientMessage& acMessage) const noexcept;

    void OnConsume(const void* apData, uint32_t aSize) override;

    /**
     * Send the authentication request once the connection to the server is up: version, protocol, player, mods, place, time and the server password.
     * The password's text goes out once and is forgotten then; whether there was one stays known for this connection.
     */
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    //! When the message being handled arrived, in ms on the local network clock; only meaningful while handling one.
    [[nodiscard]] double GetMessageReceiveTime() const noexcept;
    //! The local network clock in ms when this frame's messages were polled: "now" for remote playback.
    [[nodiscard]] double GetLastPollTime() const noexcept { return m_lastPollTime; }

    /**
     * Keep the password for the next connection to a server.
     *
     * @param acPassword The password; empty when the server has none.
     */
    void SetServerPassword(const std::string& acPassword) noexcept { m_serverPassword.Set(acPassword); }

    /**
     * Get the server last connected to.
     *
     * @return Its address as "host:port".
     */
    [[nodiscard]] const std::string& GetServerEndpoint() const noexcept { return m_serverEndpoint; }

    /**
     * Check whether the server this game connects to, or is on, was given a password.
     * It stays known after the password went out at authentication.
     *
     * @return True when the last password set was not empty.
     */
    [[nodiscard]] bool HasServerPassword() const noexcept { return m_serverPassword.IsGiven(); }

    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;
    void HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    bool m_connected;
    double m_lastPollTime{};
    ServerPassword m_serverPassword{};
    std::string m_serverEndpoint{};
    uint32_t m_localPlayerId;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_sendServerMessageConnection;
    entt::scoped_connection m_settingsChangeConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    std::function<void(UniquePtr<ServerMessage>&)> m_messageHandlers[kServerOpcodeMax];
};
