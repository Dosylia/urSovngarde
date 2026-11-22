#pragma once

#include <Events/PacketEvent.h>
#include <Messages/NotifyHourMove.h>

struct World;
struct Player;
struct PlayerLeaveEvent;
struct RequestHourMove;
struct RequestCombatState;

/**
 * A plugin's move of the shared clock forward to an hour, as a wait does, and the combat reports its rules need.
 * Allowed with Gameplay:bEnableSleep on a private server, for a player in a party, or alone on the server, with nobody of the party in combat and no shared skip under way.
 * Every game in the world takes the move; only the player who asked hears a refusal.
 */
struct HourMoveService
{
    /**
     * Listen for hour moves, combat reports and departures.
     *
     * @param aWorld The server's world, for the clock, the parties and the sleep vote.
     * @param aDispatcher Where the server's events come from.
     */
    HourMoveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;

    TP_NOCOPYMOVE(HourMoveService);

private:
    /**
     * Move the shared clock to the hour asked for, or tell the asker why not.
     *
     * @param acMessage The hour and the player who asked.
     */
    void OnHourMoveRequest(const PacketEvent<RequestHourMove>& acMessage) noexcept;

    /**
     * Note whether a player fights, for the combat rule.
     *
     * @param acMessage The report and the player who sent it.
     */
    void OnCombatStateRequest(const PacketEvent<RequestCombatState>& acMessage) noexcept;

    /**
     * Forget a departing player's combat state.
     *
     * @param acEvent The player who disconnected.
     */
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;

    /**
     * Find the rule that refuses a player's hour move, in this order: switched off, public server, no party, a shared skip under way, a party member fighting.
     *
     * @param aAsker The player who asked.
     * @return The refusal for the asker; nothing when the move may go ahead.
     */
    std::optional<NotifyHourMove> FindRefusal(Player& aAsker) const noexcept;

    /**
     * Tell every game in the world the move, then move the shared clock, which resyncs them.
     *
     * @param acAsker The player who asked.
     * @param aHours How far to move, in game hours.
     */
    void MoveClock(const Player& acAsker, float aHours) noexcept;

    World& m_world;
    TiltedPhoques::Set<uint32_t> m_playersInCombat; // server ids of players whose game reports combat

    entt::scoped_connection m_hourMoveConnection;
    entt::scoped_connection m_combatConnection;
    entt::scoped_connection m_leaveConnection;
};
