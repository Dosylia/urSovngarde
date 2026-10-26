#include <TiltedOnlinePCH.h>
#include <PerfScope.h>

#include <Services/ActorValueService.h>
#include <World.h>
#include <Forms/ActorValueInfo.h>
#include <Games/References.h>
#include <Components.h>
#include <Systems/InterpolationSystem.h>
#include <EpochMiss.h>

#include <Events/UpdateEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ClashEvent.h>
#include <PlayerCharacter.h>
#include <Games/Skyrim/DeathCause.h>
#include <Services/PvpProjectiles.h>

#include <Messages/NotifyActorValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/RequestDeathStateChange.h>

#include <misc/ActorValueOwner.h>

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_world.on_construct<LocalComponent>().connect<&ActorValueService::OnLocalComponentAdded>(this);
    m_dispatcher.sink<DisconnectedEvent>().connect<&ActorValueService::OnDisconnected>(this);
    m_dispatcher.sink<ActorRemovedEvent>().connect<&ActorValueService::OnActorRemoved>(this);
    m_dispatcher.sink<UpdateEvent>().connect<&ActorValueService::OnUpdate>(this);
    m_dispatcher.sink<NotifyActorValueChanges>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_dispatcher.sink<NotifyActorMaxValueChanges>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_dispatcher.sink<HealthChangeEvent>().connect<&ActorValueService::OnHealthChange>(this);
    m_dispatcher.sink<NotifyHealthChangeBroadcast>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_dispatcher.sink<ClashEvent>().connect<&ActorValueService::OnClash>(this);
    m_dispatcher.sink<NotifyDeathStateChange>().connect<&ActorValueService::OnDeathStateChange>(this);
}

// The least health a remote player's copy is ever given (see StandingValues in CharacterService).
// A copy at 1 health goes down on the next hit and stops being drawn.
constexpr float kRemotePlayerHealthFloor = 25.f;

// The least health an NPC copy is given: only its owner's death word kills it.
constexpr float kNpcCopyHealthFloor = 1.f;

static bool IsNpcCopy(Actor* apActor) noexcept
{
    const ActorExtension* pExtension = apActor ? apActor->GetExtension() : nullptr;
    return pExtension && pExtension->IsRemote() && !pExtension->IsPlayer();
}

// Once per copy, so that a fight leaves a few lines.
static void SayNpcCopyKeptUp(Actor* apActor, float aHealth) noexcept
{
    static TiltedPhoques::Set<uint32_t> s_said;
    if (s_said.size() > 256)
        s_said.clear();
    if (s_said.insert(apActor->formID).second)
        spdlog::info("Death sync: NPC copy {:X} kept at {:.0f} health instead of {:.0f}; its owner's game decides whether it dies", apActor->formID, kNpcCopyHealthFloor, aHealth);
}

// The form id of player aPlayerId's copy in this game, or 0 when it has none.
static uint32_t PlayerCopyFormId(uint32_t aPlayerId) noexcept
{
    auto view = World::Get().view<PlayerComponent, FormIdComponent>();
    for (const auto cEntity : view)
        if (view.get<PlayerComponent>(cEntity).Id == aPlayerId)
            return view.get<FormIdComponent>(cEntity).Id;
    return 0;
}

void ActorValueService::CreateActorValuesComponent(const entt::entity aEntity, Actor* apActor) noexcept
{
    auto& actorValuesComponent = m_world.emplace_or_replace<ActorValuesComponent>(aEntity);

    for (int i = 0; i < ActorValueInfo::kActorValueCount; i++)
    {
        float value = apActor->GetActorValue(i);
        actorValuesComponent.CurrentActorValues.ActorValuesList.insert({i, value});
        float maxValue = apActor->GetActorPermanentValue(i);
        actorValuesComponent.CurrentActorValues.ActorMaxValuesList.insert({i, maxValue});
    }
}

void ActorValueService::OnLocalComponentAdded(entt::registry& aRegistry, const entt::entity aEntity) noexcept
{
    const auto& formIdComponent = aRegistry.get<FormIdComponent>(aEntity);
    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (pActor != NULL)
    {
        auto& localComponent = aRegistry.get<LocalComponent>(aEntity);
        localComponent.IsDead = pActor->IsDead();
        localComponent.IsWeaponDrawn = pActor->actorState.IsWeaponDrawn();
        CreateActorValuesComponent(aEntity, pActor);
    }
}

