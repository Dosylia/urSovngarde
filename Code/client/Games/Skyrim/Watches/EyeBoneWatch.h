#pragma once

#ifdef SKYRIMVR
// TEMPORARY: catches the release that frees a node of an actor's skeleton, the eye bone or one above it, while its parent still holds it.
// The game's eye tracking later looks the eye bone up by name and calls through the freed memory (SkyrimVR.exe+0x5EB6C6).
// It looks at the nodes BoneHold's destructor hook destroys.
namespace EyeBoneWatch
{
/**
 * Log the nodes freed while a live parent still held them, once a frame, on the main thread, from the main loop, after BoneHold::Update.
 * The first time it also asks BoneHold to show it every node its destructor hook destroys.
 */
void Update() noexcept;
//! From the crash handler: the nodes freed while held, matched against the crashing registers.
void Report(const uint64_t* apRegisters, size_t aCount) noexcept;
} // namespace EyeBoneWatch
#endif
