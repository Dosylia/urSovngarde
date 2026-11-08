#pragma once

#ifdef SKYRIMVR
// Stacks and addresses for the watches' log lines, through game and plugin frames alike.
namespace GameStack
{
bool Readable(const void* apAddress, size_t aBytes) noexcept;
//! "module+offset", or the bare address outside any module.
std::string Where(const void* apAddress) noexcept;
//! Whether the game's unwind table reads as one; without it stacks stop at the first game frame.
bool Ready() noexcept;
//! The return addresses from the caller's caller up.
USHORT Capture(void** apFrames, USHORT aMax) noexcept;
std::string Describe(void* const* apFrames, USHORT aDepth) noexcept;
} // namespace GameStack
#endif
