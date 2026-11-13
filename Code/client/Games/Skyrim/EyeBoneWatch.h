#pragma once

#ifdef SKYRIMVR
// TEMPORARY: catches the release that frees an actor's eye bone while its parent still holds it.
// The game's eye tracking later looks that bone up by name and calls through the freed memory (SkyrimVR.exe+0x5EB6C6).
namespace EyeBoneWatch
{
//! Once a frame, on the main thread, from the main loop.
void Update() noexcept;
//! From the crash handler: the eye bones freed while held, matched against the crashing registers.
void Report(const uint64_t* apRegisters, size_t aCount) noexcept;
} // namespace EyeBoneWatch
#endif
