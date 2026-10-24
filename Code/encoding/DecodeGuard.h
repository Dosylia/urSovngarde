#pragma once

#include <cstdint>

//! Set by a reader that meets a size or an id no real message has; the message factories then drop the whole message.
//! A message comes from the network, so a damaged or crafted one must not allocate without end or be read half wrong.
namespace DecodeGuard
{
inline thread_local bool t_failed = false;

inline void Fail() noexcept
{
    t_failed = true;
}

//! False, and the message failed, when a count read from the network passes what any real message holds.
inline bool Within(const uint64_t aValue, const uint64_t aMax) noexcept
{
    if (aValue <= aMax)
        return true;
    t_failed = true;
    return false;
}
} // namespace DecodeGuard
