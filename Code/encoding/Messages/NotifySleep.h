#pragma once

#include "Message.h"

//! The server's side of a sleep: an answer to the sleeper's ask, or progress relayed to the sleeper's party.
struct NotifySleep final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifySleep;

    enum class Event : uint8_t
    {
        kGranted,          // to the sleeper: let the clock go
        kRefusedDisabled,  // to the sleeper: bEnableSleep is off
        kRefusedNotAllowed, // to the sleeper: public server, or not in a party with others online
        kRefusedBusy,      // to the sleeper: PlayerId is already sleeping
        kRefusedCombat,    // to the sleeper: PlayerId is in combat
        kProgress,         // to the party: PlayerId has slept HoursSlept so far
        kFinished,         // to the party: PlayerId woke up after HoursSlept
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
