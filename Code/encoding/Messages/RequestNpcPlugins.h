#pragma once

#include "Message.h"

// The plugins of this game that add or change NPCs or levelled NPC lists, sent once connected.
// Two games without the same ones roll different creatures and people in the same places.
struct RequestNpcPlugins final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestNpcPlugins;

    RequestNpcPlugins()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestNpcPlugins() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestNpcPlugins& achRhs) const noexcept { return GetOpcode() == achRhs.GetOpcode() && Plugins == achRhs.Plugins; }

    TiltedPhoques::Vector<String> Plugins{};
};
