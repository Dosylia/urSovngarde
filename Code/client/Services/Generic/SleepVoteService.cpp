#include <Services/SleepVoteService.h>

#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Interface/Menus/SleepWaitOk.h>
#include <Interface/Menus/SleepWaitVoteLine.h>
#include <Messages/NotifyTimeSkip.h>
#include <Messages/ServerTimeSettings.h>
#include <Services/CalendarService.h>
#include <Services/LocalSleepVote.h>
#include <Services/TransportService.h>
#include <NameList.h>
#include <PlayerCharacter.h>
#include <Utils.h>
#include <WholeSeconds.h>
#include <World.h>

namespace
{
using Change = NotifyTimeSkip::Change;
using Phase = LocalSleepVote::Phase;

/** Longest name, in bytes, the Sleep/Wait menu's line keeps whole; a longer one is cut, so the line stays on the question's one line. */
constexpr size_t kMenuNameBytes = 20;

/** What ends a name that was cut. */
constexpr std::string_view kCutMark = "...";

/** A UTF-8 continuation byte, 10xxxxxx, never starts a character: its top two bits, and their value. */
constexpr unsigned char kUtf8ContinuationMask = 0xC0;
constexpr unsigned char kUtf8ContinuationBits = 0x80;

/**
 * Cut a name that is too long for the menu's line, at a whole UTF-8 character, and mark the cut.
 *
 * @param acName The name.
 * @return The name whole when it fits, else its start and the cut mark.
 */
std::string CutForMenu(const std::string& acName)
{
    if (acName.size() <= kMenuNameBytes)
        return acName;

    size_t end = kMenuNameBytes - kCutMark.size();
    while (end > 0 && (static_cast<unsigned char>(acName[end]) & kUtf8ContinuationMask) == kUtf8ContinuationBits)
        --end;
    return acName.substr(0, end) + std::string(kCutMark);
}

/**
 * Say whom the vote waits for, short enough for one line of the Sleep/Wait menu's question field.
 *
 * @param acMessage Where the vote stands.
 * @return One name, or how many players, with the count.
 */
std::string DescribeWaitingForMenu(const NotifyTimeSkip& acMessage)
{
    const auto& cMissing = acMessage.MissingPlayerNames;
    const std::string cWho = cMissing.size() == 1 ? CutForMenu(cMissing.front().c_str()) : fmt::format("{} players", cMissing.size());
    return fmt::format("Waiting for {} ({}/{})", cWho, acMessage.ReadyCount, acMessage.RequiredCount);
}

/**
 * Say how far the vote has got, short enough for one line of the Sleep/Wait menu's question field, for a player who is not in it.
 *
 * @param acMessage Where the vote stands.
 * @return The count and the length.
 */
std::string DescribeVoteForMenu(const NotifyTimeSkip& acMessage)
{
    return fmt::format("{}/{} ready to rest {} h", acMessage.ReadyCount, acMessage.RequiredCount, acMessage.Hours);
}

/**
 * Say what one player did to the vote, for a player who is not in it.
 *
 * @param acMessage Where the vote stands.
 * @return The line, in players' words.
 */
std::string DescribeChangeForHud(const NotifyTimeSkip& acMessage)
{
    const char* pName = acMessage.ChangedPlayerName.c_str();
    const uint32_t cReady = acMessage.ReadyCount;
    const uint32_t cRequired = acMessage.RequiredCount;

    switch (acMessage.PlayerChange)
    {
    case Change::kConfirmed: return fmt::format("{} is ready for a {} h rest ({}/{}), sleep or wait to join", pName, acMessage.Hours, cReady, cRequired);
    case Change::kWithdrew: return fmt::format("{} stopped resting ({}/{})", pName, cReady, cRequired);
    case Change::kWithdrewCombat: return fmt::format("{} is in combat and stopped resting ({}/{})", pName, cReady, cRequired);
    case Change::kLeft: return fmt::format("{} left ({}/{} ready to rest)", pName, cReady, cRequired);
    default: return fmt::format("{}/{} ready for a {} h rest, sleep or wait to join", cReady, cRequired, acMessage.Hours);
    }
}
} // namespace

