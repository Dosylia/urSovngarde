#include <TiltedOnlinePCH.h>

#include <SceneLines.h>

#include <mutex>

namespace
{
// The speak hook runs on the game's threads and the relay on ours.
std::mutex s_lock;
// By scene form id: when a copy last said a line of it.
TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_lastLines;
} // namespace

namespace SceneLines
{
void NoteCopyLine(uint32_t aSceneId) noexcept
{
    std::scoped_lock lock(s_lock);
    s_lastLines[aSceneId] = std::chrono::steady_clock::now();
}

bool SceneSpokeSince(uint32_t aSceneId, std::chrono::steady_clock::time_point aSince) noexcept
{
    std::scoped_lock lock(s_lock);
    const auto it = s_lastLines.find(aSceneId);
    return it != s_lastLines.end() && it->second >= aSince;
}
} // namespace SceneLines
