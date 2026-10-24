#pragma once

#include "Message.h"

#include <Structs/ActionEvent.h>

//! Animation actions of the actors the sender owns, sent reliably on their own.
//! Movement goes unreliably, so an action is never lost with a movement message.
struct RequestActionEvents final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActionEvents;

    RequestActionEvents()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestActionEvents& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Actions == acRhs.Actions; }

    //! Server id to that actor's actions, in the order they were performed.
    TiltedPhoques::Map<uint32_t, TiltedPhoques::Vector<ActionEvent>> Actions{};
};