/**
 * Listen for the frame's update, the server's vote notices and time resyncs, and the end of the connection.
 *
 * @param aDispatcher Where the client's events and messages come from.
 * @param aTransport The connection to the server.
 * @param aCalendar The game's clock, which a shared skip leaves to the game while it rests.
 */
SleepVoteService::SleepVoteService(entt::dispatcher& aDispatcher, TransportService& aTransport, CalendarService& aCalendar) noexcept
    : m_transport(aTransport)
    , m_calendar(aCalendar)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SleepVoteService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&SleepVoteService::OnDisconnected>(this);
    m_noticeConnection = aDispatcher.sink<NotifyTimeSkip>().connect<&SleepVoteService::OnNotifyTimeSkip>(this);
    m_resyncConnection = aDispatcher.sink<ServerTimeSettings>().connect<&SleepVoteService::OnTimeResync>(this);
}

/**
 * Take the vote back when its menu closed or combat began, send a pressed OK, report the end of the game's own rest, and follow the wait after a refusal.
 *
 * @param acEvent The frame's update.
 */
void SleepVoteService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsOnline())
        return;

    // Withdrawn first, so a press made in combat or as the menu closed is never sent only to be taken back.
    WithdrawWhenMenuGoneOrFighting();
    SendPress();
    NoteRestOver();
    EndSkipOnceRested();
    FollowWaitAfterRefusal();
}

/**
 * Drop this game's vote with the connection.
 *
 * @param acEvent The end of the connection.
 */
void SleepVoteService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    SleepWaitOk::GetLocalVote().Reset();
    m_voteLine.clear();
    m_waitLine.clear();
    RefreshMenuLine();
    m_awaitingResync = false;
}

/**
 * Follow the vote: show each change, start the agreed skip, release a vote that ran out, or rest alone where time is not shared.
 *
 * @param acMessage Where the vote stands.
 */
void SleepVoteService::OnNotifyTimeSkip(const NotifyTimeSkip& acMessage) noexcept
{
    switch (acMessage.SkipEvent)
    {
    case NotifyTimeSkip::Event::kVoteChanged: ShowVoteChange(acMessage); break;
    case NotifyTimeSkip::Event::kAgreed: StartAgreedSkip(acMessage); break;
    case NotifyTimeSkip::Event::kTimedOut: ReleaseRunOutVote(acMessage); break;
    case NotifyTimeSkip::Event::kDisabled: RestAlone(); break;
    case NotifyTimeSkip::Event::kTooSoon: RefuseTooSoon(acMessage); break;
    default: spdlog::warn("SleepVote: an unknown notice from the server, ignored"); break;
    }
}

/**
 * Note the first time resync after an agreed skip, for the log.
 *
 * @param acMessage The server's time.
 */
void SleepVoteService::OnTimeResync(const ServerTimeSettings& acMessage) noexcept
{
    if (!m_awaitingResync)
        return;

    m_awaitingResync = false;
    spdlog::info("SleepVote: time resync after the skip, hour {:.2f}", acMessage.timeModel.Time);
}

/**
 * Send a pressed OK as this player's confirmation.
 */
void SleepVoteService::SendPress() noexcept
{
    const auto cHours = SleepWaitOk::GetLocalVote().TakePress();
    if (!cHours)
        return;

    SendStep(RequestTimeSkip::Step::kConfirm, *cHours);
    spdlog::info("SleepVote: confirmed {} h, waiting for the others", *cHours);
}

/**
 * Take the vote back when its menu is gone or the player is in combat, and tell the server when it holds the confirmation.
 */
void SleepVoteService::WithdrawWhenMenuGoneOrFighting() noexcept
{
    LocalSleepVote& vote = SleepWaitOk::GetLocalVote();
    if (!vote.IsVoting())
        return;

    if (vote.IsMenuGone(SleepWaitOk::FindOpenMenu()))
    {
        if (vote.Withdraw())
            SendStep(RequestTimeSkip::Step::kWithdraw, 0.f);
        spdlog::info("SleepVote: withdrew, the menu closed");
        return;
    }

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->IsInCombat())
    {
        if (vote.Withdraw())
            SendStep(RequestTimeSkip::Step::kWithdrawCombat, 0.f);
        spdlog::info("SleepVote: withdrew, in combat");
    }
}

