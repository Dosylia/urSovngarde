#include <Services/CalendarService.h>
#include <Services/SleepTimeSync.h>
#include <Services/GameClockFrame.h>
#include <Helpers/PerfScope.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/RequestHourMove.h>
#include <Messages/RequestCombatState.h>
#include <Messages/NotifyHourMove.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Games/References.h>
#include <PlayerCharacter.h>
#include <TimeManager.h>
#include <Utils.h>

#include <Services/PartyService.h>
#include <Services/TransportService.h>

#include <cmath>

using GameClockFrame::kTransitionSpeed;

namespace
{
constexpr double kCombatCheckInterval = 0.5;

/**
 * Write a clock into the game's own date and hour.
 *
 * @param acTime The clock.
 */
void WriteGameClock(const TimeModel& acTime) noexcept
{
    auto* pGameTime = TimeData::Get();
    pGameTime->GameHour->f = acTime.Time;
    pGameTime->GameDay->f = static_cast<float>(acTime.Day);
    pGameTime->GameMonth->f = static_cast<float>(acTime.Month);
    pGameTime->GameYear->f = static_cast<float>(acTime.Year);
}

/**
 * Turn an answer to a plugin's hour move into the value plugins read.
 *
 * @param aEvent The answer.
 * @return Moved for a move or an hour already reached, else the refusal's reason.
 */
CalendarService::HourAnswer AnswerFor(const NotifyHourMove::Event aEvent) noexcept
{
    using Event = NotifyHourMove::Event;
    switch (aEvent)
    {
    case Event::kMoved:
    case Event::kAlreadyThere: return CalendarService::HourAnswer::kMoved;
    case Event::kRefusedDisabled: return CalendarService::HourAnswer::kSleepOff;
    case Event::kRefusedPublic: return CalendarService::HourAnswer::kPublic;
    case Event::kRefusedCombat: return CalendarService::HourAnswer::kCombat;
    case Event::kRefusedBusy: return CalendarService::HourAnswer::kBusy;
    case Event::kRefusedNoParty: return CalendarService::HourAnswer::kNoParty;
    }
    // An answer this build does not know reads as moved, as an hour already reached does.
    return CalendarService::HourAnswer::kMoved;
}

/**
 * Write an hour of the day as hh:mm, for the log.
 *
 * @param aTime The hour, from 0 to 24.
 * @return The text.
 */
String FormatHour(float aTime)
{
    const int cHour = static_cast<int>(aTime);
    const int cMinute = static_cast<int>((aTime - static_cast<float>(cHour)) * 60.f);
    return fmt::format("{:02}:{:02}", cHour, cMinute).c_str();
}

}

bool CalendarService::s_gameClockLocked = false;

/**
 * Tell the game's own time step whether it may move the clock: offline, or while a shared skip leaves the clock to the game.
 * Read by the game's time hook (TimeManager.cpp), on the game's thread.
 *
 * @return True when the game may move its own clock.
 */
bool CalendarService::AllowGameTick() noexcept
{
    // During a shared skip the game runs its own clock, so its hourly updates see the hours pass.
    return !s_gameClockLocked || s_sharedSkipRunning;
}

/**
 * Listen for the server's time, the frame's update, the end of the connection and a plugin's hour moves.
 *
 * @param aWorld The client's world, for its clock.
 * @param aDispatcher Where the client's events and messages come from.
 * @param aTransport The connection to the server.
 */
CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_transport(aTransport)
{
    m_timeUpdateConnection = aDispatcher.sink<ServerTimeSettings>().connect<&CalendarService::OnTimeUpdate>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::HandleUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&CalendarService::OnDisconnected>(this);
    m_hourMoveConnection = aDispatcher.sink<NotifyHourMove>().connect<&CalendarService::OnNotifyHourMove>(this);
}

/**
 * Take the server's time: the game's clock follows it from now on, with this player's own date when calendars are not shared.
 *
 * @param acMessage The server's time.
 */
void CalendarService::OnTimeUpdate(const ServerTimeSettings& acMessage) noexcept
{
    // disable the game clock
    ToggleGameClock(false);
    m_lastTick = m_world.GetTick();
    SleepTimeSync::ApplyServerTime(m_onlineTime, acMessage.timeModel, m_offlineTime.m_timeModel, m_world.GetServerSettings().SyncPlayerCalendar);
}

/**
 * Give the clock back to the game when the connection is lost, dropping any hour move or shared skip.
 *
 * @param acEvent The end of the connection.
 */
void CalendarService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    if (m_hourMove)
    {
        m_hourMove.reset();
        s_hourAnswer = HourAnswer::kLost;
        spdlog::info("Hour: a plugin's move of the hour was lost with the connection");
    }

    m_sharedSkip.Cancel();
    s_sharedSkipRunning = false;

    // signal a time transition
    m_fadeTimer = 0.f;
    ToggleGameClock(true);

    m_reportedCombat = false;
}

