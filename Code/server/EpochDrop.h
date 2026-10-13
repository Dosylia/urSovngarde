#pragma once

#include <chrono>
#include <string>

//! Logs a message dropped for a bad ownership epoch, since the sender gets no negative acknowledgement.
//! Rate limited per kind of message, because a stale sender sends many.
inline void ReportEpochDrop(const char* apWhat, uint32_t aServerId, uint32_t aSent, uint32_t aHeld) noexcept
{
    static TiltedPhoques::Map<std::string, std::chrono::steady_clock::time_point> s_nextByKind;
    const auto now = std::chrono::steady_clock::now();
    auto& next = s_nextByKind[apWhat];
    if (now < next)
        return;

    next = now + std::chrono::seconds(5);
    spdlog::warn("Dropped {} for actor {:X}: the sender's ownership epoch is {} and this server holds {}. Nothing was applied and the sender was not told.", apWhat, aServerId, aSent, aHeld);
}