/**
 * Note that the game's own rest is over, once the game no longer sleeps or waits.
 */
void SleepVoteService::NoteRestOver() noexcept
{
    LocalSleepVote& vote = SleepWaitOk::GetLocalVote();
    if (vote.GetPhase() != Phase::kResting)
        return;

    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (pPlayer && pPlayer->IsSleepingOrWaiting())
        return;

    if (vote.FinishResting())
        spdlog::info("SleepVote: slept, the game's own rest is over");
}

/**
 * Give the clock back to the server once the skip is over here, rested or not, and tell the server.
 */
void SleepVoteService::EndSkipOnceRested() noexcept
{
    if (!m_calendar.IsSharedSkipRunning())
        return;

    const Phase cPhase = SleepWaitOk::GetLocalVote().GetPhase();
    if (cPhase == Phase::kAgreed || cPhase == Phase::kResting)
        return;

    m_calendar.EndSharedSkip();
    SendStep(RequestTimeSkip::Step::kAwake, 0.f);
}

/**
 * Say in the menu how long is left when the player presses OK or opens a menu during the wait after a refusal, and take the line away once the wait is over.
 */
void SleepVoteService::FollowWaitAfterRefusal() noexcept
{
    LocalSleepVote& vote = SleepWaitOk::GetLocalVote();
    const uint64_t cLeftMs = vote.GetWaitLeftMs(LocalSleepVote::SteadyNowMs());
    const bool cPressDropped = vote.TakeDroppedPress();
    if (cLeftMs == 0)
    {
        if (!m_waitLine.empty())
        {
            m_waitLine.clear();
            RefreshMenuLine();
        }
        return;
    }

    // Written when the player presses or a menu opens, not every second, so the menu's field is not rewritten all the time.
    const void* pMenu = SleepWaitOk::FindOpenMenu();
    if (cPressDropped || pMenu != m_waitMenu)
    {
        m_waitMenu = pMenu;
        ShowWaitLine(ToWholeSeconds(cLeftMs));
    }
}

/**
 * Show the wait after a refusal in the menu.
 *
 * @param aSeconds The seconds left.
 */
void SleepVoteService::ShowWaitLine(const uint32_t aSeconds) noexcept
{
    m_waitLine = fmt::format("Rest again in {} s", aSeconds);
    RefreshMenuLine();
}

/**
 * Put the line that matters most in the menu: the wait after a refusal, else where the vote stands, else the menu's own question.
 */
void SleepVoteService::RefreshMenuLine() noexcept
{
    if (!m_waitLine.empty())
        SleepWaitVoteLine::Show(m_waitLine);
    else if (!m_voteLine.empty())
        SleepWaitVoteLine::Show(m_voteLine);
    else
        SleepWaitVoteLine::Clear();
}

/**
 * Send one step of this player's vote.
 *
 * @param aStep What the player did.
 * @param aHours The length, in game hours; read with a confirmation only.
 */
void SleepVoteService::SendStep(const RequestTimeSkip::Step aStep, const float aHours) noexcept
{
    RequestTimeSkip request{};
    request.SkipStep = aStep;
    request.Hours = aHours;
    m_transport.Send(request);
}

/**
 * Show a change to the vote: whom it waits for to a player in it, who changed what to the others.
 *
 * @param acMessage Where the vote stands.
 */
