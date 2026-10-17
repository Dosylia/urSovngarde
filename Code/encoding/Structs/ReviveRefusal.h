#pragma once

#include <cstdint>

//! Why the server refused a revive (ReviveRules::CheckRevive); the reviver's game is told (NotifyDownedState::kReviveRefused).
enum class ReviveRefusal : uint8_t
{
    kNone,
    kSelf,
    kTargetNotDown,
    kTargetOutOfTime,
    kReviverDown,
    kNotSameParty,
    kNotSamePlace,
    kTooFar,
};
