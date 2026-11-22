#include <Messages/NotifyTimeSkip.h>

#include <Messages/ByteEnum.h>
#include <TiltedCore/Serialization.hpp>

namespace
{
/** Most names a damaged count can make the reader take. */
constexpr uint64_t kNameCountMask = 0xFFFF;
} // namespace

/**
 * Say what a player did to the vote, in words for the logs.
 *
 * @param aChange What the player did.
 * @return A few words; "none" for no single player's doing.
 */
const char* NotifyTimeSkip::DescribeChange(const Change aChange) noexcept
{
    switch (aChange)
    {
    case Change::kConfirmed: return "confirmed";
    case Change::kWithdrew: return "withdrew, menu closed";
    case Change::kWithdrewCombat: return "withdrew, combat";
    case Change::kLeft: return "left";
    default: return "none";
    }
}

/**
 * Write the event, the change, the hours, the counts, the missing names and the wait.
 *
 * @param aWriter Where the message goes.
 */
void NotifyTimeSkip::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    ByteEnum::Write(aWriter, SkipEvent);
    ByteEnum::Write(aWriter, PlayerChange);
    Serialization::WriteString(aWriter, ChangedPlayerName);
    Serialization::WriteFloat(aWriter, Hours);
    Serialization::WriteVarInt(aWriter, ReadyCount);
    Serialization::WriteVarInt(aWriter, RequiredCount);

    Serialization::WriteVarInt(aWriter, MissingPlayerNames.size());
    for (const String& cName : MissingPlayerNames)
        Serialization::WriteString(aWriter, cName);

    Serialization::WriteVarInt(aWriter, WaitSeconds);
}

/**
 * Read the event, the change, the hours, the counts, the missing names and the wait.
 *
 * @param aReader Where the message comes from.
 */
void NotifyTimeSkip::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    SkipEvent = ByteEnum::Read<Event>(aReader);
    PlayerChange = ByteEnum::Read<Change>(aReader);
    ChangedPlayerName = Serialization::ReadString(aReader);
    Hours = Serialization::ReadFloat(aReader);
    ReadyCount = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    RequiredCount = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));

    // No more missing players than players: a damaged count cannot make it read on and on.
    const auto cNameCount = Serialization::ReadVarInt(aReader) & kNameCountMask;
    for (uint64_t i = 0; i < cNameCount && i < RequiredCount; ++i)
        MissingPlayerNames.push_back(Serialization::ReadString(aReader));

    WaitSeconds = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
}
