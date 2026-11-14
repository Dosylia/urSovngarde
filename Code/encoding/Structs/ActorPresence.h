#pragma once

#include <DecodeGuard.h>

#include <TiltedCore/Serialization.hpp>

//! How a game has a character loaded.
enum class ActorPresenceKind : uint8_t
{
    Own,     //!< this game runs it
    Copy,    //!< a copy of its owner's
    StandIn, //!< a copy standing in for this game's own reference, which rolled as another kind
};

//! A character with a server id that a game has loaded, with its 3D.
struct ActorPresence
{
    uint32_t ServerId{};
    ActorPresenceKind Kind{ActorPresenceKind::Own};
    bool Dead{};
    //! Whom the character fights here, by server id; 0 when it fights nobody or is not this game's own.
    uint32_t CombatTarget{};

    bool operator==(const ActorPresence& acRhs) const noexcept
    {
        return ServerId == acRhs.ServerId && Kind == acRhs.Kind && Dead == acRhs.Dead && CombatTarget == acRhs.CombatTarget;
    }
};

namespace ActorPresences
{
//! Far above what one game's loaded cells hold.
constexpr uint64_t kMax = 4096;

inline void Write(TiltedPhoques::Buffer::Writer& aWriter, const TiltedPhoques::Vector<ActorPresence>& acActors) noexcept
{
    Serialization::WriteVarInt(aWriter, acActors.size());
    for (const auto& actor : acActors)
    {
        Serialization::WriteVarInt(aWriter, actor.ServerId);
        aWriter.WriteBits(static_cast<uint8_t>(actor.Kind), 2);
        Serialization::WriteBool(aWriter, actor.Dead);
        Serialization::WriteVarInt(aWriter, actor.CombatTarget);
    }
}

inline void Read(TiltedPhoques::Buffer::Reader& aReader, TiltedPhoques::Vector<ActorPresence>& aActors) noexcept
{
    const auto cCount = Serialization::ReadVarInt(aReader);
    if (!DecodeGuard::Within(cCount, kMax))
        return;
    aActors.resize(static_cast<size_t>(cCount));
    for (auto& actor : aActors)
    {
        actor.ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        uint64_t kind{};
        aReader.ReadBits(kind, 2);
        actor.Kind = static_cast<ActorPresenceKind>(kind);
        actor.Dead = Serialization::ReadBool(aReader);
        actor.CombatTarget = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
}
} // namespace ActorPresences
