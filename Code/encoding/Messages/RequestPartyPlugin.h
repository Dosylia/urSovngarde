#pragma once

#include "Message.h"

// A player's game asking whether everyone in its party has a plugin, by its file name.
struct RequestPartyPlugin final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPartyPlugin;

    RequestPartyPlugin()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestPartyPlugin() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPartyPlugin& achRhs) const noexcept { return PluginFile == achRhs.PluginFile; }

    String PluginFile{};
};
