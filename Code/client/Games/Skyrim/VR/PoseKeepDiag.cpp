#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VR/PoseKeepDiag.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace PoseKeepDiag
{
namespace
{
constexpr auto kWindow = std::chrono::seconds(10);

/**
 * What one copy did in the current 10 s window.
 */
struct Window
{
    std::chrono::steady_clock::time_point Since{};
    uint32_t FrameEnds = 0;
    uint32_t StillWhereLeft = 0;
    uint32_t MovedSince = 0;
    uint32_t Switches = 0;
    bool HasLast = false;
    bool LastStill = false;
    // The whole-body shift at the frame ends where something had moved the bones, and the largest at any.
    float ShiftSumMoved = 0.f;
    float ShiftMost = 0.f;
    uint32_t Placed = 0;
    uint32_t PlacedAtOnce = 0;
    uint32_t GivenAtFrameEnd = 0;
};

std::mutex s_lock;
std::unordered_map<uint32_t, Window> s_windows;

/**
 * Write a copy's window to the log and start a new one.
 *
 * @param aFormId The copy's form id.
 * @param arWindow Its window, emptied afterwards.
 * @param aNow The current time.
 */
void Report(const uint32_t aFormId, Window& arWindow, const std::chrono::steady_clock::time_point aNow) noexcept
{
    const float cShiftAverage = arWindow.MovedSince ? arWindow.ShiftSumMoved / static_cast<float>(arWindow.MovedSince) : 0.f;
    const auto cSeconds = std::chrono::duration_cast<std::chrono::seconds>(aNow - arWindow.Since).count();
    spdlog::info("PoseKeepDiag: copy {:X}, {} s: {} frame ends; its bones were where the last frame end put them at {}, moved by something else at {} "
                 "(switching {} times); the frame end moved the whole body to the pose's hips by {:.0f} units on average at those, {:.0f} at most; "
                 "{} of {} positions given to it moved its 3D at once, {} were given at the frame end",
                 aFormId, cSeconds, arWindow.FrameEnds, arWindow.StillWhereLeft, arWindow.MovedSince, arWindow.Switches, cShiftAverage, arWindow.ShiftMost,
                 arWindow.PlacedAtOnce, arWindow.Placed, arWindow.GivenAtFrameEnd);
    arWindow = Window{};
    arWindow.Since = aNow;
}
} // namespace

/**
 * Count one frame end at which a live copy of another player is posed.
 *
 * @param aFormId The copy's form id.
 * @param aStillWhereLeft True when its pelvis was still where the last frame end put it, false when something moved it since.
 * @param aBodyShift How far across the ground, in game units, this frame end moved the whole body to put its hips where the pose has them.
 */
void NotePosed(const uint32_t aFormId, const bool aStillWhereLeft, const float aBodyShift) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    std::scoped_lock lock(s_lock);
    Window& window = s_windows[aFormId];
    if (window.Since == std::chrono::steady_clock::time_point{})
        window.Since = cNow;

    ++window.FrameEnds;
    if (aStillWhereLeft)
        ++window.StillWhereLeft;
    else
        ++window.MovedSince;
    if (window.HasLast && window.LastStill != aStillWhereLeft)
        ++window.Switches;
    window.HasLast = true;
    window.LastStill = aStillWhereLeft;
    if (!aStillWhereLeft)
        window.ShiftSumMoved += aBodyShift;
    window.ShiftMost = std::max(window.ShiftMost, aBodyShift);

    if (cNow - window.Since >= kWindow)
        Report(aFormId, window, cNow);
}

/**
 * Count one position the interpolation gives a copy of another player.
 *
 * @param aFormId The copy's form id.
 * @param aMovedAtOnce True when the game moves its 3D at once, false when its character controller takes it to the next physics step.
 */
void NotePlaced(const uint32_t aFormId, const bool aMovedAtOnce) noexcept
{
    std::scoped_lock lock(s_lock);
    Window& window = s_windows[aFormId];
    ++window.Placed;
    if (aMovedAtOnce)
        ++window.PlacedAtOnce;
}

/**
 * Count one of those positions given at the frame end, right before the copy's pose (PosedCopyPlacement), rather than on the interpolation's thread.
 *
 * @param aFormId The copy's form id.
 */
void NoteGivenAtFrameEnd(const uint32_t aFormId) noexcept
{
    std::scoped_lock lock(s_lock);
    ++s_windows[aFormId].GivenAtFrameEnd;
}
} // namespace PoseKeepDiag
