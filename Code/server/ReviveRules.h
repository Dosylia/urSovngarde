#pragma once

#include <cstdint>
#include <vector>

#include <Structs/ReviveRefusal.h>

//! Who may revive whom, how long a downed player waits, when nobody is left to revive them, and where they respawn.
//! Plain values in and out, so the unit tests check the rules without a server (ReviveService applies them).
namespace ReviveRules
{
//! Farthest a reviver may stand from the downed player, by the server's positions.
//! A hand reaches about a body's length, and the positions the server holds lag the players' by up to a snapshot.
constexpr float cReachUnits = 300.f;

using Refusal = ReviveRefusal;

struct ReviveCheck
{
    uint32_t ReviverId{};
    uint32_t TargetId{};
    bool ReviverDown{};
    bool TargetDown{};
    bool TargetTimeLeft{};
    bool SameParty{};
    //! The same worldspace outdoors, or the same interior cell.
    bool SamePlace{};
    float Distance{};
};

//! Why this revive is refused, or kNone when it may go ahead.
Refusal CheckRevive(const ReviveCheck& acCheck) noexcept;

//! The refusal in words, for the server log.
const char* Describe(Refusal aRefusal) noexcept;

//! How long a downed player can be revived: the server's setting in a party of two or more.
//! Alone there is nobody to wait for, so none: the respawn starts at once, as it always did.
uint32_t DownedSeconds(size_t aPartySize, uint32_t aPartySeconds) noexcept;

//! Every member of the party is down, one flag per connected member (the downed player alone counts too).
//! Then nobody is left to revive them, and they all respawn.
bool NobodyToRevive(const std::vector<bool>& acMembersDown) noexcept;

enum class RespawnPoint
{
    kPartyLeader,
    kOwn,
    kGameDefault,
};

//! Where a player who was not revived respawns: the party leader's last safe point, which brings the party back together the way one save would.
//! Without one (alone, or the leader has none yet), the player's own last safe point; without that, the game's own respawn spot.
RespawnPoint ChooseRespawnPoint(bool aLeaderHasSafePoint, bool aHasOwnSafePoint) noexcept;
} // namespace ReviveRules
