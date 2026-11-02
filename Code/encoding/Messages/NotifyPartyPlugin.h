#pragma once

#include "Message.h"

// The server's answer to RequestPartyPlugin.
struct NotifyPartyPlugin final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyPlugin;

    NotifyPartyPlugin()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPartyPlugin() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyPlugin& achRhs) const noexcept { return PluginFile == achRhs.PluginFile && PlayerWithout == achRhs.PlayerWithout; }

    String PluginFile{};
    // Empty when every player of the party has it.
    String PlayerWithout{};
};
