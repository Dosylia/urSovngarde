#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Projectiles this game launched for another player's copy: that player's arrows and spells, replayed here.
// While one of theirs is in flight, their ranged hits on this player are decided here, by the replayed projectile.
namespace PvpProjectiles
{
// How long after a replayed launch a hit on this player counts as that projectile's: an arrow's longest flight, with margin.
constexpr std::chrono::seconds cFlight{3};

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

// A projectile was launched here for the copy aCopyFormId of another player.
inline void NoteLaunched(uint32_t aCopyFormId) noexcept
{
    std::scoped_lock lock(Lock());
    Launches()[aCopyFormId] = std::chrono::steady_clock::now();
}

// Whether a projectile of that copy may still be in flight here.
inline bool Recent(uint32_t aCopyFormId) noexcept
{
    std::scoped_lock lock(Lock());
    const auto it = Launches().find(aCopyFormId);
    return it != Launches().end() && std::chrono::steady_clock::now() - it->second < cFlight;
}
} // namespace PvpProjectiles
