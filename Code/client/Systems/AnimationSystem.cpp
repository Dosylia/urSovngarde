#include <TiltedOnlinePCH.h>

#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <Forms/TESNPC.h>
#include <Forms/ActorValueInfo.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Messages/RequestActionEvents.h>
#include <Structs/ReferenceUpdate.h>

#include <Components.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <NetImmerse/NiTransform.h>

#ifdef SKYRIMVR
#include <PlayerCharacter.h>
#include <Games/Skyrim/VRBodySync.h>
#include <BSAnimationGraphManager.h>
#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/AnimationGraphDescriptor.h>
#endif

extern thread_local const char* g_animErrorCode;

void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    if (aAnimationComponent.ForeignGraph)
    {
        // The owner's actions belong to another creature's graph (see MarkForeignGraph); nothing here fits.
        actions.clear();
        return;
    }

    const auto it = std::begin(actions);
    if (it != std::end(actions) && it->Tick <= aTick)
    {
        // Check if animation graph is ready before attempting to play animations
        if (!apActor->animationGraphHolder.IsReady())
        {
            // Animation graph not ready, keep the action in queue and try again later
            return;
        }
        // A dead copy plays nothing more, or it stands back up.
        // The owner's actions are replayed 300 ms late, so ones recorded while it was alive still arrive after it died here.
        if (apActor->actorState.IsDead())
        {
            aAnimationComponent.LastRanAction = *it;
            if (aAnimationComponent.ReplayCount > 0)
                aAnimationComponent.ReplayCount--;
            actions.pop_front();
            return;
        }

        if (aAnimationComponent.ReplayCount > 0 && aAnimationComponent.ResetAnimationGraphForReplay)
        {
            apActor->animationGraphHolder.RevertAnimationGraphManager();
            aAnimationComponent.ResetAnimationGraphForReplay = false;
        }

        const auto& first = *it;

        const auto actionId = first.ActionId;
        const auto targetId = first.TargetId;

        const auto pAction = Cast<BGSAction>(TESForm::GetById(actionId));
        const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(targetId));

        // The owner's state word also carries the life state (bits 21-24).
        // Late actions recorded while the body was alive would stand a corpse back up, so once dying or dead that part is kept.
        // Death messages own it.
        constexpr uint32_t cLifeStateMask = 0x1E00000;
        // Every copy keeps its own knock state (bits 25-27): its fall and its get-up run on this game's physics.
        // With the owner's written over it, a copy thrown to the floor here would count as standing and never get up (live-copy-knockdown).
        constexpr uint32_t cKnockStateMask = 0xE000000;
        // Another player's copy never goes down (ActorValueService keeps it at 25 health).
        // It keeps its own life state too, and the owner's "Ragdoll" is not replayed on it.
        // A replayed fall would leave the copy on the floor, since the later "GetUpBegin" does not stand it up.
        const bool cPlayerCopy = apActor->GetExtension()->IsRemotePlayer();
        const uint32_t cOwnStateBefore = apActor->actorState.flags1;
        uint32_t keptMask = cKnockStateMask;
        if (cPlayerCopy || apActor->actorState.IsDeadOrDying())
            keptMask |= cLifeStateMask;
        apActor->actorState.flags1 = (first.State1 & ~keptMask) | (apActor->actorState.flags1 & keptMask);
        apActor->actorState.flags2 = first.State2;

        if (cPlayerCopy && !first.EventName.empty() && _stricmp(first.EventName.c_str(), "Ragdoll") == 0)
        {
            static std::chrono::steady_clock::time_point s_nextSaid{};
            if (std::chrono::steady_clock::now() >= s_nextSaid)
            {
                s_nextSaid = std::chrono::steady_clock::now() + 10s;
                spdlog::info("ReplayDiag: 'Ragdoll' not replayed on player copy {:X}; a copy never goes down", apActor->formID);
            }
            aAnimationComponent.LastRanAction = first;
            if (aAnimationComponent.ReplayCount > 0)
                aAnimationComponent.ReplayCount--;
            actions.pop_front();
            return;
        }

        apActor->LoadAnimationVariables(first.Variables);

        aAnimationComponent.LastRanAction = first;

        // Play the animation
        TESActionData actionData(first.Type & 0x3, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(first.EventName.c_str());
        actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(first.IdleId));
        actionData.someFlag = ((first.Type & 0x4) != 0) ? 1 : 0;

        const auto result = ActorMediator::Get()->ForceAction(&actionData);

        // The game can refuse "bleedOutStop" as an action, which leaves the copy down while its owner is up again.
        // Sending the graph event itself takes the copy out of the pose, as on the owner's side.
        if (!result && !cPlayerCopy && !first.EventName.empty() && _stricmp(first.EventName.c_str(), "bleedOutStop") == 0)
        {
            BSFixedString event("bleedOutStop");
            const bool cSent = apActor->SendAnimationEvent(&event);
            // The copy's own state before the owner's was written over it: what the game judged the action on.
            spdlog::info("ReplayDiag: 'bleedOutStop' refused as an action on {:X}; sent to its graph instead ({}); before it the copy had life state {}, knock state {}, "
                         "health {:.0f}; the owner's life state {}",
                         apActor->formID, cSent ? "taken" : "not taken", (cOwnStateBefore >> 21) & 0xF, (cOwnStateBefore >> 25) & 0x7,
                         apActor->GetActorValue(ActorValueInfo::kHealth), (first.State1 >> 21) & 0xF);
        }

        // TEMPORARY diagnostic for remote NPCs sliding while the game refuses most of their moveStart and turnStop replays.
        // For one refused replay per 10 s, it logs everything the refusal could depend on. Remove once understood.
        if (!result)
        {
            static std::chrono::steady_clock::time_point s_nextSample{};
            const auto now = std::chrono::steady_clock::now();
            if (now >= s_nextSample)
            {
                s_nextSample = now + 10s;
                const auto* pBase = Cast<TESNPC>(apActor->baseForm);
                spdlog::info("ReplayDiag: refused '{}' on {:X} ({}): action {:X} resolved {}, idle {:X} resolved {}, type {}, target {:X} resolved {}, graph ready {}, has 3D {}, "
                             "has process {}, flags1 {:#x}, flags2 {:#x}, dead or dying {}, replay left {}, queued {}",
                             first.EventName.c_str(), apActor->formID, pBase ? pBase->fullName.value.AsAscii() : "?", actionId, pAction != nullptr, first.IdleId,
                             actionData.idleForm != nullptr, first.Type, targetId, pTarget != nullptr, apActor->animationGraphHolder.IsReady(),
                             apActor->GetNiNode() != nullptr, apActor->currentProcess != nullptr, apActor->actorState.flags1, apActor->actorState.flags2,
                             apActor->actorState.IsDeadOrDying(), aAnimationComponent.ReplayCount, actions.size());
            }
        }

        if (aAnimationComponent.ReplayCount > 0)
            aAnimationComponent.ReplayCount--;

        actions.pop_front();
    }
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<RemoteAnimationComponent>(aEntity))
        aWorld.remove<RemoteAnimationComponent>(aEntity);
}

