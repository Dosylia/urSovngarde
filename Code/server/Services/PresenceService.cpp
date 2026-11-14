#include <Services/PresenceService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Events/UpdateEvent.h>
#include <Messages/RequestActorPresence.h>

namespace
{
// Longer than a loading screen (8 to 10 s), so actors are not handed away at every door.
constexpr auto cSilence = std::chrono::seconds(15);

// TEMPORARY: removals since the last census, and those made while another game still had the character loaded.
uint32_t s_removals = 0;
uint32_t s_removalsWhileSeen = 0;

const char* PresenceKindName(const ActorPresenceKind aKind) noexcept
{
    switch (aKind)
    {
    case ActorPresenceKind::Own: return "its own";
    case ActorPresenceKind::Copy: return "a copy";
    case ActorPresenceKind::StandIn: return "a stand-in";
    }
    return "unknown";
}
} // namespace

PresenceService::PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&PresenceService::OnUpdate>(this))
    , m_presenceConnection(aDispatcher.sink<PacketEvent<RequestActorPresence>>().connect<&PresenceService::OnActorPresenceRequest>(this))
{
}

bool PresenceService::IsSilent(const Player* apPlayer, const std::chrono::steady_clock::time_point aNow) noexcept
{
    return apPlayer->GetLastMovementAt() != std::chrono::steady_clock::time_point{} && aNow - apPlayer->GetLastMovementAt() > cSilence;
}

std::optional<ActorPresenceKind> PresenceService::LoadedAs(const entt::entity aEntity, const uint32_t aPlayerId, const std::chrono::steady_clock::time_point aNow) const noexcept
{
    const auto* pPresence = m_world.try_get<PresenceComponent>(aEntity);
    if (!pPresence)
        return std::nullopt;
    for (const PresenceComponent::Seen& acSeen : pPresence->SeenBy)
        if (acSeen.PlayerId == aPlayerId && aNow - acSeen.At <= PresenceComponent::kFresh)
            return acSeen.Kind;
    return std::nullopt;
}

// A stand-in is a copy of the character, so the game that has the reference itself goes first.
TiltedPhoques::Vector<Player*> PresenceService::NextOwners(const entt::entity aEntity, const OwnerComponent& acOwner, const CellIdComponent& acCell, const bool aIsDragon,
                                                           const std::chrono::steady_clock::time_point aNow) const noexcept
{
    enum class Has
    {
        Reference,
        StandIn,
        RangeOnly
    };

    TiltedPhoques::Vector<Player*> candidates;
    for (const bool cSilentToo : {false, true})
    {
        for (const Has cHas : {Has::Reference, Has::StandIn, Has::RangeOnly})
        {
            for (Player* pPlayer : m_world.GetPlayerManager())
            {
                if (pPlayer == acOwner.GetOwner() || IsSilent(pPlayer, aNow) != cSilentToo)
                    continue;
                if (std::find(acOwner.InvalidOwners.begin(), acOwner.InvalidOwners.end(), pPlayer) != acOwner.InvalidOwners.end())
                    continue;
                if (std::find(candidates.begin(), candidates.end(), pPlayer) != candidates.end())
                    continue;

                const auto cKind = LoadedAs(aEntity, pPlayer->GetId(), aNow);
                bool takes = false;
                if (cHas == Has::Reference)
                    takes = cKind && *cKind != ActorPresenceKind::StandIn;
                else if (cHas == Has::StandIn)
                    takes = cKind == ActorPresenceKind::StandIn;
                else
                    takes = pPlayer->GetCellComponent().IsInRange(acCell, aIsDragon);
                if (takes)
                    candidates.push_back(pPlayer);
            }
        }
    }
    return candidates;
}

uint32_t PresenceService::FightOf(const entt::entity aEntity) const noexcept
{
    const auto* pPresence = m_world.try_get<PresenceComponent>(aEntity);
    if (!pPresence || !pPresence->CombatTarget || std::chrono::steady_clock::now() - pPresence->CombatTargetAt > PresenceComponent::kFresh)
        return 0;
    const auto cTarget = static_cast<entt::entity>(pPresence->CombatTarget);
    return m_world.valid(cTarget) && m_world.all_of<CharacterComponent>(cTarget) ? pPresence->CombatTarget : 0;
}

