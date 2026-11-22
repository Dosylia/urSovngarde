#include <TiltedOnlinePCH.h>

#include <Copies/FrameStartSwitchOff.h>

#include <Actor.h>

#include <mutex>

namespace
{
std::mutex s_lock;
std::vector<uint32_t> s_handles;
} // namespace

namespace FrameStartSwitchOff
{
/**
 * Queue a copy to be switched off at the start of the next frame.
 *
 * @param apActor The copy.
 */
void Queue(const Actor* apActor) noexcept
{
    std::scoped_lock lock(s_lock);
    s_handles.push_back(apActor->GetHandle().handle.iBits);
}

/**
 * Take the handles queued since the last call.
 *
 * @return The copies' handles, in the order queued; a copy can be in it twice.
 */
std::vector<uint32_t> TakeQueued() noexcept
{
    std::vector<uint32_t> handles;
    std::scoped_lock lock(s_lock);
    handles.swap(s_handles);
    return handles;
}
} // namespace FrameStartSwitchOff
