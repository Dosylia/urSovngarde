#pragma once

#include "Message.h"

using TiltedPhoques::String;

/**
 * Where the shared time skip vote stands, sent to every connected player on each change.
 * Time moves only when every connected player has confirmed; the shortest confirmed length wins.
 */
struct NotifyTimeSkip final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyTimeSkip;

    /** What happened to the vote. */
    enum class Event : uint8_t
    {
        kVoteChanged, // a player confirmed, withdrew or left; the counts and names are current
        kAgreed,      // every connected player confirmed: skip Hours now; a time resync follows
        kTimedOut,    // the vote ran out: the menus are released, the names say who did not confirm
        kDisabled,    // to the voter alone: Gameplay:bEnableSleep is off, so the time is not shared
        kTooSoon,     // to the voter alone: they withdrew, or a vote they were in ran out, lately; WaitSeconds until they may confirm again
    };

    /** What one player did to the vote, named by ChangedPlayerName. */
    enum class Change : uint8_t
    {
        kNone,           // no single player's doing, as with kAgreed and kTimedOut
        kConfirmed,      // pressed OK in the Sleep/Wait menu
        kWithdrew,       // closed the menu, or their game closed it
        kWithdrewCombat, // entered combat
        kLeft,           // disconnected
    };

    /**
     * Say what a player did to the vote, in words for the logs.
     *
     * @param aChange What the player did.
     * @return A few words; "none" for no single player's doing.
     */
    static const char* DescribeChange(Change aChange) noexcept;

    /**
     * Make an empty notice.
     */
    NotifyTimeSkip()
        : ServerMessage(Opcode)
    {
    }

    /**
     * Write the event, the change, the hours, the counts, the missing names and the wait.
     *
     * @param aWriter Where the message goes.
     */
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;

    /**
     * Read the event, the change, the hours, the counts, the missing names and the wait.
     *
     * @param aReader Where the message comes from.
     */
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    /**
     * Compare two notices field by field.
     *
     * @param acRhs The other notice.
     * @return True when both say the same thing.
     */
    bool operator==(const NotifyTimeSkip& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SkipEvent == acRhs.SkipEvent && PlayerChange == acRhs.PlayerChange && ChangedPlayerName == acRhs.ChangedPlayerName &&
               Hours == acRhs.Hours && ReadyCount == acRhs.ReadyCount && RequiredCount == acRhs.RequiredCount && MissingPlayerNames == acRhs.MissingPlayerNames &&
               WaitSeconds == acRhs.WaitSeconds;
    }

    Event SkipEvent{};
    Change PlayerChange{};
    String ChangedPlayerName{}; // empty with kNone
    float Hours{};              // the shortest confirmed length, in game hours; 0 while nobody is confirmed
    uint32_t ReadyCount{};      // connected players who confirmed
    uint32_t RequiredCount{};   // connected players, loading ones and those in the main menu included
    TiltedPhoques::Vector<String> MissingPlayerNames{}; // connected players who have not confirmed
    uint32_t WaitSeconds{};     // with kTooSoon: how long until the voter may confirm again
};
