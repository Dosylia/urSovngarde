#pragma once

#include "Message.h"

// A player's game saying that an actor became, or stopped being, that player's follower.
struct RequestFollowerState final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestFollowerState;

    RequestFollowerState()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestFollowerState() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestFollowerState& achRhs) const noexcept { return ServerId == achRhs.ServerId && IsFollower == achRhs.IsFollower && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t ServerId{};
    bool IsFollower{};
};
