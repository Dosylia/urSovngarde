#include <TiltedOnlinePCH.h>

#include <Copies/FallStop.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace
{
// Bodies looked at, at most; more means old ones were never forgotten.
constexpr size_t kMostNoted = 512;

/**
 * The last look taken at a copy's ragdoll.
 */
struct LastLook
{
    float RootHeight = 0.f;
    std::chrono::steady_clock::time_point At{};
};

// PlaceCorpse looks from the interpolation, on the game's job threads.
std::mutex s_lock;
std::unordered_map<uint32_t, LastLook> s_lastLooks;
} // namespace

namespace FallStop
{
/**
 * Note a look PlaceCorpse takes at a copy's ragdoll, and decide by SankAway whether it is falling away.
 *
 * @param aFormId The copy.
 * @param aDepth How far below where its owner has the body its ragdoll lies, in units (negative is above).
 * @param aRootHeight The height of the ragdoll's root now.
 * @return True when it is falling away: the caller switches the copy off.
 */
bool NoteLookAndCheckFall(const uint32_t aFormId, const float aDepth, const float aRootHeight) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    std::scoped_lock lock(s_lock);
    if (s_lastLooks.size() > kMostNoted)
        s_lastLooks.clear();

    const auto [it, cFirstLook] = s_lastLooks.try_emplace(aFormId, LastLook{aRootHeight, cNow});
    const LastLook cLast = it->second;
    it->second = LastLook{aRootHeight, cNow};
    if (cFirstLook)
        return false;

    const float cDrop = cLast.RootHeight - aRootHeight;
    const float cSeconds = std::chrono::duration<float>(cNow - cLast.At).count();
    if (!SankAway(aDepth, cDrop, cSeconds))
        return false;

    s_lastLooks.erase(it);
    spdlog::info("FallStop: {:X}'s ragdoll lies {:.0f} units below where its owner's corpse is and dropped {:.0f} in {:.1f} s; "
                 "switched off here, a guard (its owner's game keeps the body; it is asked for again when this game announces its cell)",
                 aFormId, aDepth, cDrop, cSeconds);
    return true;
}

/**
 * Forget the looks taken at a copy, so a new copy under the same id starts afresh.
 *
 * @param aFormId The copy.
 */
void Forget(const uint32_t aFormId) noexcept
{
    std::scoped_lock lock(s_lock);
    s_lastLooks.erase(aFormId);
}
} // namespace FallStop
