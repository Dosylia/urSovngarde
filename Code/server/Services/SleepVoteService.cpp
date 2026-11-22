#include <Services/SleepVoteService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/RequestTimeSkip.h>

#include "Game/Player.h"

#include <NameList.h>
#include <WholeSeconds.h>

/**
 * Listen for the players' steps, arrivals and departures, and for the server's ticks.
 *
 * @param aWorld The server's world, for the shared clock.
 * @param aDispatcher Where the server's events come from.
 */
SleepVoteService::SleepVoteService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_requestConnection = aDispatcher.sink<PacketEvent<RequestTimeSkip>>().connect<&SleepVoteService::OnTimeSkipRequest>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&SleepVoteService::OnPlayerJoin>(this);
    m_leaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&SleepVoteService::OnPlayerLeave>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SleepVoteService::OnUpdate>(this);
}

/**
 * Tell whether a shared skip is under way: a vote open, or a game still resting an agreed skip.
 * A plugin's hour move waits meanwhile, so it neither cuts into a vote nor lands in a game's own rest.
 *
 * @return True while one is.
 */
bool SleepVoteService::IsTimeSkipUnderWay() const noexcept
{
    return m_vote.IsOpen() || m_rests.IsAnyoneResting(GameServer::Get()->GetTick());
}

/**
 * Apply one player's step: a confirmation, a withdrawal, the drop of a crossed confirmation, or the end of their game's own sleep.
 *
 * @param acMessage The step and the player who sent it.
 */
void SleepVoteService::OnTimeSkipRequest(const PacketEvent<RequestTimeSkip>& acMessage) noexcept
{
    Player& player = *acMessage.pPlayer;
    const RequestTimeSkip& cRequest = acMessage.Packet;
    const auto cConnectedIds = m_world.GetPlayerManager().ListIds();

    switch (cRequest.SkipStep)
    {
    case RequestTimeSkip::Step::kConfirm: ConfirmVote(player, cRequest.Hours, cConnectedIds); break;
    case RequestTimeSkip::Step::kWithdraw:
    case RequestTimeSkip::Step::kWithdrawCombat:
    case RequestTimeSkip::Step::kDropCrossed: WithdrawVote(player, cRequest.SkipStep, cConnectedIds); break;
    case RequestTimeSkip::Step::kAwake:
        m_rests.Wake(player.GetId());
        spdlog::info("SleepVote: player {:X} '{}' awake", player.GetId(), player.GetUsername().c_str());
        break;
    default: spdlog::warn("SleepVote: player {:X} '{}' sent an unknown step, ignored", player.GetId(), player.GetUsername().c_str()); break;
    }
}

/**
 * Show an arriving player the open vote, so they can join it.
 *
 * @param acEvent The player whose game has entered the world.
 */
void SleepVoteService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    if (!m_vote.IsOpen())
        return;

    SettleVote(nullptr, NotifyTimeSkip::Change::kNone, m_world.GetPlayerManager().ListIds());
}

/**
 * Forget a departing player, their confirmation, wait and rest, then tell the others while a vote is open; their leaving can complete it.
 *
 * @param acEvent The player who disconnected, still listed by the server.
 */
void SleepVoteService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    const Player& cLeaving = *acEvent.pPlayer;
    m_rests.Wake(cLeaving.GetId());
    if (!m_vote.IsOpen())
    {
        m_vote.Forget(cLeaving.GetId());
        return;
    }

    const auto cConnectedIds = m_world.GetPlayerManager().ListIds(&cLeaving);
    m_vote.Forget(cLeaving.GetId());

    spdlog::info("SleepVote: player {:X} '{}' left, {}/{} ready", cLeaving.GetId(), cLeaving.GetUsername().c_str(), m_vote.CountReady(cConnectedIds), cConnectedIds.size());
    SettleVote(&cLeaving, NotifyTimeSkip::Change::kLeft, cConnectedIds);
}

/**
 * Tell everyone the changes of length that were recorded quietly once they may be told, and end an open vote that has run out.
 *
 * @param acEvent The server's tick.
 */
void SleepVoteService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_vote.IsOpen())
        return;

    const uint64_t cNowMs = GameServer::Get()->GetTick();
    const auto cConnectedIds = m_world.GetPlayerManager().ListIds();
    if (m_vote.GetStanding(cConnectedIds, cNowMs) == SleepVote::Standing::kTimedOut)
    {
        RunOutVote(cConnectedIds);
        return;
    }

    // A change of length leaves the counts as they were, so each notice only tells the new length.
    for (const uint32_t cId : m_vote.TakeChangesDue(cNowMs))
    {
        if (const Player* pChanged = m_world.GetPlayerManager().GetById(cId))
            SettleVote(pChanged, NotifyTimeSkip::Change::kConfirmed, cConnectedIds);
    }
}