// Update plays at most one action per frame, so every path that pushes into this queue trims it here.
// A long queue would replay minutes-old actions, leaving the actor acting out the past (an automaton idle through a fight).
// An action this far behind is not worth playing, so the oldest go and the actor catches up with the present.
void AnimationSystem::TrimBacklog(RemoteAnimationComponent& aAnimationComponent, const uint32_t aFormId) noexcept
{
    constexpr size_t cMaxQueued = 96; // over a second of backlog at the frame rate these replay at
    if (aAnimationComponent.TimePoints.size() <= cMaxQueued)
        return;

    const size_t dropped = aAnimationComponent.TimePoints.size() - cMaxQueued;
    for (size_t i = 0; i < dropped; ++i)
        aAnimationComponent.TimePoints.pop_front();

    if (aAnimationComponent.ReplayCount > dropped)
        aAnimationComponent.ReplayCount -= dropped;
    else
        aAnimationComponent.ReplayCount = 0;

    static std::chrono::steady_clock::time_point s_nextSaid{};
    static uint32_t s_since = 0;
    static size_t s_worst = 0;
    static uint32_t s_worstActor = 0;
    ++s_since;
    if (dropped > s_worst)
    {
        s_worst = dropped;
        s_worstActor = aFormId;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= s_nextSaid)
    {
        s_nextSaid = now + 10s;
        spdlog::warn("Animation replay backlog over {} actions; trimmed {} times, worst {} dropped at once on {:X}. An owner is looping an action.", cMaxQueued, s_since, s_worst, s_worstActor);
        s_since = 0;
        s_worst = 0;
        s_worstActor = 0;
    }
}

