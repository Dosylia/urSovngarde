#pragma once

#include "Message.h"

#include <Structs/WarState.h>

//! The civil war in the party leader's game, for its members to follow.
struct NotifyWarState final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyWarState;

    NotifyWarState()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { State.Write(aWriter); }

    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override
    {
        ServerMessage::DeserializeRaw(aReader);
        State.Read(aReader);
    }

    bool operator==(const NotifyWarState& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && State == acRhs.State; }

    WarState State{};
};
