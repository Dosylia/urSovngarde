#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VR/PosedCopyPlacement.h>
#include <Games/Skyrim/VR/PoseKeepDiag.h>

#include <Actor.h>
#include <Games/ActorExtension.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace
{
// Without a frame end this recent, places are given at once by the interpolation.
constexpr auto kFrameEndRunsWithin = std::chrono::milliseconds(500);

/**
 * A place kept for a copy until the frame end.
 */
struct KeptPlace
{
    NiPoint3 Position{};
    NiPoint3 Angles{};
};

// Kept by the interpolation's threads; taken by the frame end when it starts.
std::mutex s_lock;
std::unordered_map<uint32_t, KeptPlace> s_kept;
// The places the running frame end took; main thread only.
std::unordered_map<uint32_t, KeptPlace> s_thisFrameEnd;
std::atomic<int64_t> s_lastFrameEnd{0};

/**
 * Read the current time as a count, for the atomic.
 *
 * @return The steady clock's ticks now.
 */
int64_t NowTicks() noexcept
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

/**
 * Put a copy where it goes and turn it, as the interpolation did on its own thread.
 *
 * @param apActor The copy, another player's.
 * @param acPlace Where it goes and how it turns.
 */
void PlaceCopy(Actor* apActor, const KeptPlace& acPlace) noexcept
{
    apActor->ForcePosition(acPlace.Position);
    apActor->SetRotation(acPlace.Angles.x, acPlace.Angles.y, acPlace.Angles.z);
    PoseKeepDiag::NoteGivenAtFrameEnd(apActor->formID);
}

/**
 * Check whether an actor is still another player's copy, which a kept place may move.
 *
 * @param apActor The actor, or null.
 * @return True for a remote player's copy.
 */
bool IsRemotePlayerCopy(Actor* apActor) noexcept
{
    const ActorExtension* pExtension = apActor ? apActor->GetExtension() : nullptr;
    return pExtension && pExtension->IsRemotePlayer();
}
} // namespace

namespace PosedCopyPlacement
{
/**
 * Check whether the frame end runs, so a place kept for it is given within a frame.
 * Without it (no renderer hook, or not yet), the interpolation places copies itself.
 *
 * @return True when the frame end ran in the last half second.
 */
bool IsFrameEndRunning() noexcept
{
    const int64_t cLast = s_lastFrameEnd.load();
    if (cLast == 0)
        return false;
    const auto cSince = std::chrono::steady_clock::duration(NowTicks() - cLast);
    return cSince < kFrameEndRunsWithin;
}

/**
 * Keep a posed copy's place for the frame end, instead of giving it now; a newer place replaces an older one not given yet.
 *
 * @param aFormId The copy's form id.
 * @param acPosition Where it goes.
 * @param acAngles How it turns (x, y, z in radians), as TESObjectREFR::SetRotation takes them.
 */
void KeepForFrameEnd(const uint32_t aFormId, const NiPoint3& acPosition, const NiPoint3& acAngles) noexcept
{
    std::scoped_lock lock(s_lock);
    s_kept[aFormId] = KeptPlace{acPosition, acAngles};
}

/**
 * Forget a copy's kept place: the interpolation's step for it starts over, and only a place kept in that step is given.
 *
 * @param aFormId The copy's form id.
 */
void DropKeptPlace(const uint32_t aFormId) noexcept
{
    std::scoped_lock lock(s_lock);
    s_kept.erase(aFormId);
}

/**
 * At the start of the frame end: take the places kept so far, the ones this frame end gives.
 */
void StartFrameEnd() noexcept
{
    s_thisFrameEnd.clear();
    std::scoped_lock lock(s_lock);
    s_thisFrameEnd.swap(s_kept);
}

/**
 * Give a copy the place this frame end took for it, now: on the main thread, right before its pose is written.
 *
 * @param apActor The copy.
 * @return True when a place was given.
 */
bool GiveKeptPlace(Actor* apActor) noexcept
{
    if (!IsRemotePlayerCopy(apActor))
        return false;
    const auto it = s_thisFrameEnd.find(apActor->formID);
    if (it == s_thisFrameEnd.end())
        return false;
    const KeptPlace cPlace = it->second;
    s_thisFrameEnd.erase(it);
    PlaceCopy(apActor, cPlace);
    return true;
}

/**
 * At the end of the frame end: give the places it took that were not given yet, so a copy not posed in this frame still moves, and note that the frame end runs.
 */
void FinishFrameEnd() noexcept
{
    s_lastFrameEnd = NowTicks();
    for (const auto& [cFormId, cPlace] : s_thisFrameEnd)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        if (IsRemotePlayerCopy(pActor))
            PlaceCopy(pActor, cPlace);
    }
    s_thisFrameEnd.clear();
}
} // namespace PosedCopyPlacement
