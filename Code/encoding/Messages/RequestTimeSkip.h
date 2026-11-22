#pragma once

#include "Message.h"

/**
 * One player's step in the shared time skip vote; when the vote changes, the server tells everyone with NotifyTimeSkip.
 * Only the hours travel: each game keeps its own player's choice of sleeping or waiting.
 */
struct RequestTimeSkip final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestTimeSkip;

    /** What the player did in the Sleep/Wait menu, or what their game did after it. */
    enum class Step : uint8_t
    {
        kConfirm,        // the player pressed OK for Hours
        kWithdraw,       // the menu closed without a skip, by the player or by the game
        kWithdrawCombat, // the player entered combat while confirmed
        kAwake,          // the game's own sleep or wait has finished
        kDropCrossed,    // the game no longer counts a confirmation it sent as a vote ended or a refusal came
    };

    /**
     * Make an empty step.
     */
    RequestTimeSkip()
        : ClientMessage(Opcode)
    {
    }

    /**
     * Write the step and the hours.
     *
     * @param aWriter Where the message goes.
     */
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;

    /**
     * Read the step and the hours.
     *
     * @param aReader Where the message comes from.
     */
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    /**
     * Compare two steps field by field.
     *
     * @param acRhs The other step.
     * @return True when both say the same thing.
     */
    bool operator==(const RequestTimeSkip& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SkipStep == acRhs.SkipStep && Hours == acRhs.Hours; }

    Step SkipStep{};
    float Hours{}; // the length the player picked, in game hours; read with kConfirm only
};