void AnimationSystem::AddActionsForReplay(RemoteAnimationComponent& aAnimationComponent,
                                          const ActionReplayChain& acReplay) noexcept
{
    aAnimationComponent.TimePoints.insert(aAnimationComponent.TimePoints.end(), acReplay.Actions.begin(),
                                          acReplay.Actions.end());
    aAnimationComponent.ReplayCount = acReplay.Actions.size();
    aAnimationComponent.ResetAnimationGraphForReplay = acReplay.ResetAnimationGraph;

    TrimBacklog(aAnimationComponent, 0);
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto itor = std::begin(aAnimationComponent.TimePoints);
    const auto end = std::cend(aAnimationComponent.TimePoints);

    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);

    TrimBacklog(aAnimationComponent, 0);
}

#ifdef SKYRIMVR
// Where the graph's "Direction" float sits among the variables the player sends, or -1 when its behavior has none.
// Looked up by name once per behavior descriptor, because a modded behavior can put it elsewhere.
static int PlayerDirectionSlot(Actor* apPlayer) noexcept
{
    static size_t s_hash = 0;
    static int s_slot = -1;
    const size_t cHash = apPlayer->GetExtension()->GraphDescriptorHash;
    if (cHash == s_hash)
        return s_slot;
    s_hash = cHash;
    s_slot = -1;

    const AnimationGraphDescriptor* pDescriptor = AnimationGraphDescriptorManager::Get().GetDescriptor(cHash);
    BSAnimationGraphManager* pManager = nullptr;
    if (!pDescriptor || !apPlayer->animationGraphHolder.GetBSAnimationGraph(&pManager))
        return s_slot;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        // The player's variables are always read from graph 0 (SaveAnimationVariables).
        for (const auto& [index, name] : pManager->DumpAnimationVariables(false, 0))
        {
            if (name != "Direction")
                continue;
            for (size_t i = 0; i < pDescriptor->FloatLookupTable.size(); ++i)
                if (pDescriptor->FloatLookupTable[i] == index)
                    s_slot = static_cast<int>(i);
        }
    }
    pManager->Release();
    return s_slot;
}
#endif