void SleepVoteService::ShowVoteChange(const NotifyTimeSkip& acMessage) noexcept
{
    const String cMissing = NameList::Join(acMessage.MissingPlayerNames, "nobody");
    spdlog::info("SleepVote: '{}' {}, {}/{} ready for {} h, waiting for {}", acMessage.ChangedPlayerName.c_str(), NotifyTimeSkip::DescribeChange(acMessage.PlayerChange),
                 acMessage.ReadyCount, acMessage.RequiredCount, acMessage.Hours, cMissing.c_str());

    const bool cVoting = SleepWaitOk::GetLocalVote().IsVoting();
    if (cVoting)
        Utils::ShowModHudMessage(fmt::format("waiting for {} ({}/{})", cMissing.c_str(), acMessage.ReadyCount, acMessage.RequiredCount).c_str());
    else
        Utils::ShowModHudMessage(DescribeChangeForHud(acMessage).c_str());

    // The open menu shows the count too, as a VR player sees no HUD line over it; the question comes back once nobody is ready.
    if (acMessage.ReadyCount == 0)
        m_voteLine.clear();
    else
        m_voteLine = cVoting ? DescribeWaitingForMenu(acMessage) : DescribeVoteForMenu(acMessage);
    RefreshMenuLine();
}

/**
 * Start the agreed skip: the main loop presses the game's own OK for the agreed length.
 *
 * @param acMessage The agreement, with the length.
 */
void SleepVoteService::StartAgreedSkip(const NotifyTimeSkip& acMessage) noexcept
{
    spdlog::info("SleepVote: everyone confirmed, skipping {} h", acMessage.Hours);
    m_awaitingResync = true;
    m_voteLine.clear();
    RefreshMenuLine();

    // Before the server's resync, which comes next: the clock is the game's own until it has rested.
    m_calendar.BeginSharedSkip(acMessage.Hours);

    if (!SleepWaitOk::GetLocalVote().Agree(acMessage.Hours))
        spdlog::info("SleepVote: this game holds no vote any more; the clock moves here without a sleep");

    // A second OK pressed while the agreement was on its way opened a newer vote on the server; this game rests now, so it is dropped there.
    SendStep(RequestTimeSkip::Step::kDropCrossed, 0.f);
}

/**
 * Release a vote that ran out, and say who did not confirm.
 *
 * @param acMessage The end of the vote, with the missing names.
 */
void SleepVoteService::ReleaseRunOutVote(const NotifyTimeSkip& acMessage) noexcept
{
    const String cMissing = NameList::Join(acMessage.MissingPlayerNames, "nobody");
    spdlog::warn("SleepVote: timed out, did not confirm: {}", cMissing.c_str());

    // A confirmation sent as the vote ran out opened a newer vote on the server, which this game no longer counts as its own.
    if (SleepWaitOk::GetLocalVote().Withdraw())
        SendStep(RequestTimeSkip::Step::kDropCrossed, 0.f);
    m_voteLine.clear();
    RefreshMenuLine();
    Utils::ShowModHudMessage(fmt::format("nobody rests, {} did not confirm in time", cMissing.c_str()).c_str());
}

/**
 * Rest this player's own length, as the game does offline, where the server does not share the time.
 */
void SleepVoteService::RestAlone() noexcept
{
    spdlog::info("SleepVote: this server does not share the time (Gameplay:bEnableSleep is off), resting alone");

    SleepWaitOk::GetLocalVote().AgreeAlone();
    Utils::ShowModHudMessage("no time will pass, sleeping does not pass time on this server");
}

/**
 * Release this player's press when the server refused it as too soon after a withdrawal or a vote that ran out.
 * Presses are then dropped for the wait, and the menu and the HUD say how long it is.
 *
 * @param acMessage The refusal, with the wait.
 */
void SleepVoteService::RefuseTooSoon(const NotifyTimeSkip& acMessage) noexcept
{
    LocalSleepVote& vote = SleepWaitOk::GetLocalVote();
    // Another press, sent while this refusal was on its way, may have reached the server after the wait.
    // This game no longer counts it, so it is dropped there too.
    if (vote.Withdraw())
        SendStep(RequestTimeSkip::Step::kDropCrossed, 0.f);
    const uint64_t cWaitMs = std::chrono::milliseconds(std::chrono::seconds(acMessage.WaitSeconds)).count();
    vote.WaitBeforePressing(LocalSleepVote::SteadyNowMs() + cWaitMs);

    spdlog::info("SleepVote: confirmed again too soon, {} s to wait", acMessage.WaitSeconds);
    ShowWaitLine(acMessage.WaitSeconds);
    Utils::ShowModHudMessage(fmt::format("you can call a rest again in {} s", acMessage.WaitSeconds).c_str());
}
