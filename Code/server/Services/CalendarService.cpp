#include <Services/CalendarService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/ServerTimeSettings.h>

#include "Game/Player.h"

#include <Setting.h>

extern Console::Setting<bool> bPublicStatus;
extern Console::Setting<bool> bAnnounceServer;

namespace
{
Console::Setting bEnableSleep{"Gameplay:bEnableSleep", "Lets players move the clock forward together: time skips once every connected player confirms the Sleep/Wait menu, and on private servers a plugin may move the hour", false};
}

/**
 * Tell whether strangers join this server: listed upstream or on the public server page.
 * Nobody, admins included, may move the hour for strangers there by a plugin.
 *
 * @return True when bAnnounceServer or bPublicStatus is on.
 */
bool CalendarService::IsOpenToStrangers() noexcept
{
    return bAnnounceServer || bPublicStatus;
}

/**
 * Tell whether a plugin's hour move can be granted: Gameplay:bEnableSleep on a server strangers do not join.
 * Clients report their combat state only then.
 *
 * @return True when the setting is on and the server is private.
 */
bool CalendarService::IsSleepAvailable() noexcept
{
    return bEnableSleep && !IsOpenToStrangers();
}

/**
 * Tell whether the server lets players move the shared clock by sleeping or waiting (Gameplay:bEnableSleep).
 *
 * @return True when the setting is on, on any server.
 */
bool CalendarService::IsSleepEnabled() noexcept
{
    return bEnableSleep;
}

/**
 * Move the shared clock and calendar forward, then send every loaded player the new time.
 *
 * @param aHours How far to move, in game hours.
 */
void CalendarService::AdvanceHours(const float aHours) noexcept
{
    m_dateTime.AdvanceHours(aHours);
    SendTimeResync();
}

/**
 * Give the shared clock's hour now.
 *
 * @return The hour, from 0 to 24, with its fraction.
 */
float CalendarService::GetCurrentHour() const noexcept
{
    return m_dateTime.m_timeModel.Time;
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::OnUpdate>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&CalendarService::OnPlayerJoin>(this);
}

void CalendarService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_lastTick)
        m_lastTick = GameServer::Get()->GetTick();

    auto now = GameServer::Get()->GetTick();

    // client got ahead, we wait
    if (now < m_lastTick)
        return;

    auto delta = now - m_lastTick;
    m_lastTick = now;
    m_dateTime.Update(delta);
}

void CalendarService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    ServerTimeSettings timeMsg;
    // the player with the furthest date is used
    bool playerHasFurthestTime = acEvent.PlayerTime.GetTimeInDays() > m_dateTime.GetTimeInDays();

    if (playerHasFurthestTime)
    {
        // Note that this doesn't set timescale because the server config should set that.
        if (!m_timeSetFromFirstPlayer)
        {
            m_dateTime.m_timeModel.Time = acEvent.PlayerTime.m_timeModel.Time;
            m_timeSetFromFirstPlayer = true;
        }
        m_dateTime.m_timeModel.Day = acEvent.PlayerTime.m_timeModel.Day;
        m_dateTime.m_timeModel.Month = acEvent.PlayerTime.m_timeModel.Month;
        m_dateTime.m_timeModel.Year = acEvent.PlayerTime.m_timeModel.Year;
    }
    timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
    timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
    timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
    timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
    timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;

    if (playerHasFurthestTime)
        GameServer::Get()->SendToPlayers(timeMsg);
    else
        acEvent.pPlayer->Send(timeMsg);
}

bool CalendarService::SetTime(int aHours, int aMinutes, float aScale) noexcept
{
    m_dateTime.m_timeModel.TimeScale = aScale;

    if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59)
    {
        // encode time as skyrim time
        auto minutes = static_cast<float>(aMinutes) * 0.17f;
        minutes = floor(minutes * 100) / 1000;
        m_dateTime.m_timeModel.Time = static_cast<float>(aHours) + minutes;

        SendTimeResync();

        GameServer::Get()->GetWorld().GetScriptService().HandleSetTime(aHours, aMinutes, aScale);
        return true;
    }
    return false;
}

bool CalendarService::SetDate(int aDay, int aMonth, float aYear) noexcept
{
    if (aMonth >= 0 && aMonth < 12 && aYear >= 0 && aYear <= 999)
    {
        auto maxDays = m_dateTime.GetNumberOfDaysByMonthIndex(aMonth);
        if (aDay >= 0 && aDay < maxDays)
        {
            m_dateTime.m_timeModel.Day = aDay;
            m_dateTime.m_timeModel.Month = aMonth;
            m_dateTime.m_timeModel.Year = aYear;
            SendTimeResync();
            return true;
        }
    }
    return false;
}

ServerTimeSettings CalendarService::TimeResync() const noexcept
{
    ServerTimeSettings timeMsg;
    timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
    timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
    timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
    timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
    timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;
    return timeMsg;
}

void CalendarService::SendTimeResync() noexcept
{
    GameServer::Get()->SendToLoaded(TimeResync());
}

CalendarService::TTime CalendarService::GetTime() const noexcept
{
    const auto hour = floor(m_dateTime.m_timeModel.Time);
    const auto minutes = (m_dateTime.m_timeModel.Time - hour) / 17.f;

    const auto flatMinutes = static_cast<int>(ceil((minutes * 100.f) * 10.f));
    return {static_cast<int>(hour), flatMinutes};
}

CalendarService::TTime CalendarService::GetRealTime() noexcept
{
    const auto t = std::time(nullptr);
    int h = (t / 3600) % 24;
    int m = (t / 60) % 60;
    return {h, m};
}

CalendarService::TDate CalendarService::GetDate() const noexcept
{
    return {m_dateTime.m_timeModel.Day, m_dateTime.m_timeModel.Month, m_dateTime.m_timeModel.Year};
}

bool CalendarService::SetTimeScale(float aScale) noexcept
{
    if (aScale >= 0.f && aScale <= 1000.f)
    {
        m_dateTime.m_timeModel.TimeScale = aScale;

        ServerTimeSettings timeMsg;
        timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
        timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
        timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
        timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
        timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;
        GameServer::Get()->SendToPlayers(timeMsg);
        return true;
    }

    return false;
}
