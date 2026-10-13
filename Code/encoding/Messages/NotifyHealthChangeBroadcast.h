#pragma once

#include "Message.h"

struct NotifyHealthChangeBroadcast final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyHealthChangeBroadcast;

    NotifyHealthChangeBroadcast()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyHealthChangeBroadcast& acRhs) const noexcept
    {
        return Id == acRhs.Id && DeltaHealth == acRhs.DeltaHealth && AttackerPlayerId == acRhs.AttackerPlayerId && Tick == acRhs.Tick && GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t Id;
    float DeltaHealth;
    //! The attacking player, filled in by the server; the actor's owner adds them to its combat.
    uint32_t AttackerPlayerId{};
    //! When it happened in the attacker's game, on the server's clock; 0 when not known (RequestHealthChangeBroadcast).
    uint64_t Tick{};
};
