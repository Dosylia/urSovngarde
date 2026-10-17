#include "ReviveRules.h"

#include <algorithm>

namespace ReviveRules
{
Refusal CheckRevive(const ReviveCheck& acCheck) noexcept
{
    if (acCheck.ReviverId == acCheck.TargetId)
        return Refusal::kSelf;
    if (!acCheck.TargetDown)
        return Refusal::kTargetNotDown;
    if (!acCheck.TargetTimeLeft)
        return Refusal::kTargetOutOfTime;
    if (acCheck.ReviverDown)
        return Refusal::kReviverDown;
    if (!acCheck.SameParty)
        return Refusal::kNotSameParty;
    if (!acCheck.SamePlace)
        return Refusal::kNotSamePlace;
    if (acCheck.Distance > cReachUnits)
        return Refusal::kTooFar;
    return Refusal::kNone;
}

const char* Describe(const Refusal aRefusal) noexcept
{
    switch (aRefusal)
    {
    case Refusal::kNone: return "allowed";
    case Refusal::kSelf: return "a player cannot revive themselves";
    case Refusal::kTargetNotDown: return "the target is not down";
    case Refusal::kTargetOutOfTime: return "the target's time to be revived ran out";
    case Refusal::kReviverDown: return "the reviver is down too";
    case Refusal::kNotSameParty: return "they are not in the same party";
    case Refusal::kNotSamePlace: return "they are not in the same worldspace or interior";
    case Refusal::kTooFar: return "they are too far apart";
    }
    return "unknown";
}

uint32_t DownedSeconds(const size_t aPartySize, const uint32_t aPartySeconds) noexcept
{
    return aPartySize >= 2 ? aPartySeconds : 0;
}

bool NobodyToRevive(const std::vector<bool>& acMembersDown) noexcept
{
    return !acMembersDown.empty() && std::all_of(acMembersDown.begin(), acMembersDown.end(), [](const bool aDown) { return aDown; });
}

RespawnPoint ChooseRespawnPoint(const bool aLeaderHasSafePoint, const bool aHasOwnSafePoint) noexcept
{
    if (aLeaderHasSafePoint)
        return RespawnPoint::kPartyLeader;
    if (aHasOwnSafePoint)
        return RespawnPoint::kOwn;
    return RespawnPoint::kGameDefault;
}
} // namespace ReviveRules