void ActorValueService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    // TODO: this crashes sometimes, no clue why
    m_world.clear<ActorValuesComponent>();
    m_heldHits.clear();
    m_clashes.clear();
}

void ActorValueService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    const uint32_t formId = acEvent.FormId;

    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [view, formId](auto entity)
        {
            const auto& formIdComponent = view.get<FormIdComponent>(entity);

            return formIdComponent.Id == formId;
        });

    if (it != std::end(view))
        m_world.remove<ActorValuesComponent>(*it);
}

void ActorValueService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PerfScope perfScope("ActorValueService::OnUpdate");

    RunSmallHealthUpdates();
    RunHeldHits();
    RunDeathStateUpdates();
    RunActorValuesUpdates();
}

void ActorValueService::BroadcastActorValues() noexcept
{
    if (!m_transport.IsConnected())
        return;

    // Only changes are sent, so a copy that misses one stays wrong until the next change.
    // A player's health, magicka and stamina are therefore re-sent every three seconds.
    static std::chrono::steady_clock::time_point s_nextRefresh{};
    const auto refreshNow = std::chrono::steady_clock::now();
    const bool cRefresh = refreshNow >= s_nextRefresh;
    if (cRefresh)
        s_nextRefresh = refreshNow + 3s;

    auto view = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();

    for (auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        auto* pForm = TESForm::GetById(formIdComponent.Id);
        auto* pActor = Cast<Actor>(pForm);

        if (!pActor)
            continue;

        auto& localComponent = view.get<LocalComponent>(entity);
        auto& actorValuesComponent = view.get<ActorValuesComponent>(entity);

        RequestActorValueChanges requestValueChanges;
        requestValueChanges.Id = localComponent.Id;
        requestValueChanges.OwnershipEpoch = localComponent.OwnershipEpoch;
        RequestActorMaxValueChanges requestMaxValueChanges;
        requestMaxValueChanges.Id = localComponent.Id;
        requestMaxValueChanges.OwnershipEpoch = localComponent.OwnershipEpoch;

        bool isPlayer = pActor->GetExtension() && pActor->GetExtension()->IsPlayer();

        for (int i = 0; i < ActorValueInfo::kActorValueCount; i++)
        {
            if (isPlayer && i == ActorValueInfo::kDragonSouls)
                continue;
            
            float newValue = pActor->GetActorValue(i);
            float oldValue = actorValuesComponent.CurrentActorValues.ActorValuesList[i];
            if (newValue != oldValue)
            {
                requestValueChanges.Values.insert({i, newValue});
                actorValuesComponent.CurrentActorValues.ActorValuesList[i] = newValue;
            }

            float newMaxValue = pActor->GetActorPermanentValue(i);
            float oldMaxValue = actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i];
            if (newMaxValue != oldMaxValue)
            {
                requestMaxValueChanges.Values.insert({i, newMaxValue});
                actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i] = newMaxValue;
            }
        }

        // Players only: NPC copies are corrected in OnActorValueChanges, and there are far more of them.
        if (cRefresh && isPlayer)
        {
            for (const uint32_t key : {uint32_t(ActorValueInfo::kHealth), uint32_t(ActorValueInfo::kMagicka), uint32_t(ActorValueInfo::kStamina)})
            {
                requestValueChanges.Values[key] = pActor->GetActorValue(key);
                requestMaxValueChanges.Values[key] = pActor->GetActorPermanentValue(key);
            }
        }

        if (requestValueChanges.Values.size() > 0)
        {
            m_transport.Send(requestValueChanges);
        }

        if (requestMaxValueChanges.Values.size() > 0)
        {
            m_transport.Send(requestMaxValueChanges);
        }
    }
}

