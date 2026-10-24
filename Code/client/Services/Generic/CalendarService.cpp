#include <Services/CalendarService.h>
#include <Services/SleepTimeSync.h>
#include <Services/GameClockFrame.h>
#include <PerfScope.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/RequestSleep.h>
#include <Messages/NotifySleep.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Games/References.h>
#include <Interface/UI.h>
#include <Interface/Menus/SleepWaitMenu.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>
#include <Utils.h>

#include <Services/PartyService.h>
#include <Services/TransportService.h>

using GameClockFrame::kTransitionSpeed;

namespace
{
// A menu closed before the clock moved keeps it free this long, in case the fast-forward is still to come.
constexpr double kSleepGrace = 3.0;
// The sleep is over once the hours slept have grown less than this for this long.
constexpr float kSleepMovingStep = 0.01f;
constexpr double kSleepStillTime = 0.5;
constexpr double kWatchHudInterval = 2.5;
// Less than a game minute slept is a menu opened and cancelled.
constexpr float kMinSleepHours = 1.f / 60.f;
// Progress goes out at most this often, and only once the clock has moved this far.
constexpr double kSleepSendInterval = 0.2;
constexpr double kSleepHeartbeatInterval = 5.0;
constexpr float kSleepSendStep = 0.1f;
// A watcher that hears nothing for this long regains controls on its own.
constexpr double kWatchTimeout = 15.0;
constexpr double kCombatCheckInterval = 0.5;

bool IsSleepMenuOpen() noexcept
{
    UI* pUI = UI::Get();
    // Sleeping and waiting both run through this menu.
    // Checked every frame; interning the name once avoids a string-cache lookup per call.
    static const BSFixedString s_name("Sleep/Wait Menu");
    return pUI && pUI->GetMenuOpen(s_name);
}

TimeModel ReadGameClock() noexcept
{
    auto* pGameTime = TimeData::Get();

    TimeModel model{};
    model.TimeScale = pGameTime->TimeScale->f;
    model.Time = pGameTime->GameHour->f;
    model.Day = static_cast<uint32_t>(pGameTime->GameDay->f);
    model.Month = static_cast<uint32_t>(pGameTime->GameMonth->f);
    model.Year = static_cast<uint32_t>(pGameTime->GameYear->f);
    return model;
}

void WriteGameClock(const TimeModel& acTime) noexcept
{
    auto* pGameTime = TimeData::Get();
    pGameTime->GameHour->f = acTime.Time;
    pGameTime->GameDay->f = static_cast<float>(acTime.Day);
    pGameTime->GameMonth->f = static_cast<float>(acTime.Month);
    pGameTime->GameYear->f = static_cast<float>(acTime.Year);
}

String FormatHour(float aTime)
{
    const int cHour = static_cast<int>(aTime);
    const int cMinute = static_cast<int>((aTime - static_cast<float>(cHour)) * 60.f);
    return fmt::format("{:02}:{:02}", cHour, cMinute).c_str();
}

}

bool CalendarService::s_gameClockLocked = false;

