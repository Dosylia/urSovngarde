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

// This game decides whether an arrow hit, and its shooter's game says how hard. Each hears of the same arrow about when the other does.
// So whichever comes first waits this long for the other; a report no hit here answers is an arrow that missed here.
constexpr std::chrono::milliseconds cArrowPairing{500};

struct ArrowNote
{
    float Damage = 0.f;
    std::chrono::steady_clock::time_point At{};
};

inline std::unordered_map<uint32_t, ArrowNote>& ArrowReports() noexcept
{
    static std::unordered_map<uint32_t, ArrowNote> s_reports;
    return s_reports;
}

inline std::unordered_map<uint32_t, ArrowNote>& ArrowHits() noexcept
{
    static std::unordered_map<uint32_t, ArrowNote> s_hits;
    return s_hits;
}

// A replayed arrow hit this player here for aDamage.
// Returns what its shooter's game said it does, or 0 when that is not known yet: the report then finds this hit.
inline float ArrowHitHere(uint32_t aCopyFormId, const float aDamage) noexcept
{
    std::scoped_lock lock(Lock());
    const auto cNow = std::chrono::steady_clock::now();
    float reported = 0.f;
    if (const auto it = ArrowReports().find(aCopyFormId); it != ArrowReports().end())
    {
        if (cNow - it->second.At < cArrowPairing)
            reported = it->second.Damage;
        ArrowReports().erase(it);
    }
    ArrowHits()[aCopyFormId] = ArrowNote{reported > 0.f ? reported : aDamage, cNow};
    return reported;
}

// The shooter's game says its arrow did aDamage.
// An enchanted arrow reports twice, its enchantment and then its bow, so the highest number counts.
// Returns what the replayed arrow already took here, or 0 when it has not hit yet: that hit then finds this report.
inline float ArrowReported(uint32_t aCopyFormId, const float aDamage) noexcept
{
    std::scoped_lock lock(Lock());
    const auto cNow = std::chrono::steady_clock::now();
    if (const auto it = ArrowHits().find(aCopyFormId); it != ArrowHits().end() && cNow - it->second.At < cArrowPairing)
    {
        const float cTaken = it->second.Damage;
        it->second.Damage = std::max(cTaken, aDamage);
        return cTaken;
    }
    auto& report = ArrowReports()[aCopyFormId];
    report.Damage = cNow - report.At < cArrowPairing ? std::max(report.Damage, aDamage) : aDamage;
    report.At = cNow;
    return 0.f;
}
} // namespace PvpProjectiles
