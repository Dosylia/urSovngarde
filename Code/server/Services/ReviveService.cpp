#include <Services/ReviveService.h>

#include <ReviveRules.h>
#include <GameServer.h>
#include <World.h>
#include <Components.h>
#include <Setting.h>
#include <Events/UpdateEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Messages/PlayerDownedRequest.h>
#include <Messages/ReviveRequest.h>
#include <Messages/SafePointRequest.h>
#include <Messages/PlayerRespawnRequest.h>

namespace
{
Console::Setting uDownedSeconds{"Gameplay:uDownedSeconds", "How long a downed player in a party can be revived before respawning, in seconds", 60u};

// A second of grace after the time runs out: a hold can complete just as it does.
constexpr auto cGrace = std::chrono::seconds(1);

// Where a player's character stands, as this server last heard: false when it has no character or no position yet.
bool GetPlace(World& aWorld, const Player* apPlayer, CellIdComponent& aCell, glm::vec3& aPosition) noexcept
{
    const auto cCharacter = apPlayer->GetCharacter();
    if (!cCharacter || !aWorld.valid(*cCharacter))
        return false;
    const auto* pCell = aWorld.try_get<CellIdComponent>(*cCharacter);
    const auto* pMovement = aWorld.try_get<MovementComponent>(*cCharacter);
    if (!pCell || !pMovement)
        return false;
    aCell = *pCell;
    aPosition = pMovement->Position;
    return true;
}

const char* DescribePoint(const ReviveRules::RespawnPoint aPoint) noexcept
{
    switch (aPoint)
    {
    case ReviveRules::RespawnPoint::kPartyLeader: return "the party leader's safe point";
    case ReviveRules::RespawnPoint::kOwn: return "their own safe point";
    case ReviveRules::RespawnPoint::kGameDefault: return "the game's own respawn spot (no safe point known)";
    }
    return "an unknown point";
}
} // namespace

ReviveService::ReviveService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&ReviveService::OnUpdate>(this))
    , m_downedConnection(aDispatcher.sink<PacketEvent<PlayerDownedRequest>>().connect<&ReviveService::OnPlayerDowned>(this))
    , m_reviveConnection(aDispatcher.sink<PacketEvent<ReviveRequest>>().connect<&ReviveService::OnRevive>(this))
    , m_safePointConnection(aDispatcher.sink<PacketEvent<SafePointRequest>>().connect<&ReviveService::OnSafePoint>(this))
    , m_respawnConnection(aDispatcher.sink<PacketEvent<PlayerRespawnRequest>>().connect<&ReviveService::OnPlayerRespawn>(this))
    , m_leaveConnection(aDispatcher.sink<PlayerLeaveEvent>().connect<&ReviveService::OnPlayerLeave>(this))
{
}

void ReviveService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_downed.empty())
        return;

    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextCheck)
        return;
    m_nextCheck = cNow + std::chrono::seconds(1);

    CheckDowned();
}

void ReviveService::OnPlayerDowned(const PacketEvent<PlayerDownedRequest>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    if (!acMessage.Packet.IsDown)
    {
        // The player's own game stopped waiting: the down ends now, as the server would have ended it, and the answer says where it respawns.
        // Until then a revive would still be accepted, and then ignored by a game already respawning.
        if (acMessage.Packet.Respawning)
        {
            if (IsDown(pPlayer->GetId()))
                EndDown(pPlayer, NotifyDownedState::kRespawned, "stopped waiting on its own game's word");
            return;
        }

        const auto cTold = TakeDowned(pPlayer->GetId());
        if (!cTold)
            return;
        spdlog::info("Revive: player {} '{}' is up again without a revive (a save was loaded)", pPlayer->GetId(), pPlayer->GetUsername().c_str());

        NotifyDownedState notify{};
        notify.PlayerId = pPlayer->GetId();
        notify.Change = NotifyDownedState::kStoodUp;
        SendToPartyOf(pPlayer, notify);
        TellFormerMembers(pPlayer, *cTold);
        return;
    }

    const auto cParty = PartyOf(pPlayer);
    const uint32_t cSeconds = ReviveRules::DownedSeconds(cParty.size(), uDownedSeconds.value_as<uint32_t>());
    if (cSeconds == 0)
    {
        EndDown(pPlayer, NotifyDownedState::kDied, "is down alone");
        return;
    }

    Downed& downed = m_downed[pPlayer->GetId()];
    downed.Until = std::chrono::steady_clock::now() + std::chrono::seconds(cSeconds);
    downed.Told.clear();
    for (const Player* pMember : cParty)
        downed.Told.push_back(pMember->GetId());

    // The last one standing going down ends the party's wait at once: nobody is told to revive a player who respawns straight away.
    if (EndFallenParty(pPlayer))
        return;

    spdlog::info("Revive: player {} '{}' is down; {} s to be revived ({} in the party)", pPlayer->GetId(), pPlayer->GetUsername().c_str(), cSeconds, cParty.size());

    NotifyDownedState notify{};
    notify.PlayerId = pPlayer->GetId();
    notify.Change = NotifyDownedState::kDown;
    notify.SecondsLeft = cSeconds;
    SendToPartyOf(pPlayer, notify);
}