bool CalendarService::AllowGameTick() noexcept
{
    return !s_gameClockLocked;
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_transport(aTransport)
{
    m_timeUpdateConnection = aDispatcher.sink<ServerTimeSettings>().connect<&CalendarService::OnTimeUpdate>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::HandleUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CalendarService::OnDisconnected>(this);
    m_sleepConnection = aDispatcher.sink<NotifySleep>().connect<&CalendarService::OnNotifySleep>(this);
}

void CalendarService::OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept
{
    // A join or admin change can resync the server while a sleep is in progress. Keep the native clock
    // and provisional audience progress until the session ends; every end path sends a fresh resync.
    if (m_sleepState == SleepState::kSleeping || m_watchedSleeper)
        return;

    if (m_sleepState == SleepState::kFinishing)
        m_sleepState = SleepState::kIdle;

    // disable the game clock
    ToggleGameClock(false);
    m_lastTick = m_world.GetTick();
    SleepTimeSync::ApplyServerTime(m_onlineTime, acMessage.timeModel, m_offlineTime.m_timeModel, m_world.GetServerSettings().SyncPlayerCalendar);
}

void CalendarService::OnDisconnected(const DisconnectedEvent&) noexcept
{
#ifdef SKYRIMVR
    ResolveDeferredSleepWait(false);
#endif
    if (m_watchedSleeper)
        StopWatching(0.f);

    // signal a time transition
    m_fadeTimer = 0.f;
    ToggleGameClock(true);

    s_sleepControlsLocked = false;
    m_watchedSleeper.reset();
    m_sleepState = SleepState::kIdle;
    m_reportedCombat = false;
}

String CalendarService::PlayerName(uint32_t aPlayerId) const noexcept
{
    const auto& cPlayers = m_world.GetPartyService().GetPlayers();
    const auto it = cPlayers.find(aPlayerId);
    return it != cPlayers.end() ? it->second : String("A party member");
}

void CalendarService::OnNotifySleep(const NotifySleep& acMessage) noexcept
{
    using Event = NotifySleep::Event;

    switch (acMessage.SleepEvent)
    {
    case Event::kGranted:
        if (m_sleepState != SleepState::kAsking || !IsSleepMenuOpen()
#ifdef SKYRIMVR
            || m_sleepMenuGeneration != SleepWaitMenuGeneration()
#endif
        )
        {
            // The menu closed before the answer came.
            RequestSleep cancel{};
            cancel.SleepStep = RequestSleep::Step::kCancel;
            m_transport.Send(cancel);
#ifdef SKYRIMVR
            // Await this grant's cancellation resync before asking for the replacement menu.
            m_sleepState = SleepState::kFinishing;
#endif
            return;
        }

        m_sleepStart = ReadGameClock();
        m_sleepRealSeconds = 0.0;
        m_sleepSentHours = 0.f;
        m_sleepAnnounced = false;
        m_sleepLastHours = 0.f;
        m_sleepStill = 0.0;
        m_sleepGrace = kSleepGrace;
        m_sleepSendTimer = 0.0;
        m_sleepHeartbeatTimer = 0.0;
        s_faderQueued = false;
        m_sleepState = SleepState::kSleeping;
        ToggleGameClock(true);
#ifdef SKYRIMVR
        ResolveDeferredSleepWait(true);
#endif
        return;
    case Event::kRevoked:
#ifdef SKYRIMVR
        ResolveDeferredSleepWait(false);
#endif
        if (m_sleepState == SleepState::kSleeping || m_sleepState == SleepState::kFinishing)
        {
            // The server discarded this session. Restore the continuously maintained baseline before
            // its resync, including the personal date when calendar synchronization is disabled.
            RestoreOnlineClock();
            ToggleGameClock(false);
        }
        m_sleepState = IsSleepMenuOpen() ? SleepState::kRefused : SleepState::kIdle;
        Utils::ShowHudMessage("urSovngarde: sleep session expired, reopen the sleep/wait menu to try again");
        return;
    case Event::kRefusedDisabled:
    case Event::kRefusedPublic:
    case Event::kRefusedNotAllowed:
    case Event::kRefusedBusy:
    case Event::kRefusedCombat:
    {
        if (m_sleepState == SleepState::kAsking)
            m_sleepState = SleepState::kRefused;
#ifdef SKYRIMVR
        ResolveDeferredSleepWait(false);
#endif

        String reason;
        if (acMessage.SleepEvent == Event::kRefusedDisabled)
            reason = "sleeping does not pass time on this server";
        else if (acMessage.SleepEvent == Event::kRefusedPublic)
            reason = "sleeping only passes time on private servers";
        else if (acMessage.SleepEvent == Event::kRefusedNotAllowed)
            reason = "join a party to sleep, the hour is shared";
        else if (acMessage.SleepEvent == Event::kRefusedBusy)
            reason = PlayerName(acMessage.PlayerId) + " is already sleeping";
        else
            reason = PlayerName(acMessage.PlayerId) + " is in combat";

        spdlog::info("Sleep: refused, {}", reason);
        Utils::ShowHudMessage(String("urSovngarde: no time will pass, ") + reason);
        return;
    }
    case Event::kProgress:
        if (!m_watchedSleeper)
        {
            m_watchedSleeper = acMessage.PlayerId;
            m_watchHudTimer = 0.0;
            m_watchHours = 0.f;
            s_sleepControlsLocked = true;
        }

        if (m_watchedSleeper != acMessage.PlayerId)
            return;

        m_watchSilence = 0.0;
        if (acMessage.HoursSlept > m_watchHours)
        {
            // Our clock kept running too, so only the hours slept on top of it are added.
            const float cStep = acMessage.HoursSlept - m_watchHours;
            m_watchHours = acMessage.HoursSlept;
            m_onlineTime.AdvanceHours(cStep);
            m_fadeTimer = kTransitionSpeed;
            if (s_gameClockLocked)
                TimeData::Get()->GameDaysPassed->f += cStep / 24.f;
        }

        // The game shows each HUD line for about 2 s and queues the rest, and a sleep runs at about an hour a second.
        if (m_watchHudTimer <= 0.0)
        {
            m_watchHudTimer = kWatchHudInterval;
            Utils::ShowHudMessage(String("urSovngarde: ") + PlayerName(acMessage.PlayerId) + " is sleeping, " + FormatHour(m_onlineTime.m_timeModel.Time));
        }
        return;
    case Event::kFinished:
        CommitSleepTime(acMessage.PlayerId, acMessage.HoursSlept);
        return;
    }
}

void CalendarService::StopWatching(float aHours) noexcept
{
    CommitSleepTime(*m_watchedSleeper, aHours);
}

void CalendarService::CommitSleepTime(uint32_t aSleeperId, float aHours) noexcept
{
    // Progress is provisional until the finish. Undo it on cancellation/disconnect/timeout, including
    // midnight and GameDaysPassed. Players outside the party have no progress to subtract, so their
    // personal calendar advances by the full duration, even with bSyncPlayerCalendar disabled.
    const auto cDays = SleepTimeSync::CommitSleep(m_onlineTime, m_transport.GetLocalPlayerId(), aSleeperId, aHours, m_watchedSleeper, m_watchHours);
    if (!cDays)
        return;

    m_fadeTimer = kTransitionSpeed;
    if (s_gameClockLocked)
    {
        TimeData::Get()->GameDaysPassed->f += *cDays;
        // The following resync snapshots these globals. Publish the new date now so it cannot put
        // 06:00 back on the old day; it must also preserve a 24-hour sleep with the same final hour.
        WriteGameClock(m_onlineTime.m_timeModel);
    }

    if (m_watchedSleeper != aSleeperId)
        return;

    if (aHours >= kMinSleepHours)
        Utils::ShowHudMessage(String("urSovngarde: ") + PlayerName(*m_watchedSleeper) + " woke up, " + FormatHour(m_onlineTime.m_timeModel.Time));

    // The server's resync follows with the shared clock and corrects anything left over.
    m_watchedSleeper.reset();
    s_sleepControlsLocked = false;
}

void CalendarService::UpdateWatch(double aDelta) noexcept
{
    if (!m_watchedSleeper)
        return;

    m_watchHudTimer -= aDelta;
    m_watchSilence += aDelta;
    if (m_watchSilence >= kWatchTimeout)
    {
        spdlog::warn("Sleep: nothing from player {:X} for {:.0f} s, releasing sleep controls", *m_watchedSleeper, kWatchTimeout);
        StopWatching(0.f);
    }
}

void CalendarService::UpdateSleep(double aDelta) noexcept
{
    bool cMenuOpen = IsSleepMenuOpen();
#ifdef SKYRIMVR
    if (m_sleepState != SleepState::kIdle && m_sleepMenuGeneration != SleepWaitMenuGeneration())
    {
        // A close/reopen can happen between VM updates, even reusing the same IMenu address.
        // Drain the old answer before asking again; its grant must not start the new confirmation.
        if (m_sleepState == SleepState::kAsking)
            return;
        cMenuOpen = false;
    }
#endif

    switch (m_sleepState)
    {
    case SleepState::kIdle:
        if (cMenuOpen)
        {
#ifdef SKYRIMVR
            m_sleepMenuGeneration = SleepWaitMenuGeneration();
#endif
            RequestSleep ask{};
            ask.SleepStep = RequestSleep::Step::kAsk;
            m_transport.Send(ask);
            m_sleepState = SleepState::kAsking;
        }
        return;
    case SleepState::kAsking:
#ifndef SKYRIMVR
        if (!cMenuOpen)
        {
            RequestSleep cancel{};
            cancel.SleepStep = RequestSleep::Step::kCancel;
            m_transport.Send(cancel);
            m_sleepState = SleepState::kIdle;
        }
#endif
        // VR drains the outstanding reply even after closing: a late grant is cancelled and resynced
        // in OnNotifySleep before the replacement menu may request its own grant.
        return;
    case SleepState::kRefused:
        if (!cMenuOpen)
            m_sleepState = SleepState::kIdle;
        return;
    case SleepState::kFinishing: return;
    case SleepState::kSleeping: break;
    }

    m_sleepRealSeconds += aDelta;
    m_sleepHeartbeatTimer -= aDelta;
    if (m_sleepHeartbeatTimer <= 0.0)
    {
        RequestSleep heartbeat{};
        heartbeat.SleepStep = RequestSleep::Step::kHeartbeat;
        m_transport.Send(heartbeat);
        m_sleepHeartbeatTimer = kSleepHeartbeatInterval;
    }
    const float cHours = SleptHours(ReadGameClock());

    // The native sleeper fades before the clock moves; lock the party's controls from that point.
    const bool cFaderQueued = s_faderQueued.exchange(false);
    m_sleepSendTimer -= aDelta;
    if ((cFaderQueued && !m_sleepAnnounced) || (m_sleepSendTimer <= 0.0 && cHours - m_sleepSentHours >= kSleepSendStep))
    {
        m_sleepAnnounced = true;
        RequestSleep progress{};
        progress.SleepStep = RequestSleep::Step::kProgress;
        progress.HoursSlept = cHours;
        m_transport.Send(progress);

        m_sleepSentHours = cHours;
        m_sleepSendTimer = kSleepSendInterval;
    }

    if (cHours - m_sleepLastHours > kSleepMovingStep)
    {
        m_sleepLastHours = cHours;
        m_sleepStill = 0.0;
    }
    else
        m_sleepStill += aDelta;

    if (cMenuOpen)
    {
        m_sleepGrace = kSleepGrace;
        return;
    }

    // Done once the clock has moved and then stopped; a menu closed with no movement waits for the fast-forward.
    m_sleepGrace -= aDelta;
    if ((cHours >= kMinSleepHours && m_sleepStill >= kSleepStillTime) || m_sleepGrace <= 0.0)
        FinishSleep();
}

float CalendarService::SleptHours(const TimeModel& acNow) const noexcept
{
    // The server's clock ran on in real time meanwhile, so only the game time beyond that was slept.
    const double cGameHours = (DateTime(acNow).GetTimeInDays() - DateTime(m_sleepStart).GetTimeInDays()) * 24.0;
    const double cRealHours = m_sleepRealSeconds * m_sleepStart.TimeScale / 3600.0;
    return static_cast<float>(std::max(0.0, cGameHours - cRealHours));
}

void CalendarService::RestoreOnlineClock() noexcept
{
    const double cCorrectionDays = m_onlineTime.GetTimeInDays() - DateTime(ReadGameClock()).GetTimeInDays();
    TimeData::Get()->GameDaysPassed->f += static_cast<float>(cCorrectionDays);
    WriteGameClock(m_onlineTime.m_timeModel);
}

void CalendarService::FinishSleep() noexcept
{
    const float cHours = SleptHours(ReadGameClock());
    m_sleepState = SleepState::kFinishing;
    if (cHours < kMinSleepHours)
        RestoreOnlineClock();

    // Leave the displayed clock with the game until the final resync so it does not jump back during
    // the round trip. Keep m_onlineTime as the baseline in case the server revokes this grant instead.

    RequestSleep done{};
    done.SleepStep = cHours >= kMinSleepHours ? RequestSleep::Step::kFinish : RequestSleep::Step::kCancel;
    done.HoursSlept = cHours;
    m_transport.Send(done);
}

void CalendarService::ReportCombat(double aDelta) noexcept
{
    m_combatCheckTimer -= aDelta;
    if (m_combatCheckTimer > 0.0)
        return;
    m_combatCheckTimer = kCombatCheckInterval;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const bool cFighting = pPlayer && pPlayer->IsInCombat();
    // The server only checks the party for combat at the ask. A watcher attacked later gets their controls
    // back but keeps watching, so the sleep's hours still arrive once, at the finish.
    if (cFighting && s_sleepControlsLocked)
    {
        s_sleepControlsLocked = false;
        Utils::ShowHudMessage("urSovngarde: in combat, controls released while " + PlayerName(*m_watchedSleeper) + " sleeps");
    }

    // Only a server that can grant a sleep needs combat reports. If it stops granting them, a last report
    // clears this player from its combat set, so re-enabling sleep cannot refuse it on a stale entry.
    const bool cInCombat = cFighting && m_world.GetServerSettings().SleepEnabled;
    if (cInCombat == m_reportedCombat)
        return;

    m_reportedCombat = cInCombat;

    RequestSleep report{};
    report.SleepStep = RequestSleep::Step::kCombat;
    report.InCombat = cInCombat;
    m_transport.Send(report);
}

void CalendarService::ToggleGameClock(bool aEnable)
{
    auto* pGameTime = TimeData::Get();
    m_offlineTime.m_timeModel.Day = pGameTime->GameDay->f;
    m_offlineTime.m_timeModel.Month = pGameTime->GameMonth->f;
    m_offlineTime.m_timeModel.Year = pGameTime->GameYear->f;
    m_offlineTime.m_timeModel.Time = pGameTime->GameHour->f;
    m_offlineTime.m_timeModel.TimeScale = pGameTime->TimeScale->f;

    s_gameClockLocked = !aEnable;
}

void CalendarService::HandleUpdate(const UpdateEvent& aEvent) noexcept
{
    PerfScope perfScope("CalendarService::HandleUpdate");

    if (m_transport.IsOnline())
    {
        ReportCombat(aEvent.Delta);
        UpdateSleep(aEvent.Delta);
        UpdateWatch(aEvent.Delta);
    }

    if (m_transport.IsOnline() || s_gameClockLocked)
    {
        const auto updateDelta = static_cast<float>(aEvent.Delta);
        auto* pGameTime = TimeData::Get();

        if (!m_lastTick)
            m_lastTick = m_world.GetTick();

        const auto now = m_world.GetTick();

        // we got disconnected or the client got ahead of us
        if (now < m_lastTick)
            return;

        const auto delta = now - m_lastTick;
        m_lastTick = now;

        GameClockFrame::Globals globals{};
        globals.GameDaysPassed = pGameTime->GameDaysPassed->f;
        // Maintain the authoritative baseline even while a granted sleeper owns the native clock.
        if (!GameClockFrame::Step(m_onlineTime, m_offlineTime.m_timeModel, delta, updateDelta, m_fadeTimer, s_gameClockLocked, globals))
            return;
        pGameTime->TimeScale->f = globals.TimeScale;
        pGameTime->GameDay->f = globals.GameDay;
        pGameTime->GameMonth->f = globals.GameMonth;
        pGameTime->GameYear->f = globals.GameYear;
        pGameTime->GameDaysPassed->f = globals.GameDaysPassed;
        pGameTime->GameHour->f = globals.GameHour;
    }
}
