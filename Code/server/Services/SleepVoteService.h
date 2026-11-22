#pragma once

#include <Events/PacketEvent.h>
#include <Messages/NotifyTimeSkip.h>
#include <Messages/RequestTimeSkip.h>
#include <RestingPlayers.h>
#include <SleepVote.h>

struct World;
struct Player;
struct UpdateEvent;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;

/**
 * Runs the shared time skip vote: when every connected player has confirmed the Sleep/Wait menu, the clock moves by the shortest length.
 * Every change is told to every connected player, one player's changes of length at most every 5 s; a vote nobody completes runs out after a minute.
 * A player who withdrew, or whose vote ran out, waits half a minute before confirming again.
 * With Gameplay:bEnableSleep off, each confirmation is refused and the clock never moves here.
 */
struct SleepVoteService
{
    /**
     * Listen for the players' steps, arrivals and departures, and for the server's ticks.
     *
     * @param aWorld The server's world, for the shared clock.
     * @param aDispatcher Where the server's events come from.
     */
    SleepVoteService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;

    TP_NOCOPYMOVE(SleepVoteService);

    /**
     * Tell whether a shared skip is under way: a vote open, or a game still resting an agreed skip.
     * A plugin's hour move waits meanwhile, so it neither cuts into a vote nor lands in a game's own rest.
     *
     * @return True while one is.
     */
    bool IsTimeSkipUnderWay() const noexcept;

private:
    /**
     * Apply one player's step: a confirmation, a withdrawal, the drop of a crossed confirmation, or the end of their game's own sleep.
     *
     * @param acMessage The step and the player who sent it.
     */
    void OnTimeSkipRequest(const PacketEvent<RequestTimeSkip>& acMessage) noexcept;

    /**
     * Show an arriving player the open vote, so they can join it.
     *
     * @param acEvent The player whose game has entered the world.
     */
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept;

    /**
     * Forget a departing player, their confirmation, wait and rest, then tell the others while a vote is open; their leaving can complete it.
     *
     * @param acEvent The player who disconnected, still listed by the server.
     */
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

    /**
     * Tell everyone the changes of length that were recorded quietly once they may be told, and end an open vote that has run out.
     *
     * @param acEvent The server's tick.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    /**
     * Record a player's confirmed length and tell everyone, or refuse it when sleep is switched off, the length is broken or the player still waits.
     * The same length again changes nothing, and a change of length soon after the last one is told later, from OnUpdate.
     *
     * @param aPlayer The player who pressed OK.
     * @param aHours The length they picked, in game hours.
     * @param acConnectedIds Server ids of every connected player.
     */
    void ConfirmVote(Player& aPlayer, float aHours, const std::vector<uint32_t>& acConnectedIds) noexcept;

    /**
     * Take a player's confirmation back and tell the others; nothing happens when they had not confirmed.
     * A withdrawal makes the player wait; the drop of a confirmation that crossed a vote's end does not, right after the end.
     *
     * @param acPlayer The player who closed the menu, entered combat, or whose game dropped a crossed confirmation.
     * @param aStep Which of those it was.
     * @param acConnectedIds Server ids of every connected player.
     */
    void WithdrawVote(const Player& acPlayer, RequestTimeSkip::Step aStep, const std::vector<uint32_t>& acConnectedIds) noexcept;

    /**
     * Tell a player who confirmed that the server does not share the time.
     *
     * @param aPlayer The player who pressed OK.
     */
    void RefuseWhileDisabled(Player& aPlayer) noexcept;

    /**
     * Tell a player who confirmed too soon after withdrawing, or after a vote ran out, how long they still wait.
     *
     * @param aPlayer The player who pressed OK.
     */
    void RefuseTooSoon(Player& aPlayer) noexcept;

    /**
     * After a change, skip the time when everyone has confirmed, end the vote when it ran out, or else tell everyone where it stands.
     *
     * @param apChanged The player whose step changed the vote; null when nobody's step did.
     * @param aChange What that player did.
     * @param acConnectedIds Server ids of every connected player.
     */
    void SettleVote(const Player* apChanged, NotifyTimeSkip::Change aChange, const std::vector<uint32_t>& acConnectedIds) noexcept;

    /**
     * Tell everyone to skip the shortest length, close the vote, then move the shared clock and resync it.
     *
     * @param acConnectedIds Server ids of every connected player.
     */
    void SkipAgreedHours(const std::vector<uint32_t>& acConnectedIds) noexcept;

    /**
     * Tell everyone the vote ran out and who did not confirm, then close it.
     *
     * @param acConnectedIds Server ids of every connected player.
     */
    void RunOutVote(const std::vector<uint32_t>& acConnectedIds) noexcept;

    /**
     * Build a notice of where the vote stands now.
     *
     * @param aEvent What happened to the vote.
     * @param apChanged The player whose step it was; null when nobody's.
     * @param aChange What that player did.
     * @param acConnectedIds Server ids of every connected player.
     * @return The notice, with the shortest length, the counts and the missing players' names.
     */
    NotifyTimeSkip BuildNotice(NotifyTimeSkip::Event aEvent, const Player* apChanged, NotifyTimeSkip::Change aChange, const std::vector<uint32_t>& acConnectedIds) const noexcept;

    World& m_world;
    SleepVote m_vote;
    RestingPlayers m_rests; // the games resting an agreed skip

    entt::scoped_connection m_requestConnection;
    entt::scoped_connection m_joinConnection;
    entt::scoped_connection m_leaveConnection;
    entt::scoped_connection m_updateConnection;
};
