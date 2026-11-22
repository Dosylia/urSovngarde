#pragma once

#include <cstdint>

// The game's life states, as Actor::SetLifeState takes them (ActorState::LifeState reads them).
namespace LifeStates
{
constexpr uint32_t kAlive = 0;
constexpr uint32_t kDying = 1;
constexpr uint32_t kDead = 2;
constexpr uint32_t kUnconscious = 3;
// Down for good until healed: an essential actor, or this game's player held down for a revive.
constexpr uint32_t kEssentialDown = 7;
constexpr uint32_t kBleedout = 8;
} // namespace LifeStates