/**
 * Record a player's confirmed length and tell everyone, or refuse it when sleep is switched off, the length is broken or the player still waits.
 * The same length again changes nothing, and a change of length soon after the last one is told later, from OnUpdate.
 *
 * @param aPlayer The player who pressed OK.
 * @param aHours The length they picked, in game hours.
 * @param acConnectedIds Server ids of every connected player.
 */
void SleepVoteService::ConfirmVote(Player& aPlayer, const float aHours, const std::vector<uint32_t>& acConnectedIds) noexcept
{
    if (!CalendarService::IsSleepEnabled())
    {
        RefuseWhileDisabled(aPlayer);
        return;
    }

    switch (m_vote.Confirm(aPlayer.GetId(), aHours, GameServer::Get()->GetTick()))
    {
    case SleepVote::ConfirmResult::kInvalidHours:
        spdlog::warn("SleepVote: player {:X} '{}' sent {} h, which no menu offers, ignored", aPlayer.GetId(), aPlayer.GetUsername().c_str(), aHours);
        return;
    case SleepVote::ConfirmResult::kTooSoon: RefuseTooSoon(aPlayer); return;
    case SleepVote::ConfirmResult::kUnchanged: return;
    case SleepVote::ConfirmResult::kChangedQuietly:
        spdlog::debug("SleepVote: player {:X} '{}' changed to {:.2f} h, told once 5 s have passed since the last change", aPlayer.GetId(), aPlayer.GetUsername().c_str(), aHours);
        return;
    case SleepVote::ConfirmResult::kAccepted: break;
    }

    spdlog::info("SleepVote: player {:X} '{}' confirmed {:.2f} h, {}/{} ready", aPlayer.GetId(), aPlayer.GetUsername().c_str(), aHours, m_vote.CountReady(acConnectedIds),
                 acConnectedIds.size());
    SettleVote(&aPlayer, NotifyTimeSkip::Change::kConfirmed, acConnectedIds);
}

/**
 * Take a player's confirmation back and tell the others; nothing happens when they had not confirmed.
 * A withdrawal makes the player wait; the drop of a confirmation that crossed a vote's end does not, right after the end.
 *
 * @param acPlayer The player who closed the menu, entered combat, or whose game dropped a crossed confirmation.
 * @param aStep Which of those it was.
 * @param acConnectedIds Server ids of every connected player.
 */
void SleepVoteService::WithdrawVote(const Player& acPlayer, const RequestTimeSkip::Step aStep, const std::vector<uint32_t>& acConnectedIds) noexcept
{
    const uint32_t cId = acPlayer.GetId();
    const uint64_t cNowMs = GameServer::Get()->GetTick();
    const bool cHadConfirmed = aStep == RequestTimeSkip::Step::kDropCrossed ? m_vote.DropCrossed(cId, cNowMs) : m_vote.Withdraw(cId, cNowMs);
    if (!cHadConfirmed)
        return;

    const auto cChange = aStep == RequestTimeSkip::Step::kWithdrawCombat ? NotifyTimeSkip::Change::kWithdrewCombat : NotifyTimeSkip::Change::kWithdrew;
    const char* pWhat = aStep == RequestTimeSkip::Step::kDropCrossed ? "dropped a confirmation sent as the vote ended" : NotifyTimeSkip::DescribeChange(cChange);
    spdlog::info("SleepVote: player {:X} '{}' {}, waits {} s, {}/{} ready", cId, acPlayer.GetUsername().c_str(), pWhat, ToWholeSeconds(m_vote.GetCooldownLeftMs(cId, cNowMs)),
                 m_vote.CountReady(acConnectedIds), acConnectedIds.size());
    SettleVote(&acPlayer, cChange, acConnectedIds);
}

/**
 * Tell a player who confirmed that the server does not share the time.
 *
 * @param aPlayer The player who pressed OK.
 */
void SleepVoteService::RefuseWhileDisabled(Player& aPlayer) noexcept
{
    spdlog::info("SleepVote: player {:X} '{}' confirmed while Gameplay:bEnableSleep is off, refused", aPlayer.GetId(), aPlayer.GetUsername().c_str());

    NotifyTimeSkip refusal{};
    refusal.SkipEvent = NotifyTimeSkip::Event::kDisabled;
    aPlayer.Send(refusal);
}

/**
 * Tell a player who confirmed too soon after withdrawing, or after a vote ran out, how long they still wait.
 *
 * @param aPlayer The player who pressed OK.
 */
void SleepVoteService::RefuseTooSoon(Player& aPlayer) noexcept
{
    NotifyTimeSkip refusal{};
    refusal.SkipEvent = NotifyTimeSkip::Event::kTooSoon;
    refusal.WaitSeconds = ToWholeSeconds(m_vote.GetCooldownLeftMs(aPlayer.GetId(), GameServer::Get()->GetTick()));
    aPlayer.Send(refusal);
    spdlog::info("SleepVote: player {:X} '{}' confirmed again too soon, {} s left, refused", aPlayer.GetId(), aPlayer.GetUsername().c_str(), refusal.WaitSeconds);
}

