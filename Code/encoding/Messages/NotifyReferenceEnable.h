#pragma once

#include "Message.h"

#include <Structs/ReferenceEnableState.h>

//! References the party leader's scripts switched on or off: as they happen, and the party's whole list on joining it.
struct NotifyReferenceEnable final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyReferenceEnable;

    NotifyReferenceEnable()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { ReferenceEnableStates::Write(aWriter, Changes); }

    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override
    {
        ServerMessage::DeserializeRaw(aReader);
        ReferenceEnableStates::Read(aReader, Changes);
    }

    bool operator==(const NotifyReferenceEnable& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Changes == acRhs.Changes; }

    TiltedPhoques::Vector<ReferenceEnableState> Changes{};
};
