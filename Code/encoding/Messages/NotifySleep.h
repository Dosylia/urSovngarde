#pragma once

#include "Message.h"

//! Sleep permission, party-only provisional progress, and a final duration committed for every loaded player.
struct NotifySleep final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifySleep;

    enum class Event : uint8_t
    {
        kGranted,          // to the sleeper: let the clock go
        kRefusedDisabled,  // to the sleeper: bEnableSleep is off
        kRefusedNotAllowed, // to the sleeper: not in a party with others online
        kRefusedBusy,      // to the sleeper: PlayerId is already sleeping
        kRefusedCombat,    // to the sleeper: PlayerId is in combat
        kProgress,         // to the party: PlayerId has slept HoursSlept so far
        kFinished,         // to everyone loaded: commit HoursSlept before the time resync; zero cancels, sent only to a party that saw progress
        kRevoked,          // to the sleeper: stop using the game clock; an authoritative resync follows
        kRefusedPublic,    // to the sleeper: bAnnounceServer or bPublicStatus is on, so the server is public
    };

    NotifySleep()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySleep& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SleepEvent == acRhs.SleepEvent && PlayerId == acRhs.PlayerId && HoursSlept == acRhs.HoursSlept;
    }

    Event SleepEvent{};
    uint32_t PlayerId{};
    float HoursSlept{};
};
