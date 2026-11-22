#pragma once

#include "Message.h"

/**
 * A plugin's move of the shared clock forward to the next time it is an hour, asked once; the server answers with NotifyHourMove.
 * The server counts the hours on its own clock and moves every game's clock, as a wait does.
 */
struct RequestHourMove final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestHourMove;

    /**
     * Make an empty ask.
     */
    RequestHourMove()
        : ClientMessage(Opcode)
    {
    }

    /**
     * Write the hour asked for.
     *
     * @param aWriter Where the message goes.
     */
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;

    /**
     * Read the hour asked for.
     *
     * @param aReader Where the message comes from.
     */
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    /**
     * Compare two asks.
     *
     * @param acRhs The other ask.
     * @return True when both ask for the same hour.
     */
    bool operator==(const RequestHourMove& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && TargetHour == acRhs.TargetHour; }

    float TargetHour{}; // the hour to move to, from 0 to 24
};
