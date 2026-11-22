#include "SleepVote.h"

#include "ServerClock.h"

#include <algorithm>
#include <iterator>

/**
 * Record a player's confirmed length; the first confirmation opens the vote and starts its timeout.
 * A player who confirmed already may change their length with no wait; everyone hears of it at most every kChangeNoticeIntervalMs.
 *
 * @param aPlayerId The player's server id.
 * @param aHours The length the player picked, in game hours.
 * @param aNowMs Server time now, in milliseconds.
 * @return What came of it; nothing is recorded with kUnchanged, kInvalidHours and kTooSoon.
 */
SleepVote::ConfirmResult SleepVote::Confirm(const uint32_t aPlayerId, const float aHours, const uint64_t aNowMs) noexcept
{
    if (!TimeSkipHours::IsValid(aHours))
        return ConfirmResult::kInvalidHours;

    if (const auto cIt = m_confirmations.find(aPlayerId); cIt != m_confirmations.end())
        return ChangeHours(cIt->second, aHours, aNowMs);

    if (GetCooldownLeftMs(aPlayerId, aNowMs) > 0)
        return ConfirmResult::kTooSoon;

    if (!IsOpen())
        m_openedAtMs = aNowMs;

    m_confirmations[aPlayerId] = Confirmation{aHours, aNowMs, aNowMs, false};
    m_cooldownSinceMs.erase(aPlayerId);
    return ConfirmResult::kAccepted;
}

/**
 * Take a player's confirmation back, after which they wait kCooldownMs to confirm again; the vote closes when nobody is left confirmed.
 *
 * @param aPlayerId The player's server id.
 * @param aNowMs Server time now, in milliseconds.
 * @return True when the player had confirmed.
 */
bool SleepVote::Withdraw(const uint32_t aPlayerId, const uint64_t aNowMs) noexcept
{
    if (m_confirmations.erase(aPlayerId) == 0)
        return false;

    StartCooldown(aPlayerId, aNowMs);
    return true;
}

/**
 * Drop a confirmation the player's game no longer counts as its own: one it sent as a vote ended, which the server recorded after the end.
 * It costs no wait once per player and end, when both the confirmation and the drop came within kCrossGraceMs of the end; otherwise it counts as a withdrawal.
 *
 * @param aPlayerId The player's server id.
 * @param aNowMs Server time now, in milliseconds.
 * @return True when the player had confirmed.
 */
bool SleepVote::DropCrossed(const uint32_t aPlayerId, const uint64_t aNowMs) noexcept
{
    const auto cIt = m_confirmations.find(aPlayerId);
    if (cIt == m_confirmations.end())
        return false;

    // Once, right after an end, so the drop cannot serve to confirm and withdraw again and again without waiting.
    if (!HasCrossedEnd(cIt->second, aNowMs) || !m_freeDropsSinceEnd.insert(aPlayerId).second)
        return Withdraw(aPlayerId, aNowMs);

    m_confirmations.erase(cIt);
    return true;
}

/**
 * End the vote: forget every confirmation, with no wait for anyone, and note when it ended, for confirmations that cross the end.
 *
 * @param aNowMs Server time now, in milliseconds.
 */
void SleepVote::Close(const uint64_t aNowMs) noexcept
{
    m_confirmations.clear();
    m_endedAtMs = aNowMs;
    m_freeDropsSinceEnd.clear();
}

/**
 * End a vote that ran out: every player who had confirmed waits kCooldownMs to confirm again, then the vote closes.
 *
 * @param aNowMs Server time now, in milliseconds.
 */
void SleepVote::RunOut(const uint64_t aNowMs) noexcept
{
    for (const auto& [cId, cConfirmation] : m_confirmations)
        StartCooldown(cId, aNowMs);
    Close(aNowMs);
}

/**
 * Forget a player who left: their confirmation and their wait.
 *
 * @param aPlayerId The player's server id.
 */
void SleepVote::Forget(const uint32_t aPlayerId) noexcept
{
    m_confirmations.erase(aPlayerId);
    m_cooldownSinceMs.erase(aPlayerId);
    m_freeDropsSinceEnd.erase(aPlayerId);
}

/**
 * Take the players whose change of length was recorded quietly and may now be told, as kChangeNoticeIntervalMs has passed.
 *
 * @param aNowMs Server time now, in milliseconds.
 * @return Their server ids; each counts as told from now on.
 */
std::vector<uint32_t> SleepVote::TakeChangesDue(const uint64_t aNowMs) noexcept
{
    std::vector<uint32_t> due;
    for (auto& [cId, confirmation] : m_confirmations)
    {
        if (!confirmation.HasUntoldChange || !ServerClock::HasPassed(confirmation.ToldAtMs, kChangeNoticeIntervalMs, aNowMs))
            continue;

        confirmation.HasUntoldChange = false;
        confirmation.ToldAtMs = aNowMs;
        due.push_back(cId);
    }
    return due;
}

/**
 * Tell how long a player still waits before confirming again.
 *
 * @param aPlayerId The player's server id.
 * @param aNowMs Server time now, in milliseconds.
 * @return The time left, in milliseconds; 0 when the player may confirm, or when the server's clock went back.
 */
uint64_t SleepVote::GetCooldownLeftMs(const uint32_t aPlayerId, const uint64_t aNowMs) const noexcept
{
    const auto cIt = m_cooldownSinceMs.find(aPlayerId);
    if (cIt == m_cooldownSinceMs.end() || ServerClock::HasPassed(cIt->second, kCooldownMs, aNowMs))
        return 0;

    return kCooldownMs - (aNowMs - cIt->second);
}

