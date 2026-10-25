#pragma once

#include <DateTime.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct ServerTimeSettings;
struct NotifySleep;
struct DisconnectedEvent;
struct World;
struct UpdateEvent;
struct TransportService;

/**
 * @brief Handles time sync.
 */
class CalendarService final : public BSTEventSink<TESActivateEvent>
{
public:
    CalendarService(World&, entt::dispatcher&, TransportService&);

    static bool AllowGameTick() noexcept;
    // Blocks gameplay input while another party member sleeps; VR tracking continues normally.
    static bool AreSleepControlsLocked() noexcept { return s_sleepControlsLocked.load(); }
    // The game queued its Fader Menu, which a sleep does before the clock moves. Any thread.
    static void OnFaderQueued() noexcept { s_faderQueued = true; }

private:
    void OnTimeUpdate(const ServerTimeSettings&) noexcept;
    void HandleUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    void ToggleGameClock(bool aEnable);

    // Sleeping online: the server grants the sleep, the clock goes back to the game until it is done,
    // and the hours slept are sent on so the sleeper's party can watch and everybody's hour moves.
    void OnNotifySleep(const NotifySleep&) noexcept;
    void UpdateSleep(double aDelta) noexcept;
    void FinishSleep() noexcept;
    void RestoreOnlineClock() noexcept;
    float SleptHours(const TimeModel& acNow) const noexcept;
    void UpdateWatch(double aDelta) noexcept;
    void StopWatching(float aHours) noexcept;
    void CommitSleepTime(uint32_t aSleeperId, float aHours) noexcept;
    void ReportCombat(double aDelta) noexcept;
    String PlayerName(uint32_t aPlayerId) const noexcept;

    entt::scoped_connection m_timeUpdateConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_sleepConnection;

    enum class SleepState
    {
        kIdle,
        kAsking,
        kRefused,
        kSleeping,
        kFinishing, // waiting for the server's final time, including after cancellation
    };
    SleepState m_sleepState = SleepState::kIdle;
#ifdef SKYRIMVR
    uint64_t m_sleepMenuGeneration = 0;
#endif
    TimeModel m_sleepStart{};
    double m_sleepRealSeconds = 0.0;
    float m_sleepSentHours = 0.f;
    bool m_sleepAnnounced = false;
    float m_sleepLastHours = 0.f;
    double m_sleepStill = 0.0;
    double m_sleepGrace = 0.0;
    static inline std::atomic<bool> s_faderQueued{false};
    double m_sleepSendTimer = 0.0;
    double m_sleepHeartbeatTimer = 0.0;

    // Another party member's sleep: keep the world visible, with gameplay controls locked until it ends.
    std::optional<uint32_t> m_watchedSleeper;
    double m_watchHudTimer = 0.0;
    static inline std::atomic<bool> s_sleepControlsLocked{false};
    float m_watchHours = 0.f;
    double m_watchSilence = 0.0;

    bool m_reportedCombat = false;
    double m_combatCheckTimer = 0.0;

    DateTime m_onlineTime;
    DateTime m_offlineTime;
    float m_fadeTimer = 0.f;
    static bool s_gameClockLocked;

    uint64_t m_lastTick = 0;
    uint64_t m_lastLogTick = 0;
    World& m_world;
    TransportService& m_transport;
};
