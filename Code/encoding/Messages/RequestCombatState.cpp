#include <Messages/RequestCombatState.h>

#include <TiltedCore/Serialization.hpp>

/**
 * Write whether this player fights.
 *
 * @param aWriter Where the message goes.
 */
void RequestCombatState::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, InCombat);
}

/**
 * Read whether this player fights.
 *
 * @param aReader Where the message comes from.
 */
void RequestCombatState::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    InCombat = Serialization::ReadBool(aReader);
}
