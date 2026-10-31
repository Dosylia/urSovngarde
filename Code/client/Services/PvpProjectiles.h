#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

// Arrows this game launched for another player's copy, replayed here.
// While one of theirs is in flight, the hit their game reports on this player is that arrow's, and the replayed arrow decides it here.
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

// A launch that could not be made here: this game is paused, has no such projectile, or the game refused it.
struct UnmadeLaunch
{
    uint32_t Copy{};
    uint32_t Spell{};
    std::chrono::steady_clock::time_point At{};
};

inline std::vector<UnmadeLaunch>& Unmade() noexcept
{
    static std::vector<UnmadeLaunch> s_unmade;
    return s_unmade;
}

inline void NoteUnmade(uint32_t aCopyFormId, uint32_t aSpellId) noexcept
{
    std::scoped_lock lock(Lock());
    const auto cNow = std::chrono::steady_clock::now();
    auto& unmade = Unmade();
    unmade.erase(std::remove_if(unmade.begin(), unmade.end(), [cNow](const UnmadeLaunch& acLaunch) { return cNow - acLaunch.At >= cFlight; }), unmade.end());
    unmade.push_back({aCopyFormId, aSpellId, cNow});
}

// Takes out one unmade launch of that spell: the hit its caster reports then has no projectile here to decide it.
inline bool TakeUnmade(uint32_t aCopyFormId, uint32_t aSpellId) noexcept
{
    std::scoped_lock lock(Lock());
    const auto cNow = std::chrono::steady_clock::now();
    auto& unmade = Unmade();
    const auto it = std::find_if(unmade.begin(), unmade.end(), [=](const UnmadeLaunch& acLaunch)
                                 { return acLaunch.Copy == aCopyFormId && acLaunch.Spell == aSpellId && cNow - acLaunch.At < cFlight; });
    if (it == unmade.end())
        return false;

    unmade.erase(it);
    return true;
}

// One side of another player's aimed spell on this player: its replayed hit here, or its caster's report of how strong it was.
// Amount is the damage that side knows of.
struct SpellSide
{
    uint32_t Copy{};
    uint32_t Spell{};
    uint32_t Effect{};
    bool Reported{};
    float Amount{};
    std::chrono::steady_clock::time_point At{};
};

// How long one side waits for the other: a projectile's longest flight.
constexpr std::chrono::milliseconds cSpellSideWait = cFlight;

// The two sides arrive in either order, so the first waits in aWaiting for the second.
// Takes the other side of acSide out of the list, if it is waiting there.
inline std::optional<SpellSide> TakeOtherSide(std::vector<SpellSide>& aWaiting, const SpellSide& acSide) noexcept
{
    const auto it = std::find_if(aWaiting.begin(), aWaiting.end(), [&acSide](const SpellSide& acOther)
                                 { return acOther.Copy == acSide.Copy && acOther.Spell == acSide.Spell && acOther.Effect == acSide.Effect && acOther.Reported != acSide.Reported; });
    if (it == aWaiting.end())
        return std::nullopt;

    const SpellSide cOther = *it;
    aWaiting.erase(it);
    return cOther;
}

// What the caster's report adds to the hit here: it decides how much, and the hit has already done its own share.
constexpr float SpellTopUp(const float aReported, const float aLanded) noexcept
{
    return std::max(0.f, aReported - aLanded);
}

// Takes out the sides that waited too long, and returns how many of them were reports: spells the caster's game saw hit, and this game did not.
inline size_t DropOldSpellSides(std::vector<SpellSide>& aWaiting, const std::chrono::steady_clock::time_point aNow) noexcept
{
    size_t reports = 0;
    for (auto it = aWaiting.begin(); it != aWaiting.end();)
    {
        if (aNow - it->At < cSpellSideWait)
        {
            ++it;
            continue;
        }
        reports += it->Reported ? 1 : 0;
        it = aWaiting.erase(it);
    }
    return reports;
}
} // namespace PvpProjectiles
