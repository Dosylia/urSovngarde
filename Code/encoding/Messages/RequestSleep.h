#pragma once

#include "Message.h"

//! One step of a sleep or wait that moves the shared clock; the server answers or relays it as NotifySleep.
//! Hours, not a date: with bSyncPlayerCalendar off every player keeps their own date, so only the length is shared.
struct RequestSleep final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestSleep;

    enum class Step : uint8_t
    {
        kAsk,      // the Sleep/Wait menu opened
        kProgress, // the clock has moved this far so far
        kFinish,   // done, move everybody by HoursSlept
        kCancel,   // the menu closed without the clock moving
        kCombat,   // not a sleep: this player entered (InCombat) or left combat
    };

    RequestSleep()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestSleep& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && SleepStep == acRhs.SleepStep && HoursSlept == acRhs.HoursSlept && InCombat == acRhs.InCombat;
    }

    Step SleepStep{};
    float HoursSlept{};
    bool InCombat{};
};
