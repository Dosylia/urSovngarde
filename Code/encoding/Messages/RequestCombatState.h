#pragma once

#include "Message.h"

/**
 * This player entered or left combat, reported to a server that can move the shared clock for a plugin, which refuses while a party member fights.
 */
struct RequestCombatState final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestCombatState;

    /**
     * Make an empty report.
     */
    RequestCombatState()
        : ClientMessage(Opcode)
    {
    }

    /**
     * Write whether this player fights.
     *
     * @param aWriter Where the message goes.
     */
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;

    /**
     * Read whether this player fights.
     *
     * @param aReader Where the message comes from.
     */
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    /**
     * Compare two reports.
     *
     * @param acRhs The other report.
     * @return True when both say the same.
     */
    bool operator==(const RequestCombatState& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && InCombat == acRhs.InCombat; }

    bool InCombat{};
};