/**
 * After a change, skip the time when everyone has confirmed, end the vote when it ran out, or else tell everyone where it stands.
 *
 * @param apChanged The player whose step changed the vote; null when nobody's step did.
 * @param aChange What that player did.
 * @param acConnectedIds Server ids of every connected player.
 */
void SleepVoteService::SettleVote(const Player* apChanged, const NotifyTimeSkip::Change aChange, const std::vector<uint32_t>& acConnectedIds) noexcept
{
    switch (m_vote.GetStanding(acConnectedIds, GameServer::Get()->GetTick()))
    {
    case SleepVote::Standing::kAgreed: SkipAgreedHours(acConnectedIds); return;
    case SleepVote::Standing::kTimedOut: RunOutVote(acConnectedIds); return;
    default: break;
    }

    // Still waiting, or closed by the last withdrawal: everyone sees the new counts.
    const NotifyTimeSkip cNotice = BuildNotice(NotifyTimeSkip::Event::kVoteChanged, apChanged, aChange, acConnectedIds);
    if (m_vote.IsOpen())
        spdlog::info("SleepVote: waiting for {}", NameList::Join(cNotice.MissingPlayerNames, "nobody").c_str());
    else
        spdlog::info("SleepVote: nobody confirmed any more, vote closed");

    GameServer::Get()->SendToPlayers(cNotice);
}

/**
 * Tell everyone to skip the shortest length, close the vote, then move the shared clock and resync it.
 *
 * @param acConnectedIds Server ids of every connected player.
 */
void SleepVoteService::SkipAgreedHours(const std::vector<uint32_t>& acConnectedIds) noexcept
{
    const NotifyTimeSkip cNotice = BuildNotice(NotifyTimeSkip::Event::kAgreed, nullptr, NotifyTimeSkip::Change::kNone, acConnectedIds);
    spdlog::info("SleepVote: everyone confirmed, skipping {:.2f} h", cNotice.Hours);

    // The skip goes out before the resync, so each game counts the hours it slept before taking the new time.
    const uint64_t cNowMs = GameServer::Get()->GetTick();
    GameServer::Get()->SendToPlayers(cNotice);
    m_vote.Close(cNowMs);
    // Each game rests now; a plugin's hour move waits until every one is awake.
    m_rests.Start(acConnectedIds, cNowMs);

    m_world.GetCalendarService().AdvanceHours(cNotice.Hours);
    spdlog::info("SleepVote: clock moved {:.2f} h, time resync sent", cNotice.Hours);
}

/**
 * Tell everyone the vote ran out and who did not confirm, then close it.
 *
 * @param acConnectedIds Server ids of every connected player.
 */
void SleepVoteService::RunOutVote(const std::vector<uint32_t>& acConnectedIds) noexcept
{
    const NotifyTimeSkip cNotice = BuildNotice(NotifyTimeSkip::Event::kTimedOut, nullptr, NotifyTimeSkip::Change::kNone, acConnectedIds);
    spdlog::warn("SleepVote: timed out after {} s, did not confirm: {}", ToWholeSeconds(SleepVote::kTimeoutMs), NameList::Join(cNotice.MissingPlayerNames, "nobody").c_str());

    GameServer::Get()->SendToPlayers(cNotice);
    m_vote.RunOut(GameServer::Get()->GetTick());
}

/**
 * Build a notice of where the vote stands now.
 *
 * @param aEvent What happened to the vote.
 * @param apChanged The player whose step it was; null when nobody's.
 * @param aChange What that player did.
 * @param acConnectedIds Server ids of every connected player.
 * @return The notice, with the shortest length, the counts and the missing players' names.
 */
NotifyTimeSkip SleepVoteService::BuildNotice(const NotifyTimeSkip::Event aEvent, const Player* apChanged, const NotifyTimeSkip::Change aChange,
                                             const std::vector<uint32_t>& acConnectedIds) const noexcept
{
    NotifyTimeSkip notice{};
    notice.SkipEvent = aEvent;
    notice.PlayerChange = aChange;
    if (apChanged)
        notice.ChangedPlayerName = apChanged->GetUsername();
    notice.Hours = m_vote.GetShortestHours();
    notice.ReadyCount = static_cast<uint32_t>(m_vote.CountReady(acConnectedIds));
    notice.RequiredCount = static_cast<uint32_t>(acConnectedIds.size());

    for (const uint32_t cId : m_vote.ListMissing(acConnectedIds))
        if (const Player* pMissing = m_world.GetPlayerManager().GetById(cId))
            notice.MissingPlayerNames.push_back(pMissing->GetUsername());

    return notice;
}