void PresenceService::ForgetReport(const entt::entity aEntity, const uint32_t aPlayerId) const noexcept
{
    if (auto* pPresence = m_world.try_get<PresenceComponent>(aEntity))
        std::erase_if(pPresence->SeenBy, [aPlayerId](const PresenceComponent::Seen& acSeen) { return acSeen.PlayerId == aPlayerId; });
}

void PresenceService::ForgetFightWith(const entt::entity aEntity, const entt::entity aCharacter) const noexcept
{
    if (auto* pPresence = m_world.try_get<PresenceComponent>(aEntity); pPresence && pPresence->CombatTarget == World::ToInteger(aCharacter))
        pPresence->CombatTarget = 0;
}

void PresenceService::OnUpdate(const UpdateEvent&) const noexcept
{
    LogPresenceCensus();
}

void PresenceService::OnActorPresenceRequest(const PacketEvent<RequestActorPresence>& acMessage) const noexcept
{
    // Only this game's own entry is touched, so a report never speaks for another game.
    const auto cNow = std::chrono::steady_clock::now();
    const uint32_t cPlayerId = acMessage.pPlayer->GetId();
    for (const ActorPresence& acActor : acMessage.Packet.Actors)
    {
        const auto cEntity = static_cast<entt::entity>(acActor.ServerId);
        if (!m_world.valid(cEntity) || !m_world.all_of<CharacterComponent>(cEntity))
            continue;

        auto& presence = m_world.get_or_emplace<PresenceComponent>(cEntity);
        // Only the owner's game runs the fight, so only its word on it counts.
        const auto* pOwner = m_world.try_get<OwnerComponent>(cEntity);
        if (acActor.Kind == ActorPresenceKind::Own && pOwner && pOwner->GetOwner() == acMessage.pPlayer)
        {
            presence.CombatTarget = acActor.CombatTarget;
            presence.CombatTargetAt = cNow;
        }

        auto& seenBy = presence.SeenBy;
        auto it = std::find_if(seenBy.begin(), seenBy.end(), [cPlayerId](const PresenceComponent::Seen& acSeen) { return acSeen.PlayerId == cPlayerId; });
        if (it == seenBy.end())
            it = seenBy.insert(seenBy.end(), PresenceComponent::Seen{cPlayerId});
        it->Kind = acActor.Kind;
        it->Dead = acActor.Dead;
        it->At = cNow;
        // A player who reconnects comes back under a new id, so old entries would pile up on a character that lives for hours.
        std::erase_if(seenBy, [cNow](const PresenceComponent::Seen& acSeen) { return cNow - acSeen.At > std::chrono::minutes(1); });
    }
}

void PresenceService::LogRemovalWhileSeen(const entt::entity aEntity, const char* acpReason) const noexcept
{
    ++s_removals;
    const auto* pPresence = m_world.try_get<PresenceComponent>(aEntity);
    if (!pPresence)
        return;

    // The owner that let it go is left out: its last report can still be fresh.
    const auto* pOwner = m_world.try_get<OwnerComponent>(aEntity);
    const uint32_t cOwnerId = pOwner && pOwner->GetOwner() ? pOwner->GetOwner()->GetId() : 0;
    const auto cNow = std::chrono::steady_clock::now();
    std::string seen;
    for (const PresenceComponent::Seen& acSeen : pPresence->SeenBy)
    {
        const Player* pPlayer = m_world.GetPlayerManager().GetById(acSeen.PlayerId);
        if (!pPlayer || acSeen.PlayerId == cOwnerId || cNow - acSeen.At > PresenceComponent::kFresh)
            continue;
        seen += fmt::format("{}player {} '{}' as {}{}, {} ms ago", seen.empty() ? "" : "; ", acSeen.PlayerId, pPlayer->GetUsername().c_str(), PresenceKindName(acSeen.Kind),
                            acSeen.Dead ? " (dead)" : "", std::chrono::duration_cast<std::chrono::milliseconds>(cNow - acSeen.At).count());
    }
    if (seen.empty())
        return;

    ++s_removalsWhileSeen;
    spdlog::info("Presence: removing actor {:X} after {} while another game still has it loaded: {}", World::ToInteger(aEntity), acpReason, seen);
}

