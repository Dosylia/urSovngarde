#pragma once

#include <cstdint>
#include <vector>

struct Actor;

/**
 * Copies to switch off on the main thread at the start of a frame (CharacterService::DisableQueuedCopies).
 * The client's update runs on one of the game's job threads, and switching off an actor there while another job runs it crashes in the game's movement code.
 * Kept by handle: the game reuses a freed form id.
 */
namespace FrameStartSwitchOff
{
/**
 * Queue a copy to be switched off at the start of the next frame.
 *
 * @param apActor The copy.
 */
void Queue(const Actor* apActor) noexcept;

/**
 * Take the handles queued since the last call.
 *
 * @return The copies' handles, in the order queued; a copy can be in it twice.
 */
std::vector<uint32_t> TakeQueued() noexcept;
} // namespace FrameStartSwitchOff
