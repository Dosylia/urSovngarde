#include "RestingPlayers.h"

#include "ServerClock.h"

#include <TimeSkipHours.h>

#include <algorithm>

/**
 * Note that players' games began to rest an agreed skip.
 *
 * @param acPlayerIds Server ids of the players.
 * @param aNowMs Server time now, in milliseconds.
 */
void RestingPlayers::Start(const std::vector<uint32_t>& acPlayerIds, const uint64_t aNowMs) noexcept
{
    for (const uint32_t cId : acPlayerIds)
        m_restingSinceMs[cId] = aNowMs;
}

/**
 * Note that a player's game is awake, or that the player left.
 *
 * @param aPlayerId The player's server id.
 */
void RestingPlayers::Wake(const uint32_t aPlayerId) noexcept
{
    m_restingSinceMs.erase(aPlayerId);
}

/**
 * Tell whether any game still rests: not awake, and within the longest rest.
 *
 * @param aNowMs Server time now, in milliseconds.
 * @return True while one does; false once the server's clock went back.
 */
bool RestingPlayers::IsAnyoneResting(const uint64_t aNowMs) const noexcept
{
    return std::any_of(m_restingSinceMs.begin(), m_restingSinceMs.end(),
                       [aNowMs](const auto& acRest) { return !ServerClock::HasPassed(acRest.second, TimeSkipHours::kLongestRestMs, aNowMs); });
}
