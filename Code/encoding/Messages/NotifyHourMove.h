#pragma once

#include "Message.h"

/**
 * The answer to a plugin's move of the shared clock: the move itself, to every game in the world, or why it was refused, to the player who asked.
 */
struct NotifyHourMove final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyHourMove;

    /** What came of the ask. */
    enum class Event : uint8_t
    {
        kMoved,           // to every game in the world: PlayerId moved the shared clock forward Hours; the server's time follows
        kAlreadyThere,    // to the asker: it is that hour already, or just past it; nothing moved
        kRefusedDisabled, // to the asker: Gameplay:bEnableSleep is off
        kRefusedPublic,   // to the asker: a public server
        kRefusedCombat,   // to the asker: PlayerId, of the asker's party, fights
        kRefusedBusy,     // to the asker: a shared sleep vote is open, or a game still rests one
        kRefusedNoParty,  // to the asker: in no party while others are online
    };

    /**
     * Make an empty answer.
     */
    NotifyHourMove()
        : ServerMessage(Opcode)
    {
    }

    /**
     * Write the event, the player and the hours.
     *
     * @param aWriter Where the message goes.
     */
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;

    /**
     * Read the event, the player and the hours.
     *
     * @param aReader Where the message comes from.
     */
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    /**
     * Compare two answers field by field.
     *
     * @param acRhs The other answer.
     * @return True when both say the same thing.
     */
    bool operator==(const NotifyHourMove& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && MoveEvent == acRhs.MoveEvent && PlayerId == acRhs.PlayerId && Hours == acRhs.Hours;
    }

    Event MoveEvent{};
    uint32_t PlayerId{}; // who moved the clock, or who fights
    float Hours{};       // how far the shared clock moved, in game hours; read with kMoved
};
