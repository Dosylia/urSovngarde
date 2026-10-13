#pragma once

#include <chrono>

//! Logs when a message for a character we do have is thrown away because the epoch does not match.
//!
//! The health, death, equipment and inventory handlers accept a message only when its ownership epoch equals the copy's.
//! `NotifyOwnershipTransfer` is range-filtered, so a player who is away when an actor changes hands misses the new epoch.
//! That player then drops every message about the character until a re-sent spawn refreshes it.
//!
//! The count tells whether the equality test should become "not older". Rate limited per kind.
inline void ReportEpochMiss(const char* apWhat, uint32_t aServerId, uint32_t aMessageEpoch, uint32_t aCopyEpoch) noexcept
{
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_nextByKind;
    static std::unordered_map<std::string, uint32_t> s_sinceByKind;

    const auto now = std::chrono::steady_clock::now();
    auto& count = s_sinceByKind[apWhat];
    ++count;

    auto& next = s_nextByKind[apWhat];
    if (now < next)
        return;

    next = now + std::chrono::seconds(10);
    spdlog::warn("EpochMiss: dropped {} for character {:X} -- the message is epoch {} and this copy is epoch {}. {} of these since the last line.", apWhat, aServerId, aMessageEpoch, aCopyEpoch, count);
    count = 0;
}