/**
 * Give a player's name for the log, from the party's list.
 *
 * @param aPlayerId The player's server id.
 * @return The name; "A party member" when the list does not know the player.
 */
String CalendarService::FindPartyMemberName(uint32_t aPlayerId) const noexcept
{
    const auto& cPlayers = m_world.GetPartyService().GetPlayers();
    const auto it = cPlayers.find(aPlayerId);
    return it != cPlayers.end() ? it->second : String("A party member");
}

/**
 * Follow a plugin's hour move: every game in the world takes the hours a move covered, and the asker hears how its ask went.
 *
 * @param acMessage The move, or why it was refused.
 */
void CalendarService::OnNotifyHourMove(const NotifyHourMove& acMessage) noexcept
{
    if (acMessage.MoveEvent == NotifyHourMove::Event::kMoved)
    {
        ApplyHourMove(acMessage.Hours);
        spdlog::info("Hour: {} moved the shared clock forward {:.2f} h to {}", FindPartyMemberName(acMessage.PlayerId).c_str(), acMessage.Hours,
                     FormatHour(m_onlineTime.m_timeModel.Time).c_str());
        // Another player's move is no answer to this game's own ask.
        if (acMessage.PlayerId != m_transport.GetLocalPlayerId())
            return;
    }

    if (!m_hourMove)
        return;

    m_hourMove.reset();
    s_hourAnswer = AnswerFor(acMessage.MoveEvent);
    spdlog::info("Hour: the server answered a plugin's move of the hour with {}", static_cast<int>(s_hourAnswer.load()));
}

/**
 * Take the hours the shared clock moved into this game's own date and days passed, before the server's resync.
 *
 * @param aHours How far the shared clock moved, in game hours.
 */
void CalendarService::ApplyHourMove(const float aHours) noexcept
{
    const auto cDays = SleepTimeSync::AdvancePersonalClock(m_onlineTime, aHours);
    if (!cDays)
        return;

    m_fadeTimer = kTransitionSpeed;
    if (s_gameClockLocked)
    {
        TimeData::Get()->GameDaysPassed->f += *cDays;
        // The resync that follows reads the date from these globals: published now, it keeps the new day.
        WriteGameClock(m_onlineTime.m_timeModel);
    }
}

/**
 * Ask the server to move the shared clock forward to the next time it is an hour, for a plugin; the answer comes in GetHourAnswer.
 * Main thread.
 *
 * @param aHour The hour, from 0 to 24.
 */
void CalendarService::MoveToHour(const float aHour) noexcept
{
    if (!m_transport.IsConnected())
    {
        s_hourAnswer = HourAnswer::kLost;
        return;
    }
    // One move at a time here, a shared skip's or another plugin's.
    if (m_hourMove || m_sharedSkip.IsRunning())
    {
        s_hourAnswer = HourAnswer::kBusy;
        spdlog::info("Hour: a plugin's move to {} not asked, a sleep or wait is under way here", FormatHour(std::fmod(aHour, 24.f)));
        return;
    }
    // The server checks the rest of the party; the game's own wait refuses this player in combat.
    if (const PlayerCharacter* pPlayer = PlayerCharacter::Get(); pPlayer && pPlayer->IsInCombat())
    {
        s_hourAnswer = HourAnswer::kCombat;
        spdlog::info("Hour: a plugin's move to {} not asked, this player is in combat", FormatHour(std::fmod(aHour, 24.f)));
        return;
    }

    // The server's rules for passing time hold, and it counts the hours on its own clock.
    m_hourMove = aHour;
    RequestHourMove ask{};
    ask.TargetHour = aHour;
    m_transport.Send(ask);
    spdlog::info("Hour: a plugin asked to move the hour to {}; asking the server", FormatHour(std::fmod(aHour, 24.f)));
}

/**
 * Let the game keep its own clock while it rests the hours every player agreed on.
 * The server's time still comes in meanwhile; the game's clock follows it again once the skip ends.
 *
 * @param aHours The agreed length, in game hours.
 */
void CalendarService::BeginSharedSkip(const float aHours) noexcept
{
    if (m_sharedSkip.IsRunning())
    {
        spdlog::warn("SleepVote: a skip of {} h agreed while the last one still runs here, ignored", aHours);
        return;
    }

    m_sharedSkip.Begin(m_onlineTime.m_timeModel, TimeData::Get()->GameDaysPassed->f, aHours, m_world.GetTick());
    s_sharedSkipRunning = true;
    spdlog::info("SleepVote: the game keeps its own clock while it rests {} h", aHours);
}

/**
 * End a shared skip: the days passed count the whole skip, this player's own date moves by it, and the game's clock follows the server's again.
 * Nothing happens when no skip runs.
 */