void ActorValueService::OnHealthChange(const HealthChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto hitteeIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.HitteeId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (hitteeIt == std::end(view))
    {
        spdlog::warn("Health change event form id component not found, form id: {:X}", acEvent.HitteeId);
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*hitteeIt);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    uint32_t serverId = serverIdRes.value();

    if (acEvent.DeltaHealth > -1.0f && acEvent.DeltaHealth < 1.0f)
    {
        if (m_smallHealthChanges.find(serverId) == m_smallHealthChanges.end())
            m_smallHealthChanges[serverId] = acEvent.DeltaHealth;
        else
            m_smallHealthChanges[serverId] += acEvent.DeltaHealth;
        return;
    }

    RequestHealthChangeBroadcast requestHealthChange;
    requestHealthChange.Id = serverId;
    requestHealthChange.DeltaHealth = acEvent.DeltaHealth;
    requestHealthChange.Tick = m_transport.GetClock().GetCurrentTick();

    m_transport.Send(requestHealthChange);

    spdlog::debug("Sent out delta health through collection: {:X}:{:f}", serverId, acEvent.DeltaHealth);
}

void ActorValueService::RunSmallHealthUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    if (!m_smallHealthChanges.empty())
    {
        for (auto& value : m_smallHealthChanges)
        {
            RequestHealthChangeBroadcast requestHealthChange;
            requestHealthChange.Id = value.first;
            requestHealthChange.DeltaHealth = value.second;
            requestHealthChange.Tick = m_transport.GetClock().GetCurrentTick();

            m_transport.Send(requestHealthChange);

            spdlog::debug("Sent out delta health through timer, {:X}:{:f}", value.first, value.second);
        }

        m_smallHealthChanges.clear();
    }
}

void ActorValueService::RunDeathStateUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto localView = m_world.view<FormIdComponent, LocalComponent>();

    for (auto entity : localView)
    {
        const auto& formIdComponent = localView.get<FormIdComponent>(entity);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        auto& localComponent = localView.get<LocalComponent>(entity);

        bool isDead = pActor->IsDead();
        if (isDead != localComponent.IsDead)
        {
            localComponent.IsDead = isDead;
            spdlog::info("Death sync: local actor {:X} is now {}, telling the server", pActor->formID, isDead ? "dead" : "alive");

            RequestDeathStateChange requestChange;
            requestChange.Id = localComponent.Id;
            requestChange.OwnershipEpoch = localComponent.OwnershipEpoch;
            requestChange.IsDead = isDead;

            m_transport.Send(requestChange);
        }
    }
}

void ActorValueService::RunActorValuesUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    BroadcastActorValues();
}

namespace
{
// The defender's rule: the defender's game decides whether a hit was blocked.
// The other player's copy shows here as late as its playback clock plays it, so a hit at tick t is seen at t plus that delay.
// The hit is held until then, and dropped if this player's hand or held item met that player's weapon around then.
// The delay before the copy's first update arrived:
constexpr uint64_t kDefaultHitSeenAfter = 225;
// Blades meet before the hit lands, and a contact is only taken in the next frames.
// Measured between two headsets at a 225 ms delay, a blocking clash came 120-290 ms before the hit's own tick.
// So the blades crossed up to about 515 ms before the hit on the attacker's side, whatever the delay.
// A clash counts from 525 ms before the hit was seen here up to 100 ms after.
constexpr uint64_t kBlockBefore = 525;
constexpr uint64_t kBlockAfter = 100;
} // namespace

void ActorValueService::OnClash(const ClashEvent& acEvent) noexcept
{
    m_clashes.push_back({acEvent.FormId, acEvent.Tick});
}