void PresenceService::LogPresenceCensus() const noexcept
{
    static std::chrono::steady_clock::time_point s_next{};
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < s_next)
        return;
    s_next = cNow + std::chrono::seconds(30);
    if (m_world.GetPlayerManager().Count() == 0)
        return;

    struct Counts
    {
        uint32_t Own{};
        uint32_t Copies{};
        uint32_t StandIns{};
        uint32_t Dead{};
        uint32_t OwnHereOwnedElsewhere{};
        uint32_t CopyHereOwnedHere{};
        uint32_t Lasting{};
    };
    TiltedPhoques::Map<uint32_t, Counts> counts;
    uint32_t characters = 0;
    uint32_t loadedNowhere = 0;

    // A report can be 2 s older than a hand-over, so a disagreement counts as lasting only when the previous census saw it too.
    static TiltedPhoques::Set<uint64_t> s_disagreed;
    TiltedPhoques::Set<uint64_t> disagreed;

    const auto view = m_world.view<OwnerComponent, CharacterComponent>();
    for (const auto cEntity : view)
    {
        if (view.get<CharacterComponent>(cEntity).IsPlayer())
            continue;
        ++characters;

        const Player* pOwner = view.get<OwnerComponent>(cEntity).GetOwner();
        bool loaded = false;
        if (const auto* pPresence = m_world.try_get<PresenceComponent>(cEntity))
        {
            for (const PresenceComponent::Seen& acSeen : pPresence->SeenBy)
            {
                if (cNow - acSeen.At > PresenceComponent::kFresh || !m_world.GetPlayerManager().GetById(acSeen.PlayerId))
                    continue;
                loaded = true;
                Counts& count = counts[acSeen.PlayerId];
                const bool cServerGivesItThem = pOwner && pOwner->GetId() == acSeen.PlayerId;
                switch (acSeen.Kind)
                {
                case ActorPresenceKind::Own: ++count.Own; break;
                case ActorPresenceKind::Copy: ++count.Copies; break;
                case ActorPresenceKind::StandIn: ++count.StandIns; break;
                }
                count.Dead += acSeen.Dead;
                const bool cOwnHereOwnedElsewhere = acSeen.Kind == ActorPresenceKind::Own && !cServerGivesItThem;
                const bool cCopyHereOwnedHere = acSeen.Kind != ActorPresenceKind::Own && cServerGivesItThem;
                count.OwnHereOwnedElsewhere += cOwnHereOwnedElsewhere;
                count.CopyHereOwnedHere += cCopyHereOwnedHere;
                if (cOwnHereOwnedElsewhere || cCopyHereOwnedHere)
                {
                    const uint64_t cKey = (static_cast<uint64_t>(World::ToInteger(cEntity)) << 32) | acSeen.PlayerId;
                    disagreed.insert(cKey);
                    count.Lasting += s_disagreed.contains(cKey);
                }
            }
        }
        loadedNowhere += !loaded;
    }

    for (const Player* pPlayer : m_world.GetPlayerManager())
    {
        const Counts& acCount = counts[pPlayer->GetId()];
        spdlog::info("Presence: player {} '{}' has {} characters loaded: {} its own, {} copies, {} stand-ins, {} of them dead; {} it runs that the server gives to another game, {} "
                     "it shows as a copy that the server gives to it, {} of these two kinds already so at the last census",
                     pPlayer->GetId(), pPlayer->GetUsername().c_str(), acCount.Own + acCount.Copies + acCount.StandIns, acCount.Own, acCount.Copies, acCount.StandIns, acCount.Dead,
                     acCount.OwnHereOwnedElsewhere, acCount.CopyHereOwnedHere, acCount.Lasting);
    }
    s_disagreed = std::move(disagreed);
    spdlog::info("Presence: {} characters on the server, {} of them loaded in no game; {} removed in the last 30 s, {} of them while another game still had them loaded", characters,
                 loadedNowhere, s_removals, s_removalsWhileSeen);
    s_removals = 0;
    s_removalsWhileSeen = 0;
}
