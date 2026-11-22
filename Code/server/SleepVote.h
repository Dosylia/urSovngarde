#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <TimeSkipHours.h>

/**
 * The shared time skip vote: who confirmed which length, whether every connected player has, and when it runs out.
 * A player who withdrew, or whose vote ran out, waits before confirming again.
 * One player's changes of length reach the others at most every few seconds, so nobody can flood them.
 * Plain values in and out, so the unit tests check the rules without a server (SleepVoteService applies them).
 */
class SleepVote
{
public:
    /** How long a vote stays open after its first confirmation, in milliseconds of server time. */
    static constexpr uint64_t kTimeoutMs = 60000;

    /** How long a player waits to confirm again after withdrawing, or after a vote they confirmed in ran out, in milliseconds. */
    static constexpr uint64_t kCooldownMs = 30000;

    /** Least time between two of one player's lengths that everyone is told of, in milliseconds; a change sooner is told once it has passed. */
    static constexpr uint64_t kChangeNoticeIntervalMs = 5000;

    /** How soon after a vote ended a confirmation, and its drop, count as having crossed the end on their way, in milliseconds; such a confirmation is dropped with no wait. */
    static constexpr uint64_t kCrossGraceMs = 2000;

    /** What came of a confirmation. */
    enum class ConfirmResult
    {
        kAccepted,       // recorded; everyone is to be told
        kChangedQuietly, // a new length recorded less than kChangeNoticeIntervalMs after the player's last told one; told later (TakeChangesDue)
        kUnchanged,      // the player had confirmed this length already; nothing changes
        kInvalidHours,   // a length no menu offers (TimeSkipHours::IsValid)
        kTooSoon,        // the player still waits after withdrawing or after a vote ran out
    };

    /** Where the vote stands against the players connected now. */
    enum class Standing
    {
        kClosed,   // nobody has confirmed
        kWaiting,  // some connected players have not confirmed yet
        kAgreed,   // every connected player confirmed
        kTimedOut, // the vote ran out before every connected player confirmed
    };

    /**
     * Record a player's confirmed length; the first confirmation opens the vote and starts its timeout.
     * A player who confirmed already may change their length with no wait; everyone hears of it at most every kChangeNoticeIntervalMs.
     *
     * @param aPlayerId The player's server id.
     * @param aHours The length the player picked, in game hours.
     * @param aNowMs Server time now, in milliseconds.
     * @return What came of it; nothing is recorded with kUnchanged, kInvalidHours and kTooSoon.
     */
    ConfirmResult Confirm(uint32_t aPlayerId, float aHours, uint64_t aNowMs) noexcept;

    /**
     * Take a player's confirmation back, after which they wait kCooldownMs to confirm again; the vote closes when nobody is left confirmed.
     *
     * @param aPlayerId The player's server id.
     * @param aNowMs Server time now, in milliseconds.
     * @return True when the player had confirmed.
     */
    bool Withdraw(uint32_t aPlayerId, uint64_t aNowMs) noexcept;

    /**
     * Drop a confirmation the player's game no longer counts as its own: one it sent as a vote ended, which the server recorded after the end.
     * It costs no wait once per player and end, when both the confirmation and the drop came within kCrossGraceMs of the end; otherwise it counts as a withdrawal.
     *
     * @param aPlayerId The player's server id.
     * @param aNowMs Server time now, in milliseconds.
     * @return True when the player had confirmed.
     */
    bool DropCrossed(uint32_t aPlayerId, uint64_t aNowMs) noexcept;

    /**
     * End the vote: forget every confirmation, with no wait for anyone, and note when it ended, for confirmations that cross the end.
     *
     * @param aNowMs Server time now, in milliseconds.
     */
    void Close(uint64_t aNowMs) noexcept;

    /**
     * End a vote that ran out: every player who had confirmed waits kCooldownMs to confirm again, then the vote closes.
     *
     * @param aNowMs Server time now, in milliseconds.
     */
    void RunOut(uint64_t aNowMs) noexcept;

    /**
     * Forget a player who left: their confirmation and their wait.
     *
     * @param aPlayerId The player's server id.
     */
    void Forget(uint32_t aPlayerId) noexcept;

    /**
     * Take the players whose change of length was recorded quietly and may now be told, as kChangeNoticeIntervalMs has passed.
     *
     * @param aNowMs Server time now, in milliseconds.
     * @return Their server ids; each counts as told from now on.
     */
    std::vector<uint32_t> TakeChangesDue(uint64_t aNowMs) noexcept;