void ActorValueService::RunHeldHits() noexcept
{
    if (m_heldHits.empty() && m_clashes.empty())
        return;

    const uint64_t cNow = m_transport.GetClock().GetCurrentTick();
    // Kept a while: a hit can arrive late.
    m_clashes.erase(std::remove_if(m_clashes.begin(), m_clashes.end(), [cNow](const auto& acClash) { return acClash.second + 30000 < cNow; }), m_clashes.end());

    for (auto it = m_heldHits.begin(); it != m_heldHits.end();)
    {
        // The attacker's copy, and how far behind the attacker it is shown here right now.
        uint32_t copy = 0;
        uint64_t seenAfter = kDefaultHitSeenAfter;
        auto view = m_world.view<FormIdComponent, PlayerComponent>();
        for (const auto entity : view)
            if (view.get<PlayerComponent>(entity).Id == it->AttackerPlayerId)
            {
                copy = view.get<FormIdComponent>(entity).Id;
                if (const auto* pInterpolation = m_world.try_get<InterpolationComponent>(entity))
                    if (const uint64_t cShown = InterpolationSystem::GetPlaybackTick(*pInterpolation); cShown && cShown < cNow)
                        seenAfter = std::clamp<uint64_t>(cNow - cShown, 16, 400);
                break;
            }

        const uint64_t cSeen = it->Tick + seenAfter;
        // A tick from the future (a clock not yet in step) is not waited for.
        if (cNow < cSeen + kBlockAfter && it->Tick < cNow + 5000)
        {
            ++it;
            continue;
        }

        const auto clash = std::find_if(m_clashes.begin(), m_clashes.end(), [copy, cSeen](const auto& acClash)
                                        { return copy && acClash.first == copy && acClash.second + kBlockBefore >= cSeen && acClash.second <= cSeen + kBlockAfter; });
        if (clash != m_clashes.end())
        {
            const int64_t cOffset = static_cast<int64_t>(clash->second) - static_cast<int64_t>(cSeen);
            spdlog::info("Defender's rule: a hit of {:.0f} from player {} is blocked: our weapon met theirs {} ms {} it was seen here", -it->DeltaHealth, it->AttackerPlayerId,
                         std::abs(cOffset), cOffset <= 0 ? "before" : "after");
        }
        else
        {
            spdlog::info("Defender's rule: a hit of {:.0f} from player {} lands, {} ms after it happened; nothing of ours met their weapon", -it->DeltaHealth, it->AttackerPlayerId,
                         static_cast<int64_t>(cNow) - static_cast<int64_t>(it->Tick));
            ApplyHealthChange(*it);
        }
        it = m_heldHits.erase(it);
    }
}

void ActorValueService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) noexcept
{
#ifdef SKYRIMVR
    // Another player's hit on this player waits for the defender's rule (RunHeldHits).
    if (acMessage.DeltaHealth < 0.f && acMessage.AttackerPlayerId)
    {
        Actor* pActor = Utils::GetByServerId<Actor>(acMessage.Id);
        if (pActor && pActor == PlayerCharacter::Get())
        {
            // A ranged hit: that player's projectile is replayed here, and its hit or miss here decides (the defender's own copy).
            if (const uint32_t cCopy = PlayerCopyFormId(acMessage.AttackerPlayerId); cCopy && PvpProjectiles::Recent(cCopy))
            {
                spdlog::info("PvP: a hit of {:.0f} from player {} not taken: its projectile is replayed here, and that copy's hit decides", -acMessage.DeltaHealth,
                             acMessage.AttackerPlayerId);
                return;
            }
            NotifyHealthChangeBroadcast held = acMessage;
            if (!held.Tick)
                held.Tick = m_transport.GetClock().GetCurrentTick();
            m_heldHits.push_back(held);
            return;
        }
    }
#endif
    ApplyHealthChange(acMessage);
}

