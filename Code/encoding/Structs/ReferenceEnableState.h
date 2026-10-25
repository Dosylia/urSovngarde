#pragma once

#include <Structs/GameId.h>
#include <DecodeGuard.h>

#include <TiltedCore/Serialization.hpp>

//! A placed reference switched on or off by a script in the party leader's game.
struct ReferenceEnableState
{
    GameId Id{};
    bool Enabled{};

    bool operator==(const ReferenceEnableState& acRhs) const noexcept { return Id == acRhs.Id && Enabled == acRhs.Enabled; }
};

namespace ReferenceEnableStates
{
//! A party's whole list goes to a player who joins it, so the cap is far above one battle's worth of switches.
constexpr uint64_t kMax = 16384;

inline void Write(TiltedPhoques::Buffer::Writer& aWriter, const TiltedPhoques::Vector<ReferenceEnableState>& acStates) noexcept
{
    Serialization::WriteVarInt(aWriter, acStates.size());
    for (const auto& state : acStates)
    {
        state.Id.Serialize(aWriter);
        Serialization::WriteBool(aWriter, state.Enabled);
    }
}

inline void Read(TiltedPhoques::Buffer::Reader& aReader, TiltedPhoques::Vector<ReferenceEnableState>& aStates) noexcept
{
    const auto cCount = Serialization::ReadVarInt(aReader);
    if (!DecodeGuard::Within(cCount, kMax))
        return;
    aStates.resize(static_cast<size_t>(cCount));
    for (auto& state : aStates)
    {
        state.Id.Deserialize(aReader);
        state.Enabled = Serialization::ReadBool(aReader);
    }
}
} // namespace ReferenceEnableStates
