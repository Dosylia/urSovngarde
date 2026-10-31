#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Arrows this game launched for another player's copy, replayed here.
// While one of theirs is in flight, their ranged hits on this player are decided here, by the replayed arrow.
namespace PvpProjectiles
{
// How long after a replayed launch a hit on this player counts as that projectile's: an arrow's longest flight, with margin.
constexpr std::chrono::milliseconds cFlight{3000};

// How long a projectile shot from this far can still be in flight: at 500 units a second, under Ice Storm's 512, the slowest, with 300 ms for the network.
// So next to the shooter the window is short, and the sword blow after a shot is not taken for the shot.
constexpr std::chrono::milliseconds FlightFrom(const float aDistance) noexcept
{
    return std::min(cFlight, std::chrono::milliseconds(300 + static_cast<int64_t>(aDistance / 0.5f)));
}

inline std::mutex& Lock() noexcept
{
    static std::mutex s_lock;
    return s_lock;
}

inline std::unordered_map<uint32_t, std::chrono::steady_clock::time_point>& Launches() noexcept
{
    static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> s_launches;
    return s_launches;
}

// An arrow was launched here for the copy aCopyFormId of another player.
inline void NoteLaunched(uint32_t aCopyFormId) noexcept
{
    std::scoped_lock lock(Lock());
    Launches()[aCopyFormId] = std::chrono::steady_clock::now();
}

// Whether an arrow of that copy, standing this far from this player, may still be in flight here.
inline bool Recent(uint32_t aCopyFormId, const float aDistance) noexcept
{
    std::scoped_lock lock(Lock());
    const auto it = Launches().find(aCopyFormId);
    return it != Launches().end() && std::chrono::steady_clock::now() - it->second < FlightFrom(aDistance);
}
} // namespace PvpProjectiles
