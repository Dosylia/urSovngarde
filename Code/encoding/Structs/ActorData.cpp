#include <Structs/ActorData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

void ActorData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    InitialActorValues.Serialize(aWriter);
    InitialInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDead);
    Serialization::WriteBool(aWriter, IsWeaponDrawn);
    Serialization::WriteBool(aWriter, RevivedSinceDeath);
}

void ActorData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    InitialActorValues.Deserialize(aReader);
    InitialInventory.Deserialize(aReader);
    IsDead = Serialization::ReadBool(aReader);
    IsWeaponDrawn = Serialization::ReadBool(aReader);
    RevivedSinceDeath = Serialization::ReadBool(aReader);
}
