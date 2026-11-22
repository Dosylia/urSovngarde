#pragma once

#include <cstdint>

/** Reading the server's millisecond clock, which can go back where the system clock does. */
namespace ServerClock
{
/**
 * Tell whether a span of time has passed since a moment.
 * A clock that went back past the moment counts the span as passed, so nothing waits for the clock to catch up.
 *
 * @param aSinceMs The moment, in server milliseconds.
 * @param aSpanMs The span, in milliseconds.
 * @param aNowMs Server time now, in milliseconds.
 * @return True once the span has passed, or when the clock went back.
 */
constexpr bool HasPassed(const uint64_t aSinceMs, const uint64_t aSpanMs, const uint64_t aNowMs) noexcept
{
    return aNowMs < aSinceMs || aNowMs - aSinceMs >= aSpanMs;
}
} // namespace ServerClock
