#pragma once

#include "Message.h"

// The party leader's game setting a number for every game of its party, by a plugin's own key.
struct RequestPartyValue final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPartyValue;
    // The game and the server both refuse a longer key, or a key beyond the most a party holds.
    static constexpr size_t kLongestKey = 63;
    static constexpr size_t kMostKeys = 32;

    RequestPartyValue()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestPartyValue() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPartyValue& achRhs) const noexcept { return Key == achRhs.Key && Value == achRhs.Value; }

    String Key{};
    float Value{};
};