/**
 * Tell whether a vote is open: somebody has confirmed and nobody closed it since.
 *
 * @return True while at least one confirmation is recorded.
 */
bool SleepVote::IsOpen() const noexcept
{
    return !m_confirmations.empty();
}

/**
 * Tell whether a player has confirmed.
 *
 * @param aPlayerId The player's server id.
 * @return True when the player's confirmation is recorded.
 */
bool SleepVote::HasConfirmed(const uint32_t aPlayerId) const noexcept
{
    return m_confirmations.contains(aPlayerId);
}

/**
 * Tell where the vote stands against the players connected now.
 * Agreement wins over the timeout when both are true at once.
 *
 * @param acConnectedIds Server ids of every connected player, loading ones and those in the main menu included.
 * @param aNowMs Server time now, in milliseconds.
 * @return The vote's standing; kClosed when nobody has confirmed or nobody is connected.
 */
SleepVote::Standing SleepVote::GetStanding(const std::vector<uint32_t>& acConnectedIds, const uint64_t aNowMs) const noexcept
{
    if (!IsOpen() || acConnectedIds.empty())
        return Standing::kClosed;

    if (CountReady(acConnectedIds) == acConnectedIds.size())
        return Standing::kAgreed;

    if (HasRunOut(aNowMs))
        return Standing::kTimedOut;

    return Standing::kWaiting;
}

/**
 * Give the shortest confirmed length, which is the one the skip uses.
 *
 * @return The shortest length, in game hours; 0 when nobody has confirmed.
 */
float SleepVote::GetShortestHours() const noexcept
{
    if (!IsOpen())
        return 0.f;

    const auto cIsShorter = [](const auto& acLeft, const auto& acRight) { return acLeft.second.Hours < acRight.second.Hours; };
    return std::min_element(m_confirmations.begin(), m_confirmations.end(), cIsShorter)->second.Hours;
}

/**
 * Count the connected players who confirmed.
 *
 * @param acConnectedIds Server ids of every connected player.
 * @return How many of them confirmed.
 */
std::size_t SleepVote::CountReady(const std::vector<uint32_t>& acConnectedIds) const noexcept
{
    return std::count_if(acConnectedIds.begin(), acConnectedIds.end(), [this](const uint32_t acId) { return HasConfirmed(acId); });
}

/**
 * List the connected players who have not confirmed yet.
 *
 * @param acConnectedIds Server ids of every connected player.
 * @return Their server ids, in the order given; empty when everyone confirmed.
 */
std::vector<uint32_t> SleepVote::ListMissing(const std::vector<uint32_t>& acConnectedIds) const
{
    std::vector<uint32_t> missing;
    std::copy_if(acConnectedIds.begin(), acConnectedIds.end(), std::back_inserter(missing), [this](const uint32_t acId) { return !HasConfirmed(acId); });
    return missing;
}

/**
 * Replace the length of a player who confirmed already.
 *
 * @param aConfirmation The player's confirmation.
 * @param aHours The new length, in game hours.
 * @param aNowMs Server time now, in milliseconds.
 * @return kUnchanged for the same length; kChangedQuietly when the last told length is less than kChangeNoticeIntervalMs old; else kAccepted.
 */
SleepVote::ConfirmResult SleepVote::ChangeHours(Confirmation& aConfirmation, const float aHours, const uint64_t aNowMs) noexcept
{
    if (aConfirmation.Hours == aHours)
        return ConfirmResult::kUnchanged;

    // Recorded at once either way, so the skip always uses the length the player picked last.
    aConfirmation.Hours = aHours;
    if (!ServerClock::HasPassed(aConfirmation.ToldAtMs, kChangeNoticeIntervalMs, aNowMs))
    {
        aConfirmation.HasUntoldChange = true;
        return ConfirmResult::kChangedQuietly;
    }

    aConfirmation.HasUntoldChange = false;
    aConfirmation.ToldAtMs = aNowMs;
    return ConfirmResult::kAccepted;
}

/**
 * Tell whether a confirmation crossed the end of the last vote: recorded, and now dropped, within kCrossGraceMs after it.
 *
 * @param acConfirmation The confirmation.
 * @param aNowMs Server time of the drop, in milliseconds.
 * @return False when no vote has ended yet.
 */
bool SleepVote::HasCrossedEnd(const Confirmation& acConfirmation, const uint64_t aNowMs) const noexcept
{
    return m_endedAtMs && !ServerClock::HasPassed(*m_endedAtMs, kCrossGraceMs, acConfirmation.RecordedAtMs) &&
           !ServerClock::HasPassed(*m_endedAtMs, kCrossGraceMs, aNowMs);
}

/**
 * Tell whether the vote has been open for its whole timeout.
 *
 * @param aNowMs Server time now, in milliseconds.
 * @return True when the timeout has passed, or when the server's clock went back; false while no vote is open.
 */
bool SleepVote::HasRunOut(const uint64_t aNowMs) const noexcept
{
    return IsOpen() && ServerClock::HasPassed(m_openedAtMs, kTimeoutMs, aNowMs);
}

/**
 * Make a player wait kCooldownMs from now before confirming again.
 *
 * @param aPlayerId The player's server id.
 * @param aNowMs Server time now, in milliseconds.
 */
void SleepVote::StartCooldown(const uint32_t aPlayerId, const uint64_t aNowMs) noexcept
{
    m_cooldownSinceMs[aPlayerId] = aNowMs;
}
