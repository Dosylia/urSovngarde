#include <Services/HourMoveService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerLeaveEvent.h>

#include <Messages/RequestCombatState.h>
#include <Messages/RequestHourMove.h>

#include "Game/Player.h"

#include <HourMove.h>

namespace
{
/**
 * Make an answer for the asker alone: a refusal, or the hour already reached.
 *
 * @param aEvent What came of the ask.
 * @param aPlayerId The player the reason names, as the one in combat; 0 for nobody.
 * @return The answer.
 */
NotifyHourMove MakeAnswer(const NotifyHourMove::Event aEvent, const uint32_t aPlayerId = 0) noexcept
{
    NotifyHourMove answer{};
    answer.MoveEvent = aEvent;
    answer.PlayerId = aPlayerId;
    return answer;
}

/**
 * Say why a move was refused, in words for the log.
 *
 * @param aEvent The refusal.
 * @return A few words.
 */
const char* DescribeRefusal(const NotifyHourMove::Event aEvent) noexcept
{
    switch (aEvent)
    {
    case NotifyHourMove::Event::kRefusedDisabled: return "Gameplay:bEnableSleep is off";
    case NotifyHourMove::Event::kRefusedPublic: return "a public server";
    case NotifyHourMove::Event::kRefusedCombat: return "a party member fights";
    case NotifyHourMove::Event::kRefusedBusy: return "a shared sleep vote is open, or a game still rests one";
    case NotifyHourMove::Event::kRefusedNoParty: return "in no party with others online";
    default: return "refused";
    }
}
} // namespace

/**
 * Listen for hour moves, combat reports and departures.
 *
 * @param aWorld The server's world, for the clock, the parties and the sleep vote.
 * @param aDispatcher Where the server's events come from.
 */
HourMoveService::HourMoveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_hourMoveConnection = aDispatcher.sink<PacketEvent<RequestHourMove>>().connect<&HourMoveService::OnHourMoveRequest>(this);
    m_combatConnection = aDispatcher.sink<PacketEvent<RequestCombatState>>().connect<&HourMoveService::OnCombatStateRequest>(this);
    m_leaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&HourMoveService::OnPlayerLeave>(this);
}

/**
 * Move the shared clock to the hour asked for, or tell the asker why not.
 *
 * @param acMessage The hour and the player who asked.
 */
void HourMoveService::OnHourMoveRequest(const PacketEvent<RequestHourMove>& acMessage) noexcept
{
    Player& asker = *acMessage.pPlayer;
    const float cTarget = acMessage.Packet.TargetHour;

    if (const auto cRefusal = FindRefusal(asker))
    {
        spdlog::info("Hour: player {:X} '{}' asked to move the hour to {:.2f}, refused: {}", asker.GetId(), asker.GetUsername().c_str(), cTarget,
                     DescribeRefusal(cRefusal->MoveEvent));
        asker.Send(*cRefusal);
        return;
    }

    const auto cHours = HourMove::HoursUntil(m_world.GetCalendarService().GetCurrentHour(), cTarget);
    if (!cHours)
    {
        spdlog::info("Hour: player {:X} '{}' asked to move the hour to {:.2f}, which it is already", asker.GetId(), asker.GetUsername().c_str(), cTarget);
        asker.Send(MakeAnswer(NotifyHourMove::Event::kAlreadyThere));
        return;
    }

    MoveClock(asker, *cHours);
}

/**
 * Note whether a player fights, for the combat rule.
 *
 * @param acMessage The report and the player who sent it.
 */
void HourMoveService::OnCombatStateRequest(const PacketEvent<RequestCombatState>& acMessage) noexcept
{
    const uint32_t cId = acMessage.pPlayer->GetId();
    if (acMessage.Packet.InCombat)
        m_playersInCombat.insert(cId);
    else
        m_playersInCombat.erase(cId);
}

/**
 * Forget a departing player's combat state.
 *
 * @param acEvent The player who disconnected.
 */
void HourMoveService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    m_playersInCombat.erase(acEvent.pPlayer->GetId());
}

/**
 * Find the rule that refuses a player's hour move, in this order: switched off, public server, no party, a shared skip under way, a party member fighting.
 *
 * @param aAsker The player who asked.
 * @return The refusal for the asker; nothing when the move may go ahead.
 */
std::optional<NotifyHourMove> HourMoveService::FindRefusal(Player& aAsker) const noexcept
{
    if (!CalendarService::IsSleepEnabled())
        return MakeAnswer(NotifyHourMove::Event::kRefusedDisabled);
    if (CalendarService::IsOpenToStrangers())
        return MakeAnswer(NotifyHourMove::Event::kRefusedPublic);

    // A player alone on the server needs no party; with others online, the hour moves for the asker's party.
    const auto* pParty = m_world.GetPartyService().GetPlayerParty(&aAsker);
    if (!pParty && m_world.GetPlayerManager().Count() != 1)
        return MakeAnswer(NotifyHourMove::Event::kRefusedNoParty);

    if (m_world.GetSleepVoteService().IsTimeSkipUnderWay())
        return MakeAnswer(NotifyHourMove::Event::kRefusedBusy);

    // The asker's own game refuses the move while its player fights, so the server checks the rest of the party.
    if (pParty)
        for (const Player* pMember : pParty->Members)
            if (pMember != &aAsker && m_playersInCombat.contains(pMember->GetId()))
                return MakeAnswer(NotifyHourMove::Event::kRefusedCombat, pMember->GetId());

    return std::nullopt;
}

/**
 * Tell every game in the world the move, then move the shared clock, which resyncs them.
 *
 * @param acAsker The player who asked.
 * @param aHours How far to move, in game hours.
 */
void HourMoveService::MoveClock(const Player& acAsker, const float aHours) noexcept
{
    NotifyHourMove moved{};
    moved.MoveEvent = NotifyHourMove::Event::kMoved;
    moved.PlayerId = acAsker.GetId();
    moved.Hours = aHours;

    // The move goes out before the resync, so a personal calendar takes the hours before the new time.
    // An asker not in the world yet hears it too, as its answer.
    GameServer::Get()->SendToLoaded(moved);
    if (!acAsker.GetCellComponent())
        acAsker.Send(moved);
    m_world.GetCalendarService().AdvanceHours(aHours);
    spdlog::info("Hour: player {:X} '{}' moved the shared clock forward {:.2f} h to {:.2f}", acAsker.GetId(), acAsker.GetUsername().c_str(), aHours,
                 m_world.GetCalendarService().GetCurrentHour());
}
