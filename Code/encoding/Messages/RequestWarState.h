#pragma once

#include "Message.h"

#include <Structs/WarState.h>

//! The civil war in the party leader's game: when it changes, and every so often for whoever fell behind.
struct RequestWarState final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestWarState;

    RequestWarState()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { State.Write(aWriter); }

    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override
    {
        ClientMessage::DeserializeRaw(aReader);
        State.Read(aReader);
    }

    bool operator==(const RequestWarState& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && State == acRhs.State; }

    WarState State{};
};