void ActorValueService::ApplyHealthChange(const NotifyHealthChangeBroadcast& acMessage) const noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.Id);
    if (!pActor)
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.Id);
        return;
    }

    const float cHealthBefore = pActor->GetActorValue(ActorValueInfo::kHealth);
    float newHealth = cHealthBefore + acMessage.DeltaHealth;

    // A player's copy is essential and goes down at zero, but only its owner's game decides when that player is down.
    // So the copy keeps a margin above zero, enough to take another hit.
    if (pActor->GetExtension() && pActor->GetExtension()->IsRemotePlayer() && newHealth < kRemotePlayerHealthFloor)
    {
        spdlog::info("Remote player {:X} copy kept at {:.0f} health instead of {:.0f}; its owner decides", pActor->formID, kRemotePlayerHealthFloor, newHealth);
        newHealth = kRemotePlayerHealthFloor;
    }
    // Nor does a health value alone kill an NPC copy: its owner's death word does (OnDeathStateChange).
    // Killed here while its owner's actor lived on, the copy lay dead where it fell while the real one fought on.
    else if (IsNpcCopy(pActor) && newHealth < kNpcCopyHealthFloor)
    {
        SayNpcCopyKeptUp(pActor, newHealth);
        newHealth = kNpcCopyHealthFloor;
    }

    pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, newHealth);

    // Another player's blow on this player: the death card may name them.
    if (acMessage.DeltaHealth < 0.f && acMessage.AttackerPlayerId && pActor == PlayerCharacter::Get())
        DeathCause::RecordPlayerHit(acMessage.AttackerPlayerId);

    const float health = pActor->GetActorValue(ActorValueInfo::kHealth);

    // Another player's blow on an actor other than this player: what it did here, so that one that never kills can be told apart.
    if (acMessage.DeltaHealth < 0.f && acMessage.AttackerPlayerId && pActor != PlayerCharacter::Get())
    {
        const ActorExtension* pExtension = pActor->GetExtension();
        spdlog::info("Death sync: player {}'s blow of {:.0f} on {} {:X}: health {:.0f} to {:.0f}{}{}{}", acMessage.AttackerPlayerId, -acMessage.DeltaHealth,
                     !pExtension ? "actor" : pExtension->IsRemotePlayer() ? "player copy" : pExtension->IsRemote() ? "NPC copy" : "this game's NPC", pActor->formID, cHealthBefore,
                     health, pActor->IsDead() ? ", already dead" : "", pActor->parentCell ? "" : ", in no cell here", pActor->GetNiNode() ? "" : ", no 3D here");
    }

    if (!pActor->IsDead() && health <= 0.f)
    {
        ActorExtension* pExtension = pActor->GetExtension();
        // Players should never be killed.
        // Nor should an actor that is bleeding out: that downed state is recoverable, and essential NPCs stay there.
        if (pExtension && !pExtension->IsPlayer() && !pActor->actorState.IsBleedingOut())
        {
            // An essential NPC refuses to die, so IsDead() stays false and every later health broadcast would try again.
            // After three refusals the actor is left to its owner; repeated kills only disturb its ragdoll and recovery.
            static TiltedPhoques::Map<uint32_t, uint32_t> s_refused;
            auto& refusals = s_refused[pActor->formID];
            if (refusals < 3)
            {
                pActor->Kill();
                if (pActor->IsDead())
                {
                    s_refused.erase(pActor->formID);
                    spdlog::info("Death sync: health broadcast killed actor {:X}", pActor->formID);
                }
                else if (++refusals == 3)
                    spdlog::warn("Death sync: actor {:X} refuses to die (essential, or protected); leaving it to its owner", pActor->formID);
            }
        }
    }

    StartCombatWithAttacker(pActor, acMessage.AttackerPlayerId, acMessage.DeltaHealth);

    // TODO(cosideci): find fix for player health sync so this can be used again
    /*
    if (pActor->GetExtension()->IsRemotePlayer())
        World::Get().GetOverlayService().SetPlayerHealthPercentage(pActor->formID);
    */
}

void ActorValueService::StartCombatWithAttacker(Actor* apActor, uint32_t aAttackerPlayerId, float aDeltaHealth) const noexcept
{
    // An NPC's combat runs on its owner, so a hit from another player must put that player into it.
    // Otherwise an NPC hostile to one player ignores the other, however hard they hit it.
    if (!apActor || !aAttackerPlayerId)
        return;

    // Damage only. The client adds the delta, so a hit is negative; a heal or a buff must never start a fight.
    if (aDeltaHealth >= 0.f)
        return;

    // Only the owner runs this actor's AI, and only it can usefully start combat. Players are never made to fight.
    ActorExtension* pExtension = apActor->GetExtension();
    if (!pExtension || !pExtension->IsLocal() || pExtension->IsPlayer())
        return;

    if (apActor->IsDead() || apActor->actorState.IsBleedingOut())
        return;

    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    const auto it = std::find_if(view.begin(), view.end(), [view, aAttackerPlayerId](auto aEntity)
                                 { return view.get<PlayerComponent>(aEntity).Id == aAttackerPlayerId; });
    if (it == view.end())
        return;

    Actor* pAttacker = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(*it).Id));
    if (!pAttacker || pAttacker == apActor)
        return;

    // A flurry of arrows would otherwise restart combat every frame.
    static TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_startedAt;
    const auto now = std::chrono::steady_clock::now();
    for (auto entry = s_startedAt.begin(); entry != s_startedAt.end();)
        entry = now - entry->second > std::chrono::seconds(30) ? s_startedAt.erase(entry) : std::next(entry);

    auto& startedAt = s_startedAt[apActor->formID];
    if (startedAt.time_since_epoch().count() && now - startedAt < std::chrono::seconds(3))
        return;
    startedAt = now;

    apActor->StartCombatEx(pAttacker);
    spdlog::info("Combat: actor {:X} now also fights player {} (copy {:X}), which hit it for {:.0f}", apActor->formID, aAttackerPlayerId, pAttacker->formID, -aDeltaHealth);
}

