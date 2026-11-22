#pragma once

#include <DateTime.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Services/SharedSkipClock.h>

struct ServerTimeSettings;
struct NotifyHourMove;
struct DisconnectedEvent;
struct World;
struct UpdateEvent;
struct TransportService;

/**
 * The game's clock while online: it follows the server's shared clock, except while a shared time skip leaves it to the game as it rests.
 * Also moves the shared hour for a plugin and reports combat to a server that can.
 */
class CalendarService final : public BSTEventSink<TESActivateEvent>
{
public:
    /**
     * Listen for the server's time, the frame's update, the end of the connection and a plugin's hour moves.
     *
     * @param aWorld The client's world, for its clock.
     * @param aDispatcher Where the client's events and messages come from.
     * @param aTransport The connection to the server.
     */
    CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport);

    /**
     * Tell the game's own time step whether it may move the clock: offline, or while a shared skip leaves the clock to the game.
     * Read by the game's time hook (TimeManager.cpp), on the game's thread.
     *
     * @return True when the game may move its own clock.
     */
    static bool AllowGameTick() noexcept;

    /** How a plugin's move of the shared hour went; the values are a promise to plugins (urSovngarde_GetPartyHourAnswer). */
    enum class HourAnswer : int32_t
    {
        kWaiting = -1, // asked; the server has not answered yet
        kNotAsked = 0, // no move asked since the game started
        kMoved = 1,    // moved, or the hour was already there
        kSleepOff = 2, // the server does not share sleep (Gameplay:bEnableSleep off)
        kPublic = 3,   // the server is open to strangers
        kCombat = 4,   // a party member is in combat
        kBusy = 5,     // a shared time skip is under way, or another move is
        kNoParty = 6,  // the player has no party and others are connected
        kLost = 7,     // the connection was lost before the answer
    };

    /**
     * Give how the last plugin's move of the shared hour went. Any thread.
     *
     * @return The answer; kWaiting while the server has not answered.
     */
    static HourAnswer GetHourAnswer() noexcept { return s_hourAnswer.load(); }

    /**
     * Claim the answer for a new move, so each answer belongs to the move that asked. Any thread.
     *
     * @return False while an earlier move still waits for its answer.
     */
    static bool StartHourAsk() noexcept
    {
        HourAnswer answer = s_hourAnswer.load();
        return answer != HourAnswer::kWaiting && s_hourAnswer.compare_exchange_strong(answer, HourAnswer::kWaiting);
    }

    /**
     * Ask the server to move the shared clock forward to the next time it is an hour, for a plugin; the answer comes in GetHourAnswer.
     * Main thread.
     *
     * @param aHour The hour, from 0 to 24.
     */
    void MoveToHour(float aHour) noexcept;

    /**
     * Let the game keep its own clock while it rests the hours every player agreed on.
     * The server's time still comes in meanwhile; the game's clock follows it again once the skip ends.
     *
     * @param aHours The agreed length, in game hours.
     */
    void BeginSharedSkip(float aHours) noexcept;

    /**
     * End a shared skip: the days passed count the whole skip, this player's own date moves by it, and the game's clock follows the server's again.
     * Nothing happens when no skip runs.
     */
    void EndSharedSkip() noexcept;

    /**
     * Tell whether a shared skip runs.
     *
     * @return True between its start and its end.
     */
    bool IsSharedSkipRunning() const noexcept { return m_sharedSkip.IsRunning(); }

private:
    /**
     * Take the server's time: the game's clock follows it from now on, with this player's own date when calendars are not shared.
     *
     * @param acMessage The server's time.
     */
    void OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept;

    /**
     * Move the shared clock on and write it into the game's clock, except while a shared skip leaves the clock to the game.
     *
     * @param aEvent The frame's update.
     */
    void HandleUpdate(const UpdateEvent& aEvent) noexcept;

    /**
     * Give the clock back to the game when the connection is lost, dropping any hour move or shared skip.
     *
     * @param acEvent The end of the connection.
     */
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    /**
     * Give a player's name for the log, from the party's list.
     *
     * @param aPlayerId The player's server id.
     * @return The name; "A party member" when the list does not know the player.
     */
    String FindPartyMemberName(uint32_t aPlayerId) const noexcept;

    /**
     * End a shared skip whose rest never reported its end, after TimeSkipHours::kLongestRestSeconds.
     *
     * @param aNow The game's time now, in milliseconds.
     */
    void EndRestThatRanTooLong(uint64_t aNow) noexcept;

    /**
     * Hand the clock to the game, or take it from the game, keeping the game's date and hour now as its own clock for when it is offline.
     *
     * @param aEnable True to let the game move its clock, false to have the shared clock written into it.
     */
    void ToggleGameClock(bool aEnable);

    /**
     * Follow a plugin's hour move: every game in the world takes the hours a move covered, and the asker hears how its ask went.
     *
     * @param acMessage The move, or why it was refused.
     */
    void OnNotifyHourMove(const NotifyHourMove& acMessage) noexcept;

    /**
     * Take the hours the shared clock moved into this game's own date and days passed, before the server's resync.
     *
     * @param aHours How far the shared clock moved, in game hours.
     */
    void ApplyHourMove(float aHours) noexcept;

    /**
     * Report this player's combat state to a server that can move the hour for a plugin, when it changed.
     *
     * @param aDelta The frame's time, in seconds.
     */
    void ReportCombat(double aDelta) noexcept;

    entt::scoped_connection m_timeUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_hourMoveConnection;

    bool m_reportedCombat = false;
    double m_combatCheckTimer = 0.0;

    // The hour a plugin's move asked for, until the server answers.
    std::optional<float> m_hourMove;
    static inline std::atomic<HourAnswer> s_hourAnswer{HourAnswer::kNotAsked};

    // The game's own clock across a shared skip; s_sharedSkipRunning tells the game's clock hook, on the game's thread.
    SharedSkipClock m_sharedSkip;
    static inline std::atomic<bool> s_sharedSkipRunning{false};

    DateTime m_onlineTime;
    DateTime m_offlineTime;
    float m_fadeTimer = 0.f;
    static bool s_gameClockLocked;

    uint64_t m_lastTick = 0;
    uint64_t m_lastLogTick = 0;
    World& m_world;
    TransportService& m_transport;
};
