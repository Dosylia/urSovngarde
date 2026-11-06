#pragma once

#include "Message.h"

#include <Structs/ActorPresence.h>

//! Every character this game has loaded, sent every few seconds, so the server knows which games can see each one.
struct RequestActorPresence final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActorPresence;

    RequestActorPresence()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override { ActorPresences::Write(aWriter, Actors); }

    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override
    {
        ClientMessage::DeserializeRaw(aReader);
        ActorPresences::Read(aReader, Actors);
    }

    bool operator==(const RequestActorPresence& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Actors == acRhs.Actors; }

    TiltedPhoques::Vector<ActorPresence> Actors{};
};
