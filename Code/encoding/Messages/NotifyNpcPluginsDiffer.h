#pragma once

#include "Message.h"

// Another player's game does not have the same plugins adding or changing NPCs (see RequestNpcPlugins).
struct NotifyNpcPluginsDiffer final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyNpcPluginsDiffer;

    NotifyNpcPluginsDiffer()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyNpcPluginsDiffer() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyNpcPluginsDiffer& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && Username == achRhs.Username && OnlyHere == achRhs.OnlyHere && OnlyThere == achRhs.OnlyThere;
    }

    String Username{};
    // In this game and not in theirs.
    TiltedPhoques::Vector<String> OnlyHere{};
    // In theirs and not in this game.
    TiltedPhoques::Vector<String> OnlyThere{};
};