void CalendarService::EndSharedSkip() noexcept
{
    if (!m_sharedSkip.IsRunning())
        return;

    auto* pGameTime = TimeData::Get();
    const auto cResult = m_sharedSkip.End(pGameTime->GameDaysPassed->f, m_world.GetTick());
    s_sharedSkipRunning = false;
    pGameTime->GameDaysPassed->f = cResult.DaysPassed;

    // The server's resync came before the rest, with the date as it was then: a personal date takes the skip's own.
    // A shared calendar has the server's date already.
    if (!m_world.GetServerSettings().SyncPlayerCalendar)
    {
        const float cTimeScale = m_onlineTime.m_timeModel.TimeScale;
        m_onlineTime.m_timeModel = cResult.PersonalClock;
        m_onlineTime.m_timeModel.TimeScale = cTimeScale;
    }

    // Published at once, as at a sleep's end, so a resync that comes later keeps the new day.
    ToggleGameClock(false);
    WriteGameClock(m_onlineTime.m_timeModel);
    m_lastTick = m_world.GetTick();
    spdlog::info("SleepVote: the clock follows the server again, day {} of month {}, hour {:.2f}, {:.3f} days passed", m_onlineTime.m_timeModel.Day,
                 m_onlineTime.m_timeModel.Month, m_onlineTime.m_timeModel.Time, cResult.DaysPassed);
}

/**
 * End a shared skip whose rest never reported its end, after TimeSkipHours::kLongestRestSeconds.
 *
 * @param aNow The game's time now, in milliseconds.
 */
void CalendarService::EndRestThatRanTooLong(const uint64_t aNow) noexcept
{
    if (!m_sharedSkip.HasRunTooLong(aNow))
        return;

    spdlog::warn("SleepVote: the rest outlasted {} s; the clock follows the server again", TimeSkipHours::kLongestRestSeconds);
    EndSharedSkip();
}

/**
 * Report this player's combat state to a server that can move the hour for a plugin, when it changed.
 *
 * @param aDelta The frame's time, in seconds.
 */
void CalendarService::ReportCombat(double aDelta) noexcept
{
    m_combatCheckTimer -= aDelta;
    if (m_combatCheckTimer > 0.0)
        return;
    m_combatCheckTimer = kCombatCheckInterval;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    const bool cFighting = pPlayer && pPlayer->IsInCombat();

    // Only a server that can move the hour for a plugin needs combat reports.
    // If it stops granting them, a last report clears this player from its combat set, so re-enabling sleep cannot refuse it on a stale entry.
    const bool cInCombat = cFighting && m_world.GetServerSettings().SleepEnabled;
    if (cInCombat == m_reportedCombat)
        return;

    m_reportedCombat = cInCombat;

    RequestCombatState report{};
    report.InCombat = cInCombat;
    m_transport.Send(report);
}

/**
 * Hand the clock to the game, or take it from the game, keeping the game's date and hour now as its own clock for when it is offline.
 *
 * @param aEnable True to let the game move its clock, false to have the shared clock written into it.
 */
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

/**
 * Move the shared clock on and write it into the game's clock, except while a shared skip leaves the clock to the game.
 *
 * @param aEvent The frame's update.
 */
void CalendarService::HandleUpdate(const UpdateEvent& aEvent) noexcept
{
    PerfScope perfScope("CalendarService::HandleUpdate");

    if (m_transport.IsOnline())
        ReportCombat(aEvent.Delta);

    if (m_transport.IsOnline() || s_gameClockLocked)
    {
        const auto updateDelta = static_cast<float>(aEvent.Delta);
        auto* pGameTime = TimeData::Get();

        if (!m_lastTick)
            m_lastTick = m_world.GetTick();

        const auto now = m_world.GetTick();

        EndRestThatRanTooLong(now);

        // we got disconnected or the client got ahead of us
        if (now < m_lastTick)
            return;

        const auto delta = now - m_lastTick;
        m_lastTick = now;

        GameClockFrame::Globals globals{};
        globals.GameDaysPassed = pGameTime->GameDaysPassed->f;
        // Maintain the authoritative baseline even while a shared skip leaves the clock to the game.
        const bool cWritesGameClock = s_gameClockLocked && !m_sharedSkip.IsRunning();
        if (!GameClockFrame::Step(m_onlineTime, m_offlineTime.m_timeModel, delta, updateDelta, m_fadeTimer, cWritesGameClock, globals))
            return;
        pGameTime->TimeScale->f = globals.TimeScale;
        pGameTime->GameDay->f = globals.GameDay;
        pGameTime->GameMonth->f = globals.GameMonth;
        pGameTime->GameYear->f = globals.GameYear;
        pGameTime->GameDaysPassed->f = globals.GameDaysPassed;
        pGameTime->GameHour->f = globals.GameHour;
    }
}