void ReviveService::OnRevive(const PacketEvent<ReviveRequest>& acMessage) noexcept
{
    Player* pReviver = acMessage.pPlayer;
    const uint32_t cTargetId = acMessage.Packet.TargetPlayerId;

    NotifyDownedState refused{};
    refused.PlayerId = cTargetId;
    refused.Change = NotifyDownedState::kReviveRefused;
    refused.ReviverId = pReviver->GetId();

    Player* pTarget = m_world.GetPlayerManager().GetById(cTargetId);
    if (!pTarget)
    {
        spdlog::info("Revive: player {} '{}' asked to revive player {}, who is not on the server; refused", pReviver->GetId(), pReviver->GetUsername().c_str(), cTargetId);
        refused.Refusal = ReviveRules::Refusal::kTargetNotDown;
        pReviver->Send(refused);
        return;
    }

    ReviveRules::ReviveCheck check{};
    check.ReviverId = pReviver->GetId();
    check.TargetId = cTargetId;
    check.ReviverDown = IsDown(pReviver->GetId());
    const auto cDowned = m_downed.find(cTargetId);
    check.TargetDown = cDowned != m_downed.end();
    check.TargetTimeLeft = check.TargetDown && std::chrono::steady_clock::now() <= cDowned->second.Until + cGrace;
    const auto& cReviverParty = pReviver->GetParty().JoinedPartyId;
    check.SameParty = cReviverParty && cReviverParty == pTarget->GetParty().JoinedPartyId;

    CellIdComponent reviverCell{};
    CellIdComponent targetCell{};
    glm::vec3 reviverAt{};
    glm::vec3 targetAt{};
    if (GetPlace(m_world, pReviver, reviverCell, reviverAt) && GetPlace(m_world, pTarget, targetCell, targetAt))
    {
        check.SamePlace = reviverCell.IsInInteriorCell() ? reviverCell.Cell == targetCell.Cell : reviverCell.WorldSpaceId == targetCell.WorldSpaceId;
        check.Distance = glm::distance(reviverAt, targetAt);
    }

    const auto cRefusal = ReviveRules::CheckRevive(check);
    if (cRefusal != ReviveRules::Refusal::kNone)
    {
        spdlog::info(
            "Revive: player {} '{}' asked to revive player {} '{}'; refused, {} ({:.0f} units apart)", pReviver->GetId(), pReviver->GetUsername().c_str(), cTargetId,
            pTarget->GetUsername().c_str(), ReviveRules::Describe(cRefusal), check.Distance);
        refused.Refusal = cRefusal;
        pReviver->Send(refused);
        return;
    }

    const auto cTold = TakeDowned(cTargetId);
    spdlog::info(
        "Revive: player {} '{}' revived player {} '{}' ({:.0f} units apart)", pReviver->GetId(), pReviver->GetUsername().c_str(), cTargetId, pTarget->GetUsername().c_str(),
        check.Distance);

    NotifyDownedState notify{};
    notify.PlayerId = cTargetId;
    notify.Change = NotifyDownedState::kRevived;
    notify.ReviverId = pReviver->GetId();
    SendToPartyOf(pTarget, notify);
    if (cTold)
        TellFormerMembers(pTarget, *cTold);
}

