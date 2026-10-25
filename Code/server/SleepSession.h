#pragma once

#include <Messages/RequestSleep.h>

#include <algorithm>
#include <cmath>
#include <optional>

// Owns the sleep lease independently of players and the game clock.
class SleepSession
{
public:
    static constexpr uint64_t kTimeoutMs = 60000;

    std::optional<uint32_t> GetPlayerId() const noexcept { return m_playerId; }

    bool HasExpired(uint64_t aTick) const noexcept { return m_playerId && aTick >= m_lastTick && aTick - m_lastTick >= kTimeoutMs; }

    void Start(uint32_t aPlayerId, uint64_t aTick) noexcept
    {
        m_playerId = aPlayerId;
        m_lastTick = aTick;
    }

    void Reset() noexcept { m_playerId.reset(); }

    // Invalid packets must neither alter the shared clock nor keep a lease alive.
    std::optional<float> Accept(uint32_t aPlayerId, const RequestSleep& acRequest, uint64_t aTick) noexcept
    {
        if (m_playerId != aPlayerId || HasExpired(aTick) || !std::isfinite(acRequest.HoursSlept))
            return std::nullopt;

        switch (acRequest.SleepStep)
        {
        case RequestSleep::Step::kProgress:
        case RequestSleep::Step::kFinish:
        case RequestSleep::Step::kCancel:
        case RequestSleep::Step::kHeartbeat: break;
        default: return std::nullopt;
        }

        m_lastTick = aTick;
        return std::clamp(acRequest.HoursSlept, 0.f, 24.f);
    }

private:
    std::optional<uint32_t> m_playerId;
    uint64_t m_lastTick = 0;
};