void ActorValueService::OnActorValueChanges(const NotifyActorValueChanges& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    // Separated from the epoch test so a stale epoch can be told apart from a character we simply do not have.
    const auto itor = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
    { return view.get<RemoteComponent>(entity).Id == acMessage.Id; });

    if (itor != std::end(view))
    {
        const auto& remoteFound = view.get<RemoteComponent>(*itor);
        if (acMessage.OwnershipEpoch == 0 || remoteFound.OwnershipEpoch != acMessage.OwnershipEpoch)
        {
            ReportEpochMiss("an actor value change", acMessage.Id, acMessage.OwnershipEpoch, remoteFound.OwnershipEpoch);
            return;
        }
    }

    if (itor == std::end(view))
        return;

    auto& formIdComponent = view.get<FormIdComponent>(*itor);
    Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (!pActor)
        return;

    const bool isRemotePlayer = pActor->GetExtension() && pActor->GetExtension()->IsRemotePlayer();

    for (auto [key, value] : acMessage.Values)
    {
        // Syncing dragon souls triggers "Dragon soul collected" event
        if (key == ActorValueInfo::kDragonSouls)
            continue;

        // A player's copy never takes invisibility (actor value 54): it would stay hittable but undrawn until a reconnect.
        // Whatever hides the owner on his own screen stays there.
        if (isRemotePlayer && key == ActorValueInfo::kInvisibility)
        {
            if (value != 0.f)
                spdlog::info("Remote player {:X} copy refused invisibility {:.2f} from its owner; a copy is always drawn", pActor->formID, value);
            continue;
        }

        // A player's copy takes its owner's health as the truth.
        // With damage deltas alone it never heals, and at zero the essential copy lies down for good.
        // NPC copies keep the delta path, which also carries their deaths.
        if (key == ActorValueInfo::kHealth)
        {
            if (pActor->IsDead())
                continue;

            if (!isRemotePlayer)
            {
                // An NPC's health here is only the sum of the damage deltas that arrived, so it drifts when one is lost.
                // Only a gap that matters is corrected: the delta path carries deaths, and the two would fight every frame.
                // Corrections are logged, to show whether deltas are being dropped.
                const float current = pActor->GetActorValue(ActorValueInfo::kHealth);
                const float gap = std::abs(current - value);
                constexpr float cWorthCorrecting = 25.f;
                if (gap < cWorthCorrecting)
                    continue;

                static std::chrono::steady_clock::time_point s_nextSaid{};
                const auto now = std::chrono::steady_clock::now();
                if (now >= s_nextSaid)
                {
                    s_nextSaid = now + 5s;
                    spdlog::info("NPC {:X} health corrected from {:.0f} to its owner's {:.0f}", pActor->formID, current, value);
                }

                pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, value);
                continue;
            }

            const float current = pActor->GetActorValue(ActorValueInfo::kHealth);
            if (std::abs(current - value) >= 10.f)
                spdlog::info("Remote player {:X} copy health corrected from {:.0f} to the owner's {:.0f}", pActor->formID, current, value);

            // The owner's death arrives as health at or below zero, which would put the copy into an unrecoverable bleedout.
            // A copy never goes down: the owner's game decides, and the respawn replaces the copy.
            if (value < kRemotePlayerHealthFloor)
            {
                spdlog::info("Remote player {:X} copy kept at {:.0f} health instead of the owner's {:.0f}; a copy never goes down", pActor->formID, kRemotePlayerHealthFloor, value);
                value = kRemotePlayerHealthFloor;
            }
        }

        spdlog::debug("Actor value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

        if (key == ActorValueInfo::kStamina || key == ActorValueInfo::kMagicka || key == ActorValueInfo::kHealth)
        {
            pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, key, value);
            continue;
        }
        pActor->SetActorValue(key, value);
    }
}