void ReviveService::OnSafePoint(const PacketEvent<SafePointRequest>& acMessage) noexcept
{
    const Player* pPlayer = acMessage.pPlayer;
    const auto& cPacket = acMessage.Packet;

    SafePoint& point = m_safePoints[pPlayer->GetId()];
    point.CellId = cPacket.CellId;
    point.WorldSpaceId = cPacket.WorldSpaceId;
    point.Position = cPacket.Position;
    spdlog::info(
        "Revive: player {} '{}' has a new safe point in cell {:X}:{:X} (worldspace {:X}:{:X}) at ({:.0f}, {:.0f}, {:.0f})", pPlayer->GetId(), pPlayer->GetUsername().c_str(),
        cPacket.CellId.ModId, cPacket.CellId.BaseId, cPacket.WorldSpaceId.ModId, cPacket.WorldSpaceId.BaseId, cPacket.Position.x, cPacket.Position.y, cPacket.Position.z);
}

void ReviveService::OnPlayerRespawn(const PacketEvent<PlayerRespawnRequest>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    const auto cTold = TakeDowned(pPlayer->GetId());
    if (!cTold)
        return;

    spdlog::info("Revive: player {} '{}' respawned on its own game's word, before the server ended the wait", pPlayer->GetId(), pPlayer->GetUsername().c_str());

    NotifyDownedState notify{};
    notify.PlayerId = pPlayer->GetId();
    notify.Change = NotifyDownedState::kRespawned;
    SendToPartyOf(pPlayer, notify);
    TellFormerMembers(pPlayer, *cTold);
}

void ReviveService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    // PartyService has already taken the player out of their party; the next CheckDowned sees whether anyone down is left with nobody standing.
    Player* pLeaving = acEvent.pPlayer;
    m_safePoints.erase(pLeaving->GetId());
    const auto cTold = TakeDowned(pLeaving->GetId());
    if (!cTold)
        return;
    spdlog::info("Revive: player {} '{}' left while down", pLeaving->GetId(), pLeaving->GetUsername().c_str());
    TellFormerMembers(pLeaving, *cTold);
}

bool ReviveService::IsDown(const uint32_t aPlayerId) const noexcept
{
    return m_downed.find(aPlayerId) != m_downed.end();
}

std::optional<Vector<uint32_t>> ReviveService::TakeDowned(const uint32_t aPlayerId) noexcept
{
    const auto cDowned = m_downed.find(aPlayerId);
    if (cDowned == m_downed.end())
        return std::nullopt;
    Vector<uint32_t> told = cDowned->second.Told;
    m_downed.erase(cDowned);
    return told;
}

void ReviveService::TellFormerMembers(Player* apPlayer, const Vector<uint32_t>& acTold) const noexcept
{
    const auto* pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    for (const uint32_t cId : acTold)
    {
        if (cId == apPlayer->GetId())
            continue;
        const bool cStillInParty = pParty && std::any_of(pParty->Members.begin(), pParty->Members.end(), [cId](const Player* apMember) { return apMember->GetId() == cId; });
        if (cStillInParty)
            continue;
        Player* pFormer = m_world.GetPlayerManager().GetById(cId);
        if (!pFormer)
            continue;

        NotifyDownedState notify{};
        notify.PlayerId = apPlayer->GetId();
        notify.Change = NotifyDownedState::kLeft;
        pFormer->Send(notify);
        spdlog::info("Revive: told player {} '{}' that player {} '{}', down, is no longer in its party", cId, pFormer->GetUsername().c_str(), apPlayer->GetId(),
                     apPlayer->GetUsername().c_str());
    }
}

Vector<Player*> ReviveService::PartyOf(Player* apPlayer) const noexcept
{
    const auto* pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    if (!pParty)
        return {apPlayer};
    return pParty->Members;
}

void ReviveService::SendToPartyOf(Player* apPlayer, const NotifyDownedState& acMessage) const noexcept
{
    for (const Player* pMember : PartyOf(apPlayer))
        pMember->Send(acMessage);
}

