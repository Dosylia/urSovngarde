#pragma once

#include <Events/PacketEvent.h>
#include <DateTime.h>
#include <Structs/GameId.h>

struct World;
struct UpdateEvent;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;
struct RequestSleep;
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
    void OnPlayerLeave(const PlayerLeaveEvent&) noexcept;
    void SendTimeResync() noexcept;

    // A party member sleeping moves everybody's hour; their party fades out and watches the sleeper's clock.
    void OnSleepRequest(const PacketEvent<RequestSleep>&) noexcept;
    void AskToSleep(Player* apPlayer) noexcept;
    void SendToSleepAudience(float aHours, bool aFinished) const noexcept;
    void EndSleep(float aHours) noexcept;

    DateTime m_dateTime;
    uint64_t m_lastTick = 0;
    bool m_timeSetFromFirstPlayer = false;

    std::optional<uint32_t> m_sleeperId;
    Vector<uint32_t> m_sleepAudience; // the sleeper's party at the grant, sleeper excluded
    uint64_t m_lastSleepTick = 0;
    TiltedPhoques::Set<uint32_t> m_playersInCombat;

    World& m_world;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
    entt::scoped_connection m_leaveConnection;
    entt::scoped_connection m_sleepConnection;
};
