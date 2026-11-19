#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct NotifyPlayerList final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerList;

    NotifyPlayerList()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPlayerList() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerList& acRhs) const noexcept { return Players == acRhs.Players && FlatPlayers == acRhs.FlatPlayers && GetOpcode() == acRhs.GetOpcode(); }

    TiltedPhoques::Map<uint32_t, String> Players{};
    //! Those of the players above who play flat Skyrim; the others play Skyrim VR.
    TiltedPhoques::Vector<uint32_t> FlatPlayers{};
};