void ActorValueService::OnActorMaxValueChanges(const NotifyActorMaxValueChanges& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    // Separated from the epoch test so a stale epoch can be told apart from a character we simply do not have.
    const auto it = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
    { return view.get<RemoteComponent>(entity).Id == acMessage.Id; });

    if (it != std::end(view))
    {
        const auto& remoteFound = view.get<RemoteComponent>(*it);
        if (acMessage.OwnershipEpoch == 0 || remoteFound.OwnershipEpoch != acMessage.OwnershipEpoch)
        {
            ReportEpochMiss("a health broadcast", acMessage.Id, acMessage.OwnershipEpoch, remoteFound.OwnershipEpoch);
            return;
        }
    }

    if (it == std::end(view))
        return;

    auto& formIdComponent = view.get<FormIdComponent>(*it);
    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));

    if (!pActor)
        return;

    for (const auto& [key, value] : acMessage.Values)
    {
        if (key == ActorValueInfo::kDragonSouls)
            continue;

        spdlog::debug("Actor max value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

        pActor->ForceActorValue(ActorValueOwner::ForceMode::PERMANENT, key, value);
    }
}

void ActorValueService::OnDeathStateChange(const NotifyDeathStateChange& acMessage) const noexcept
{
    // A copy still waiting to be made, or for its 3D, has no form id yet or no body to kill, but it must not miss the death.
    auto view = m_world.view<RemoteComponent>();

    // Separated from the epoch test so a stale epoch can be told apart from a character we simply do not have.
    const auto it = std::find_if(std::begin(view), std::end(view), [&acMessage, view](entt::entity entity)
    { return view.get<RemoteComponent>(entity).Id == acMessage.Id; });

    if (it != std::end(view))
    {
        const auto& remoteFound = view.get<RemoteComponent>(*it);
        if (acMessage.OwnershipEpoch == 0 || remoteFound.OwnershipEpoch != acMessage.OwnershipEpoch)
        {
            ReportEpochMiss("a death state change", acMessage.Id, acMessage.OwnershipEpoch, remoteFound.OwnershipEpoch);
            return;
        }
    }

    // Every exit below logs why: "no copy here", "no actor" and "already dead here" are different bugs.
    // Rate limited; a death arrives once per actor, so this is a handful of lines per session.
    const auto say = [&acMessage](const char* acpWhy)
    {
        static std::chrono::steady_clock::time_point s_next{};
        const auto now = std::chrono::steady_clock::now();
        if (now < s_next)
            return;
        s_next = now + std::chrono::seconds(1);
        spdlog::info("Death sync: {} for server id {:X} arrived and was not applied: {}", acMessage.IsDead ? "a death" : "a revival", acMessage.Id, acpWhy);
    };

    if (it == std::end(view))
    {
        say("no copy of that character here");
        return;
    }

    // Kept even when the copy is already in that state here: the corpse placement waits for this word.
    view.get<RemoteComponent>(*it).DeadAtOwner = acMessage.IsDead;

    // The copy is killed or brought back once it has its 3D, from its spawn request (CharacterService::RunRemoteUpdates).
    if (auto* pWaiting = m_world.try_get<WaitingFor3D>(*it))
    {
        pWaiting->SpawnRequest.IsDead = acMessage.IsDead;
        spdlog::info("Death sync: server id {:X} is {} at its owner while its copy waits to be made here; it will be made so", acMessage.Id,
                     acMessage.IsDead ? "dead" : "alive");
        return;
    }

    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*it);
    if (!pFormIdComponent)
    {
        say("its copy has no actor in the game");
        return;
    }

    Actor* pActor = Cast<Actor>(TESForm::GetById(pFormIdComponent->Id));

    if (!pActor)
    {
        say("its copy has no actor in the game");
        return;
    }

    ActorExtension* pExtension = pActor->GetExtension();
    // Players should never be killed
    if (pExtension->IsPlayer())
        return;

    if (pActor->IsDead() != acMessage.IsDead)
    {
        acMessage.IsDead ? pActor->Kill() : pActor->Respawn();
        spdlog::info("Death sync: remote actor {:X} set {} by its owner (now dead: {})", pActor->formID, acMessage.IsDead ? "dead" : "alive", pActor->IsDead());
    }
    else
    {
        // Not a failure: the copy died from a hit landed on this side.
        // Its body then falls under this side's ragdoll and lies elsewhere than the owner's.
        spdlog::info("Death sync: remote actor {:X} was already {} here when its owner's word arrived", pActor->formID, acMessage.IsDead ? "dead" : "alive");
    }
}