void ReviveService::EndDown(Player* apPlayer, const NotifyDownedState::State aChange, const char* acWhy) noexcept
{
    const auto cTold = TakeDowned(apPlayer->GetId());

    NotifyDownedState notify{};
    notify.PlayerId = apPlayer->GetId();
    notify.Change = aChange;

    // The party leader's point when another player leads; a leader, or a player alone, has only their own.
    const auto* pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    const bool cLedByOther = pParty && pParty->Members.size() >= 2 && pParty->LeaderPlayerId != apPlayer->GetId();
    const auto cLeaderPoint = cLedByOther ? m_safePoints.find(pParty->LeaderPlayerId) : m_safePoints.end();
    const auto cOwnPoint = m_safePoints.find(apPlayer->GetId());
    const auto cChoice = ReviveRules::ChooseRespawnPoint(cLeaderPoint != m_safePoints.end(), cOwnPoint != m_safePoints.end());
    const SafePoint* pPoint = nullptr;
    if (cChoice == ReviveRules::RespawnPoint::kPartyLeader)
        pPoint = &cLeaderPoint->second;
    else if (cChoice == ReviveRules::RespawnPoint::kOwn)
        pPoint = &cOwnPoint->second;
    if (pPoint)
    {
        notify.HasRespawnPoint = true;
        notify.RespawnCellId = pPoint->CellId;
        notify.RespawnWorldSpaceId = pPoint->WorldSpaceId;
        notify.RespawnPosition = pPoint->Position;
    }

    spdlog::info("Revive: player {} '{}' {}; respawning at {}", apPlayer->GetId(), apPlayer->GetUsername().c_str(), acWhy, DescribePoint(cChoice));
    SendToPartyOf(apPlayer, notify);
    if (cTold)
        TellFormerMembers(apPlayer, *cTold);
}

bool ReviveService::EndFallenParty(Player* apPlayer) noexcept
{
    const auto cParty = PartyOf(apPlayer);
    std::vector<bool> membersDown;
    for (const Player* pMember : cParty)
        membersDown.push_back(IsDown(pMember->GetId()));
    if (!ReviveRules::NobodyToRevive(membersDown))
        return false;

    spdlog::info("Revive: nobody in the party of player {} '{}' is left standing ({} down); all respawn", apPlayer->GetId(), apPlayer->GetUsername().c_str(), membersDown.size());
    for (Player* pMember : cParty)
        EndDown(pMember, NotifyDownedState::kPartyFallen, "fell with the whole party");
    return true;
}

void ReviveService::CheckDowned() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();

    Vector<uint32_t> downedIds;
    for (const auto& cEntry : m_downed)
        downedIds.push_back(cEntry.first);

    for (const uint32_t cId : downedIds)
    {
        if (!IsDown(cId))
            continue; // its whole party respawned earlier in this loop

        Player* pPlayer = m_world.GetPlayerManager().GetById(cId);
        if (!pPlayer)
        {
            m_downed.erase(cId);
            continue;
        }

        if (cNow > m_downed[cId].Until + cGrace)
        {
            EndDown(pPlayer, NotifyDownedState::kDied, "was not revived in time");
            continue;
        }

        if (EndFallenParty(pPlayer))
            continue;

        // A member who joined after this player went down was not told yet: told now, with the time left.
        Downed& downed = m_downed[cId];
        const auto cParty = PartyOf(pPlayer);
        const auto cInParty = [&cParty](const uint32_t aId)
        {
            return std::any_of(cParty.begin(), cParty.end(), [aId](const Player* apMember) { return apMember->GetId() == aId; });
        };
        downed.Told.erase(std::remove_if(downed.Told.begin(), downed.Told.end(), [&cInParty](const uint32_t aId) { return !cInParty(aId); }), downed.Told.end());
        for (Player* pMember : cParty)
        {
            if (std::find(downed.Told.begin(), downed.Told.end(), pMember->GetId()) != downed.Told.end())
                continue;
            downed.Told.push_back(pMember->GetId());

            NotifyDownedState notify{};
            notify.PlayerId = cId;
            notify.Change = NotifyDownedState::kDown;
            notify.SecondsLeft = static_cast<uint32_t>(std::max<int64_t>(0, std::chrono::ceil<std::chrono::seconds>(downed.Until - cNow).count()));
            pMember->Send(notify);
            spdlog::info(
                "Revive: told player {} '{}', new in the party, that player {} '{}' is down ({} s left)", pMember->GetId(), pMember->GetUsername().c_str(), cId,
                pPlayer->GetUsername().c_str(), notify.SecondsLeft);
        }
    }
}
