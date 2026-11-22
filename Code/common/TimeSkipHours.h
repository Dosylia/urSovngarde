#pragma once

#include <chrono>
#include <cstdint>

/** The lengths a shared time skip can ask for: one rule for the games and the server. */
namespace TimeSkipHours
{
/** Longest a game rests an agreed skip, in real seconds: longer than a full day's rest; a rest that outlasts it is over. */
constexpr unsigned kLongestRestSeconds = 90;

/** The same, in milliseconds, as the games and the server count time. */
constexpr uint64_t kLongestRestMs = std::chrono::milliseconds(std::chrono::seconds(kLongestRestSeconds)).count();

/** Shortest and longest skip the Sleep/Wait menu offers, in game hours: whole hours from 1 to 24. */
constexpr float kMin = 1.f;
constexpr float kMax = 24.f;

/**
 * Check that a length can be skipped: within what the menu offers, and a real number.
 * The games' own OK counts whole hours, so less than one would rest none.
 *
 * @param aHours The length a player picked, in game hours.
 * @return True when the length can be voted for.
 */
constexpr bool IsValid(const float aHours) noexcept
{
    // Both comparisons fail on NaN, so a broken number is refused.
    return aHours >= kMin && aHours <= kMax;
}
} // namespace TimeSkipHours
