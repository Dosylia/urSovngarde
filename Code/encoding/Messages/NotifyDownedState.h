#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/ReviveRefusal.h>
#include <Structs/Vector3_NetQuantize.h>

//! A player's downed state changed; sent to that player and their party (kReviveRefused: to the reviver only).
struct NotifyDownedState final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDownedState;

    enum State : uint8_t
    {
        kDown,          //!< Down, waiting for a revive for SecondsLeft.
        kRevived,       //!< Revived by ReviverId; the player's own game stands it up.
        kRespawned,     //!< Respawned on its own game's word, without the server ending the wait (no word from the server, or a bleedout that ended by itself).
        kPartyFallen,   //!< Nobody in the party is left standing; each game respawns its player now, at the respawn point.
        kDied,          //!< Not revived in time, or down alone; the player's game respawns it now, at the respawn point.
        kStoodUp,       //!< Up again without a revive or a respawn: a save was loaded while down.
        kReviveRefused, //!< ReviverId's revive of PlayerId was refused, for Refusal.
        kLeft,          //!< Left the party or the server while down: sent to the members told it was down, who can no longer revive it.
        kReviveHeld,    //!< To the downed player only: ReviverId has a hand on them and is reviving them.
        kReviveLetGo,   //!< To the downed player only: ReviverId's hand came off before the revive.
    };

    NotifyDownedState()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDownedState& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Change == acRhs.Change && ReviverId == acRhs.ReviverId && SecondsLeft == acRhs.SecondsLeft &&
               Refusal == acRhs.Refusal && HasRespawnPoint == acRhs.HasRespawnPoint && RespawnCellId == acRhs.RespawnCellId && RespawnWorldSpaceId == acRhs.RespawnWorldSpaceId &&
               RespawnPosition == acRhs.RespawnPosition;
    }

    //! The player whose state changed.
    uint32_t PlayerId{};
    State Change{kDown};
    //! Who revived them (kRevived), or whose revive was refused (kReviveRefused).
    uint32_t ReviverId{};
    //! How long they can still be revived (kDown only).
    uint32_t SecondsLeft{};
    //! Why the revive was refused (kReviveRefused only).
    ReviveRefusal Refusal{ReviveRefusal::kNone};
    //! Where PlayerId respawns (kDied and kPartyFallen): the party leader's safe point, else their own (SafePointRequest).
    //! Without one, their game uses the game's own respawn spot.
    bool HasRespawnPoint{};
    GameId RespawnCellId{};
    GameId RespawnWorldSpaceId{};
    Vector3_NetQuantize RespawnPosition{};
};
