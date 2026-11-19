#include <TiltedOnlinePCH.h>

#include <Services/PresenceService.h>

#include <Copies/CopyRegistry.h>
#include <World.h>
#include <Events/UpdateEvent.h>
#include <Games/ActorExtension.h>
#include <Messages/RequestActorPresence.h>
#include <Services/CombatService.h>
#include <Services/TransportService.h>

PresenceService::PresenceService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_updateConnection(aDispatcher.sink<UpdateEvent>().connect<&PresenceService::OnUpdate>(this))
{
}

void PresenceService::OnUpdate(const UpdateEvent&) noexcept
{
    // The whole list each time, so a lost or late report is put right by the next one.
    // Only once the server has accepted this player: it kicks a connection that sends anything before.
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextReport || !m_transport.IsOnline())
        return;
    m_nextReport = cNow + std::chrono::seconds(2);

    const TiltedPhoques::Set<uint32_t> standIns = CopyRegistry::StandInCopies();
    const CombatService& combat = m_world.GetCombatService();

    RequestActorPresence request{};
    const auto add = [this, &request, &standIns, &combat](const entt::entity aEntity, const uint32_t aServerId, const bool aOwn)
    {
        const auto* pFormId = m_world.try_get<FormIdComponent>(aEntity);
        Actor* pActor = pFormId ? Cast<Actor>(TESForm::GetById(pFormId->Id)) : nullptr;
        const ActorExtension* pExtension = pActor ? pActor->GetExtension() : nullptr;
        // Only what is drawn here: a disabled copy waiting to be freed, or one without 3D, is not seen.
        if (!pExtension || pExtension->IsPlayer() || pActor->IsDisabled() || !pActor->GetNiNode())
            return;

        ActorPresence presence{};
        presence.ServerId = aServerId;
        presence.Kind = aOwn ? ActorPresenceKind::Own : standIns.contains(pActor->formID) ? ActorPresenceKind::StandIn : ActorPresenceKind::Copy;
        presence.Dead = pActor->IsDead();
        // The server keeps it, so a game that takes the character over carries the fight on.
        if (aOwn)
            presence.CombatTarget = combat.FightOf(pActor);
        request.Actors.push_back(presence);
    };

    const auto localView = m_world.view<LocalComponent>();
    for (const auto cEntity : localView)
        add(cEntity, localView.get<LocalComponent>(cEntity).Id, true);
    const auto remoteView = m_world.view<RemoteComponent>();
    for (const auto cEntity : remoteView)
        add(cEntity, remoteView.get<RemoteComponent>(cEntity).Id, false);

    if (!request.Actors.empty())
        m_transport.Send(request);
}
