#pragma once

#include <Events/PacketEvent.h>
#include <DateTime.h>
#include <Structs/GameId.h>

struct World;
struct UpdateEvent;
struct PlayerJoinEvent;
struct ServerTimeSettings;
struct Player;

/**
 * @brief Manages time and date of the world.
 */
class CalendarService
{
public:
    CalendarService(World& aWorld, entt::dispatcher& aDispatcher);

    // we use these types for SOL
    // this is done this way because SOL
    // provides direct support for these
    using TTime = std::pair<int, int>;
    using TDate = std::tuple<int, int, int>;

    /**
     * Tell whether strangers join this server: listed upstream or on the public server page.
     * Nobody, admins included, may move the hour for strangers there by a plugin.
     *
     * @return True when bAnnounceServer or bPublicStatus is on.
     */
    static bool IsOpenToStrangers() noexcept;

    /**
     * Tell whether a plugin's hour move can be granted: Gameplay:bEnableSleep on a server strangers do not join.
     * Clients report their combat state only then.
     *
     * @return True when the setting is on and the server is private.
     */
    static bool IsSleepAvailable() noexcept;

    /**
     * Tell whether the server lets players move the shared clock by sleeping or waiting (Gameplay:bEnableSleep).
     *
     * @return True when the setting is on, on any server.
     */
    static bool IsSleepEnabled() noexcept;

    /**
     * Move the shared clock and calendar forward, then send every loaded player the new time.
     *
     * @param aHours How far to move, in game hours.
     */
    void AdvanceHours(float aHours) noexcept;

    /**
     * Give the shared clock's hour now.
     *
     * @return The hour, from 0 to 24, with its fraction.
     */
    float GetCurrentHour() const noexcept;

    bool SetTime(int aHour, int aMinutes, float aScale) noexcept;
    bool SetDate(int aDay, int aMonth, float aYear) noexcept;

    // returns hours, minutes
    TTime GetTime() const noexcept;
    static TTime GetRealTime() noexcept;

    // returns dd/mm/yy
    TDate GetDate() const noexcept;

    float GetTimeScale() const noexcept { return m_dateTime.m_timeModel.TimeScale; }
    bool SetTimeScale(float aScale) noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent&) noexcept;
    ServerTimeSettings TimeResync() const noexcept;
    void SendTimeResync() noexcept;

    DateTime m_dateTime;
    uint64_t m_lastTick = 0;
    bool m_timeSetFromFirstPlayer = false;

    World& m_world;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
};