void AnimationSystem::SerializeMovement(World& aWorld, Actor* apActor, ReferenceUpdate& aUpdate) noexcept
{
    auto& movement = aUpdate.UpdatedMovement;

    if (const auto pCell = apActor->parentCell)
        aWorld.GetModSystem().GetServerModId(pCell->formID, movement.CellId.ModId, movement.CellId.BaseId);

    if (const auto pWorldSpace = apActor->GetWorldSpace())
        aWorld.GetModSystem().GetServerModId(pWorldSpace->formID, movement.WorldSpaceId.ModId, movement.WorldSpaceId.BaseId);

    movement.Position = apActor->position;
    // A dead body lies where its ragdoll is, and dragging or throwing it leaves its reference where it died.
    // The others put their copy where this says (VRBodySync::PlaceCorpse), so the 3D root's world position is sent instead.
    if (apActor != PlayerCharacter::Get() && apActor->actorState.IsDead())
    {
        if (const NiNode* pRoot = apActor->GetNiNode())
        {
            // NiAVObject::world is at 0x7C, as VRBodySync and DroppedItemService read it.
            const auto& world = *reinterpret_cast<const NiTransform*>(reinterpret_cast<const uint8_t*>(pRoot) + 0x7C);
            movement.Position = glm::vec3(world.translate.x, world.translate.y, world.translate.z);
        }
    }

    movement.Rotation.x = apActor->rotation.x;
    movement.Rotation.y = apActor->rotation.z;

    apActor->SaveAnimationVariables(movement.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        movement.Direction = apActor->currentProcess->middleProcess->direction;
    }

    // A dragon in the air here is not taken over by another player's game (CharacterService::CanClaimOwnership on the server).
    // Taken over in flight, a dragon falls and dies.
    movement.Flying = apActor->IsDragon() && apActor->actorState.IsFlying();

#ifdef SKYRIMVR
    if (apActor == PlayerCharacter::Get())
    {
        auto* pPlayer = static_cast<PlayerCharacter*>(apActor);

        VRPose pose{};
        float heading = apActor->rotation.z;
        if (!VRBodySync::CaptureLocalPose(pPlayer, pose, heading))
        {
            // The other player then sees this body in the plain animation pose.
            // Logged so it can be told apart from a problem on the receiving side.
            static std::chrono::steady_clock::time_point s_nextWarn{};
            const auto now = std::chrono::steady_clock::now();
            if (now >= s_nextWarn)
            {
                s_nextWarn = now + 10s;
                spdlog::warn("VRBodySync: could not read the local VR pose (skeleton bones not found), the other player sees the animation pose instead");
            }
        }
        else
        {
            // The body's facing is sent as the heading, so a head turn alone does not turn the copy's hips and legs.
            // The game's walk direction is relative to its own heading, which follows the headset, so it is re-based on the body.
            float turn = std::fmod(heading - apActor->rotation.z, glm::two_pi<float>());
            if (turn > glm::pi<float>())
                turn -= glm::two_pi<float>();
            else if (turn < -glm::pi<float>())
                turn += glm::two_pi<float>();
            movement.Rotation.y = heading;
            // The process direction is in radians from -pi to pi: 0 forward, pi/2 right.
            if (apActor->currentProcess && apActor->currentProcess->middleProcess)
            {
                float direction = std::fmod(movement.Direction - turn, glm::two_pi<float>());
                if (direction > glm::pi<float>())
                    direction -= glm::two_pi<float>();
                else if (direction < -glm::pi<float>())
                    direction += glm::two_pi<float>();
                movement.Direction = direction;
            }
            // The graph's Direction is a fraction of a turn clockwise from the heading: 0 forward, 0.25 right.
            if (const int cSlot = PlayerDirectionSlot(apActor); cSlot >= 0 && static_cast<size_t>(cSlot) < movement.Variables.Floats.size())
            {
                float& direction = movement.Variables.Floats[cSlot];
                direction -= turn / glm::two_pi<float>();
                direction -= std::floor(direction);
            }
        }
        aUpdate.UpdatedVRPose = pose;
    }
    else if (apActor->actorState.IsDead())
    {
        // A body this machine owns, within reach of this player: its bones let the others see it dragged, thrown or shoved.
        // It is only sent while the body is moving (CaptureBodyPose returns false otherwise), and far bodies are never read.
        // Measured from where the body lies (movement.Position above), not where it died: a body dragged far away is still in reach.
        constexpr float cReachSquared = 600.f * 600.f;
        const auto* pPlayer = PlayerCharacter::Get();
        if (pPlayer)
        {
            const glm::vec3 toPlayer = static_cast<const glm::vec3&>(movement.Position) - static_cast<glm::vec3>(pPlayer->position);
            if (glm::dot(toPlayer, toPlayer) <= cReachSquared)
            {
                VRPose pose{};
                if (VRBodySync::CaptureBodyPose(apActor, pose))
                    aUpdate.UpdatedVRPose = pose;
            }
        }
    }
#endif
}

void AnimationSystem::SerializeActions(RequestActionEvents& aMessage, LocalComponent& localComponent, LocalAnimationComponent& animationComponent) noexcept
{
    if (animationComponent.Actions.empty())
        return;

    auto latestAction = animationComponent.GetLatestAction();

    if (latestAction)
        localComponent.CurrentAction = latestAction.MoveResult();

    aMessage.Actions[localComponent.Id] = std::move(animationComponent.Actions);
    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 14];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}