    /**
     * Tell how long a player still waits before confirming again.
     *
     * @param aPlayerId The player's server id.
     * @param aNowMs Server time now, in milliseconds.
     * @return The time left, in milliseconds; 0 when the player may confirm, or when the server's clock went back.
     */
    uint64_t GetCooldownLeftMs(uint32_t aPlayerId, uint64_t aNowMs) const noexcept;

    /**
     * Tell whether a vote is open: somebody has confirmed and nobody closed it since.
     *
     * @return True while at least one confirmation is recorded.
     */
    bool IsOpen() const noexcept;

    /**
     * Tell whether a player has confirmed.
     *
     * @param aPlayerId The player's server id.
     * @return True when the player's confirmation is recorded.
     */
    bool HasConfirmed(uint32_t aPlayerId) const noexcept;

    /**
     * Tell where the vote stands against the players connected now.
     * Agreement wins over the timeout when both are true at once.
     *
     * @param acConnectedIds Server ids of every connected player, loading ones and those in the main menu included.
     * @param aNowMs Server time now, in milliseconds.
     * @return The vote's standing; kClosed when nobody has confirmed or nobody is connected.
     */
    Standing GetStanding(const std::vector<uint32_t>& acConnectedIds, uint64_t aNowMs) const noexcept;

    /**
     * Give the shortest confirmed length, which is the one the skip uses.
     *
     * @return The shortest length, in game hours; 0 when nobody has confirmed.
     */
    float GetShortestHours() const noexcept;

    /**
     * Count the connected players who confirmed.
     *
     * @param acConnectedIds Server ids of every connected player.
     * @return How many of them confirmed.
     */
    std::size_t CountReady(const std::vector<uint32_t>& acConnectedIds) const noexcept;

    /**
     * List the connected players who have not confirmed yet.
     *
     * @param acConnectedIds Server ids of every connected player.
     * @return Their server ids, in the order given; empty when everyone confirmed.
     */
    std::vector<uint32_t> ListMissing(const std::vector<uint32_t>& acConnectedIds) const;

private:
    /** One player's confirmation in the open vote. */
    struct Confirmation
    {
        float Hours = 0.f;           // the confirmed length, in game hours
        uint64_t RecordedAtMs = 0;   // server time of the player's first confirmation in this vote
        uint64_t ToldAtMs = 0;       // server time everyone was last told the player's length
        bool HasUntoldChange = false; // a change of length recorded since, that nobody was told yet
    };

    /**
     * Replace the length of a player who confirmed already.
     *
     * @param aConfirmation The player's confirmation.
     * @param aHours The new length, in game hours.
     * @param aNowMs Server time now, in milliseconds.
     * @return kUnchanged for the same length; kChangedQuietly when the last told length is less than kChangeNoticeIntervalMs old; else kAccepted.
     */
    static ConfirmResult ChangeHours(Confirmation& aConfirmation, float aHours, uint64_t aNowMs) noexcept;

    /**
     * Tell whether a confirmation crossed the end of the last vote: recorded, and now dropped, within kCrossGraceMs after it.
     *
     * @param acConfirmation The confirmation.
     * @param aNowMs Server time of the drop, in milliseconds.
     * @return False when no vote has ended yet.
     */
    bool HasCrossedEnd(const Confirmation& acConfirmation, uint64_t aNowMs) const noexcept;

    /**
     * Tell whether the vote has been open for its whole timeout.
     *
     * @param aNowMs Server time now, in milliseconds.
     * @return True when the timeout has passed, or when the server's clock went back; false while no vote is open.
     */
    bool HasRunOut(uint64_t aNowMs) const noexcept;

    /**
     * Make a player wait kCooldownMs from now before confirming again.
     *
     * @param aPlayerId The player's server id.
     * @param aNowMs Server time now, in milliseconds.
     */
    void StartCooldown(uint32_t aPlayerId, uint64_t aNowMs) noexcept;

    std::map<uint32_t, Confirmation> m_confirmations; // by server id
    std::map<uint32_t, uint64_t> m_cooldownSinceMs;   // server time a player's wait began, by server id
    uint64_t m_openedAtMs = 0;                        // server time of the vote's first confirmation
    std::optional<uint64_t> m_endedAtMs;              // server time the last vote was agreed or ran out; nothing before the first
    std::set<uint32_t> m_freeDropsSinceEnd;           // players whose drop since the last end cost no wait
};
