#pragma once

#include <cstdint>
#include <map>
#include <vector>

/**
 * The players whose games rest the hours of an agreed shared skip, until each says it is awake or TimeSkipHours::kLongestRestSeconds pass.
 * While anyone rests, a plugin's hour move waits, so it cannot land in the middle of a game's own rest.
 * Plain values in and out, so the unit tests check it without a server (SleepVoteService applies it).
 */
class RestingPlayers
{
public:
    /**
     * Note that players' games began to rest an agreed skip.
     *
     * @param acPlayerIds Server ids of the players.
     * @param aNowMs Server time now, in milliseconds.
     */
    void Start(const std::vector<uint32_t>& acPlayerIds, uint64_t aNowMs) noexcept;

    /**
     * Note that a player's game is awake, or that the player left.
     *
     * @param aPlayerId The player's server id.
     */
    void Wake(uint32_t aPlayerId) noexcept;

    /**
     * Tell whether any game still rests: not awake, and within the longest rest.
     *
     * @param aNowMs Server time now, in milliseconds.
     * @return True while one does; false once the server's clock went back.
     */
    bool IsAnyoneResting(uint64_t aNowMs) const noexcept;

private:
    std::map<uint32_t, uint64_t> m_restingSinceMs; // server time each resting player's game began its rest, by server id
};
