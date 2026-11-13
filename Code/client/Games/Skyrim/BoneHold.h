#pragma once

#ifdef SKYRIMVR
// Freed skeleton nodes are held a moment before their memory goes back, and animation bone tables left on them are moved to the new nodes.
// A skeleton rebuilt for character creation or a race change left the player's animation writing WEAPON and SHIELD into freed memory.
namespace BoneHold
{
//! Once a frame, on the main thread, from the main loop.
void Update() noexcept;
//! From the crash handler: what reached a held node, and a crashing register pointing into one.
void Report(const uint64_t* apRegisters, size_t aCount) noexcept;
//! Called for each node the destructor hook destroys, on any thread, before it is held.
using DestroyObserver = void (*)(void* apNode) noexcept;
void SetDestroyObserver(DestroyObserver aObserver) noexcept;
} // namespace BoneHold
#endif
