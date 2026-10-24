#pragma once

#include "Message.h"

#include <Structs/ActionEvent.h>

//! Animation actions of other players' actors, relayed reliably as soon as their owner's RequestActionEvents arrives.
struct NotifyActionEvents final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActionEvents;

    NotifyActionEvents()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActionEvents& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Actions == acRhs.Actions; }

    //! Server id to that actor's actions, in the order they were performed.
    TiltedPhoques::Map<uint32_t, TiltedPhoques::Vector<ActionEvent>> Actions{};
};
