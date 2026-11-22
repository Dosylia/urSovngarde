#pragma once

#include <chrono>
#include <cstdint>

/**
 * Turn milliseconds into whole seconds, rounded up, so a player told to wait that many seconds has waited long enough.
 *
 * @param aMs The time, in milliseconds.
 * @return The seconds.
 */
constexpr uint32_t ToWholeSeconds(const uint64_t aMs) noexcept
{
    return static_cast<uint32_t>(std::chrono::ceil<std::chrono::seconds>(std::chrono::milliseconds(aMs)).count());
}
