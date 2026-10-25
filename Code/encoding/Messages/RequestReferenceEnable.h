#pragma once

#include "Message.h"

#include <Structs/ReferenceEnableState.h>

//! References the party leader's scripts switched on or off since the last send, in the order it happened.
struct RequestReferenceEnable final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestReferenceEnable;

    RequestReferenceEnable()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { ReferenceEnableStates::Write(aWriter, Changes); }

    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override
    {
        ClientMessage::DeserializeRaw(aReader);
        ReferenceEnableStates::Read(aReader, Changes);
    }

    bool operator==(const RequestReferenceEnable& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Changes == acRhs.Changes; }

    TiltedPhoques::Vector<ReferenceEnableState> Changes{};
};
