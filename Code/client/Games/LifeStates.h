#pragma once

#include <cstdint>

// The game's life states, as Actor::SetLifeState takes them (ActorState::lifeState).
namespace LifeStates
{
constexpr uint32_t kAlive = 0;
constexpr uint32_t kDying = 1;
constexpr uint32_t kDead = 2;
} // namespace LifeStates
