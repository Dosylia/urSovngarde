#include <Services/CalendarService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/ServerTimeSettings.h>
#include <Messages/RequestSleep.h>
#include <Messages/NotifySleep.h>

#include "Game/Player.h"

#include <Setting.h>

extern Console::Setting<bool> bPublicStatus;
extern Console::Setting<bool> bAnnounceServer;

namespace
{
Console::Setting bEnableSleep{"Gameplay:bEnableSleep", "Lets a party member's sleep or wait move the clock forward for everybody, with their party waiting in place while it runs. Private servers only", true};

// A server strangers join: listed upstream or on the public server page. Nobody, admins included,
// may move the hour for strangers there.
bool IsPublicServer() noexcept
{
    return bAnnounceServer || bPublicStatus;
}
}

bool CalendarService::IsSleepAvailable() noexcept
{
    return bEnableSleep && !IsPublicServer();
}

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::OnUpdate>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&CalendarService::OnPlayerJoin>(this);
    m_leaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&CalendarService::OnPlayerLeave>(this);
    m_sleepConnection = aDispatcher.sink<PacketEvent<RequestSleep>>().connect<&CalendarService::OnSleepRequest>(this);
}

void CalendarService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    const uint32_t cId = acEvent.pPlayer->GetId();
    m_playersInCombat.erase(cId);

    // Roll back the audience's provisional hours and resync immediately.
    if (m_sleepSession.GetPlayerId() == cId)
    {
        spdlog::info("Sleep: player {:X} left while sleeping, waking their party", cId);
        EndSleep(0.f);
    }

    std::erase(m_sleepAudience, cId);
}

void CalendarService::OnSleepRequest(const PacketEvent<RequestSleep>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    const RequestSleep& cRequest = acMessage.Packet;
    ExpireSleep();

    switch (cRequest.SleepStep)
    {
    case RequestSleep::Step::kCombat:
        if (cRequest.InCombat)
            m_playersInCombat.insert(pPlayer->GetId());
        else
            m_playersInCombat.erase(pPlayer->GetId());
        return;
    case RequestSleep::Step::kAsk:
        AskToSleep(pPlayer);
        return;
    default: break;
    }

    const auto cHours = m_sleepSession.Accept(pPlayer->GetId(), cRequest, GameServer::Get()->GetTick());
    if (!cHours)
        return;

    switch (cRequest.SleepStep)
    {
    case RequestSleep::Step::kProgress: SendSleepProgress(*cHours); break;
    case RequestSleep::Step::kFinish: EndSleep(*cHours); break;
    case RequestSleep::Step::kCancel: EndSleep(0.f); break;
    case RequestSleep::Step::kHeartbeat: break;
    default: break;
    }
}

void CalendarService::AskToSleep(Player* apPlayer) noexcept
{
    NotifySleep answer{};
    answer.PlayerId = apPlayer->GetId();

    auto* pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    const bool cAlone = PlayerManager::Get()->Count() == 1;
    const bool cAllowed = pParty || cAlone;

    if (!bEnableSleep)
        answer.SleepEvent = NotifySleep::Event::kRefusedDisabled;
    else if (IsPublicServer())
        answer.SleepEvent = NotifySleep::Event::kRefusedPublic;
    else if (!cAllowed)
        answer.SleepEvent = NotifySleep::Event::kRefusedNotAllowed;
    else if (m_sleepSession.GetPlayerId() && m_sleepSession.GetPlayerId() != apPlayer->GetId())
    {
        answer.SleepEvent = NotifySleep::Event::kRefusedBusy;
        answer.PlayerId = *m_sleepSession.GetPlayerId();
    }
    else
    {
        answer.SleepEvent = NotifySleep::Event::kGranted;

        if (pParty)
        {
            for (const Player* pMember : pParty->Members)
            {
                if (pMember == apPlayer || !m_playersInCombat.contains(pMember->GetId()))
                    continue;

                answer.SleepEvent = NotifySleep::Event::kRefusedCombat;
                answer.PlayerId = pMember->GetId();
                break;
            }
        }
    }

    if (answer.SleepEvent == NotifySleep::Event::kGranted)
    {
        m_sleepSession.Start(apPlayer->GetId(), GameServer::Get()->GetTick());
        m_sleepProgressSent = false;
        m_sleepAudience.clear();
        if (pParty)
            for (const Player* pMember : pParty->Members)
                if (pMember != apPlayer)
                    m_sleepAudience.push_back(pMember->GetId());
    }

    apPlayer->Send(answer);
}

void CalendarService::SendSleepProgress(float aHours) noexcept
{
    if (!m_sleepSession.GetPlayerId())
        return;

    m_sleepProgressSent = true;

    NotifySleep notify{};
    notify.SleepEvent = NotifySleep::Event::kProgress;
    notify.PlayerId = *m_sleepSession.GetPlayerId();
    notify.HoursSlept = aHours;

    for (const uint32_t cId : m_sleepAudience)
        if (Player* pMember = PlayerManager::Get()->GetById(cId))
            pMember->Send(notify);
}

void CalendarService::EndSleep(float aHours) noexcept
{
    const auto cSleeperId = m_sleepSession.GetPlayerId();
    const bool cProgressSent = m_sleepProgressSent;
    const Vector<uint32_t> cAudience = std::move(m_sleepAudience);
    m_sleepSession.Reset();
    m_sleepAudience.clear();
    m_sleepProgressSent = false;

    NotifySleep finished{};
    finished.SleepEvent = NotifySleep::Event::kFinished;
    finished.PlayerId = cSleeperId.value_or(0);
    finished.HoursSlept = aHours;

    if (aHours > 0.f)
    {
        // The same players receive the resync below. Send the elapsed duration first so personal
        // calendars and GameDaysPassed advance too, without locking controls outside the party.
        if (cSleeperId)
            GameServer::Get()->SendToLoaded(finished);
        m_dateTime.AdvanceHours(aHours);
        SendTimeResync();
        return;
    }

    // A cancelled sleep, such as a wait menu opened and closed, moved no clock but the sleeper's and, once
    // progress was relayed, the party's. Only they roll back and resync; the shared hour never changed.
    const ServerTimeSettings cResync = TimeResync();
    if (cSleeperId)
        if (Player* pSleeper = PlayerManager::Get()->GetById(*cSleeperId))
            pSleeper->Send(cResync);

    if (!cProgressSent)
        return;

    for (const uint32_t cId : cAudience)
    {
        if (Player* pMember = PlayerManager::Get()->GetById(cId))
        {
            pMember->Send(finished);
            pMember->Send(cResync);
        }
    }
}

void CalendarService::ExpireSleep() noexcept
{
    if (!m_sleepSession.HasExpired(GameServer::Get()->GetTick()))
        return;

    const uint32_t cId = *m_sleepSession.GetPlayerId();
    if (Player* pPlayer = PlayerManager::Get()->GetById(cId))
    {
        NotifySleep revoked{};
        revoked.SleepEvent = NotifySleep::Event::kRevoked;
        revoked.PlayerId = cId;
        pPlayer->Send(revoked);
    }
    spdlog::warn("Sleep: player {:X} stopped renewing their grant, revoking it", cId);
    EndSleep(0.f);
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
    ExpireSleep();
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
