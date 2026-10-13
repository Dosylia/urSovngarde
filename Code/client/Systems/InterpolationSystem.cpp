#include <TiltedOnlinePCH.h>

#include <Systems/InterpolationSystem.h>
#include <Systems/SpawnReveal.h>
#include <Components.h>

#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>

#include <Games/References.h>
#include <Games/TES.h>
#include <Forms/TESNPC.h>
#include <PlayerCharacter.h>
#include <World.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/VRBodySync.h>
#endif

void InterpolationSystem::Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, const uint64_t aTick) noexcept
{
    auto& movements = aInterpolationComponent.TimePoints;

    if (movements.size() < 2)
        return;

    while (movements.size() > 2)
    {
        const auto second = *(++movements.begin());
        if (aTick > second.Tick)
            movements.pop_front();
        else
            break;
    }

    const auto& first = *(movements.begin());
    const auto& second = *(++movements.begin());

#ifdef SKYRIMVR
    // TEMPORARY diagnostic for a playback clock that drifts past the buffered movement. Remove once understood.
    // Every 10 s it logs how far the newest buffered tick sits ahead of the tick being played.
    // Negative means the actor has run out of future and holds its last point; it also counts those actors.
    {
        static int64_t s_maxAhead = INT64_MIN;
        static int64_t s_minAhead = INT64_MAX;
        static uint32_t s_updates = 0;
        static uint32_t s_starved = 0;
        static TiltedPhoques::Map<uint32_t, int64_t> s_starvedActors; // form id -> most negative "ahead" seen
        static std::chrono::steady_clock::time_point s_nextLog = std::chrono::steady_clock::now() + 10s;

        const int64_t ahead = static_cast<int64_t>(movements.back().Tick) - static_cast<int64_t>(aTick);
        // Remote players alone, since the line above mixes in every NPC copy.
        // How far ahead of playback their newest point is says how much playback delay the network really needs.
        static TiltedPhoques::Vector<int64_t> s_playerAhead;
        if (apActor && apActor->GetExtension() && apActor->GetExtension()->IsRemotePlayer() && s_playerAhead.size() < 8192)
            s_playerAhead.push_back(ahead);
        s_maxAhead = std::max(s_maxAhead, ahead);
        s_minAhead = std::min(s_minAhead, ahead);
        ++s_updates;
        if (ahead < 0)
        {
            ++s_starved;
            if (apActor)
            {
                // Defaults to 0 on first sight, and ahead is negative here, so the first value always sticks.
                int64_t& worstSeen = s_starvedActors[apActor->formID];
                if (ahead < worstSeen)
                    worstSeen = ahead;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= s_nextLog)
        {
            s_nextLog = now + 10s;
            spdlog::info("InterpDiag: {} updates, newest buffered tick {} to {} ms ahead of playback, {} updates had no future point", s_updates, s_minAhead, s_maxAhead, s_starved);
            if (!s_playerAhead.empty())
            {
                std::sort(s_playerAhead.begin(), s_playerAhead.end());
                const auto at = [](const double acShare) { return s_playerAhead[static_cast<size_t>(acShare * static_cast<double>(s_playerAhead.size() - 1))]; };
                const auto starved = std::count_if(s_playerAhead.begin(), s_playerAhead.end(), [](const int64_t aAhead) { return aAhead < 0; });
                spdlog::info("InterpDiag players: {} updates, newest point ahead of playback: lowest {}, 1% {}, 5% {}, median {} ms; {} had none ahead", s_playerAhead.size(),
                             s_playerAhead.front(), at(0.01), at(0.05), at(0.5), starved);
                s_playerAhead.clear();
            }

            // The owner sends every actor every 100 ms, so a stopped stream means the server did not forward it or the owner lost it.
            // The server filters by range against the centre grid this client reported.
            // Distance in cells tells which: a starved actor standing next to us is the server's decision.
            if (!s_starvedActors.empty())
            {
                TiltedPhoques::Vector<std::pair<uint32_t, int64_t>> worst(s_starvedActors.begin(), s_starvedActors.end());
                std::partial_sort(worst.begin(), worst.begin() + std::min<size_t>(4, worst.size()), worst.end(), [](const auto& acLhs, const auto& acRhs) { return acLhs.second < acRhs.second; });

                const PlayerCharacter* pPlayer = PlayerCharacter::Get();
                const TES* pTES = TES::Get();
                std::string line;
                for (size_t i = 0; i < worst.size() && i < 4; ++i)
                {
                    Actor* pStarved = Cast<Actor>(TESForm::GetById(worst[i].first));
                    const TESNPC* pBase = pStarved ? Cast<TESNPC>(pStarved->baseForm) : nullptr;
                    float cells = -1.f;
                    if (pStarved && pPlayer)
                        cells = glm::distance(glm::vec2{pStarved->position.x, pStarved->position.y}, glm::vec2{pPlayer->position.x, pPlayer->position.y}) / 4096.f;
                    line += fmt::format("{:X} ({}) stale {} ms, {:.1f} cells away, has 3D {}; ", worst[i].first, pBase ? pBase->fullName.value.AsAscii() : "?", -worst[i].second, cells,
                                        pStarved && pStarved->GetNiNode() != nullptr);
                }
                spdlog::info("InterpDiag starved: {} actors, worst: {}player at ({:.0f}, {:.0f}), reported centre grid ({}, {}), current grid ({}, {})", s_starvedActors.size(), line,
                             pPlayer ? pPlayer->position.x : 0.f, pPlayer ? pPlayer->position.y : 0.f, pTES ? pTES->centerGridX : 0, pTES ? pTES->centerGridY : 0,
                             pTES ? pTES->currentGridX : 0, pTES ? pTES->currentGridY : 0);
                s_starvedActors.clear();
            }

            s_maxAhead = INT64_MIN;
            s_minAhead = INT64_MAX;
            s_updates = 0;
            s_starved = 0;
        }
    }
#endif

    // Do not subtract unsigned ticks until their order is known.
    // A clock correction or a newly trimmed buffer can put playback before its first point; that holds the first position.
    float delta = 0.f;
    if (aTick >= second.Tick)
        delta = 1.f;
    else if (aTick > first.Tick && second.Tick > first.Tick)
        delta = static_cast<float>(aTick - first.Tick) / static_cast<float>(second.Tick - first.Tick);

    const NiPoint3 position{TiltedPhoques::Lerp(first.Position, second.Position, delta)};

    aInterpolationComponent.Position = position;

    // The pose and actor position must come from the same instant.
    // Different playback ticks put the visible body and hands ahead of the actor/controller that combat collides with.
    auto& vrPose = aInterpolationComponent.InterpolatedVRPose;
    vrPose.HasData = false;
    vrPose.NoBones = false;
    vrPose.HasLegs = false;
    vrPose.HasFingers = false;
    vrPose.HasScale = false;
    vrPose.HasRootPosition = false;
    vrPose.HasWeapons = false;
    {
        const InterpolationComponent::TimePoint* pBefore = nullptr;
        const InterpolationComponent::TimePoint* pAfter = nullptr;
        for (const auto& point : movements)
        {
            if (!point.VRPoseData.HasData)
                continue;
            if (point.Tick <= aTick)
                pBefore = &point;
            else
            {
                pAfter = &point;
                break;
            }
        }
        if (!pBefore)
            pBefore = pAfter;
        if (!pAfter)
            pAfter = pBefore;

        if (pBefore && pAfter)
        {
            float poseDelta = 1.f;
            if (pAfter->Tick > pBefore->Tick && aTick < pAfter->Tick)
            {
                poseDelta = 0.f;
                if (aTick > pBefore->Tick)
                    poseDelta = static_cast<float>(static_cast<double>(aTick - pBefore->Tick) / static_cast<double>(pAfter->Tick - pBefore->Tick));
            }

            vrPose.HasData = true;
            // A body with no readable skeleton (VRPose::NoBones) has nothing to blend, so its bone arrays are left alone.
            // The newest point wins, as for every other field that is a reading rather than a rotation.
            vrPose.NoBones = pAfter->VRPoseData.NoBones || pBefore->VRPoseData.NoBones;
            if (!vrPose.NoBones)
            {
                // Legs only between two points that both carry them (the sender's trackers can come and go).
                vrPose.HasLegs = pBefore->VRPoseData.HasLegs && pAfter->VRPoseData.HasLegs;
                const size_t boneCount = vrPose.HasLegs ? VRPose::kBoneCount : VRPose::kUpperBoneCount;
                for (size_t i = 0; i < boneCount; ++i)
                    vrPose.Bones[i] = glm::slerp(static_cast<glm::quat>(pBefore->VRPoseData.Bones[i]), static_cast<glm::quat>(pAfter->VRPoseData.Bones[i]), poseDelta);
            }
            // Fingers are sent only when they change, so the newest set around the pose tick is passed on.
            // The body sync keeps the last one it received. No blending: a grip is a step.
            const VRPose& fingerSource = pAfter->VRPoseData.HasFingers ? pAfter->VRPoseData : pBefore->VRPoseData;
            if (fingerSource.HasFingers)
            {
                vrPose.HasFingers = true;
                vrPose.Fingers = fingerSource.Fingers;
            }
            // The position of a body being dragged, blended like movement.
            // Blended only when both points carry one, so it never blends against a stale origin.
            if (pBefore->VRPoseData.HasRootPosition && pAfter->VRPoseData.HasRootPosition)
            {
                vrPose.HasRootPosition = true;
                for (size_t i = 0; i < 3; ++i)
                    vrPose.RootPosition[i] = TiltedPhoques::Lerp(pBefore->VRPoseData.RootPosition[i], pAfter->VRPoseData.RootPosition[i], poseDelta);
            }
            else if (pAfter->VRPoseData.HasRootPosition)
            {
                vrPose.HasRootPosition = true;
                std::copy(std::begin(pAfter->VRPoseData.RootPosition), std::end(pAfter->VRPoseData.RootPosition), std::begin(vrPose.RootPosition));
            }

            const VRPose& scaleSource = pAfter->VRPoseData.HasScale ? pAfter->VRPoseData : pBefore->VRPoseData;
            if (scaleSource.HasScale)
            {
                vrPose.HasScale = true;
                vrPose.RootScale = scaleSource.RootScale;
            }

            // Hips, blended like movement, since the whole body is offset by this.
            // Blended only when both points carry one, so it never blends against a stale origin.
            //
            // This layer rebuilds the pose from the buffer and copies only the fields it knows.
            // A new field in VRPose must be added here too, or it is silently dropped on arrival.
            if (pBefore->VRPoseData.HasHips && pAfter->VRPoseData.HasHips)
            {
                vrPose.HasHips = true;
                for (size_t i = 0; i < 3; ++i)
                    vrPose.HipOffset[i] = TiltedPhoques::Lerp(pBefore->VRPoseData.HipOffset[i], pAfter->VRPoseData.HipOffset[i], poseDelta);
            }
            else if (pAfter->VRPoseData.HasHips)
            {
                vrPose.HasHips = true;
                std::copy(std::begin(pAfter->VRPoseData.HipOffset), std::end(pAfter->VRPoseData.HipOffset), std::begin(vrPose.HipOffset));
            }

            // The copy's hands are put where these say (VRBodySync::ReachHands), so they are blended when both ends have them.
            // Otherwise the newest wins; older senders send them only once a second.
            if (pBefore->VRPoseData.HasHandCheck && pAfter->VRPoseData.HasHandCheck)
            {
                vrPose.HasHandCheck = true;
                for (size_t i = 0; i < 3; ++i)
                {
                    vrPose.LeftHandOffset[i] = TiltedPhoques::Lerp(pBefore->VRPoseData.LeftHandOffset[i], pAfter->VRPoseData.LeftHandOffset[i], poseDelta);
                    vrPose.RightHandOffset[i] = TiltedPhoques::Lerp(pBefore->VRPoseData.RightHandOffset[i], pAfter->VRPoseData.RightHandOffset[i], poseDelta);
                }
            }
            else
            {
                const VRPose& handSource = pAfter->VRPoseData.HasHandCheck ? pAfter->VRPoseData : pBefore->VRPoseData;
                if (handSource.HasHandCheck)
                {
                    vrPose.HasHandCheck = true;
                    std::copy(std::begin(handSource.LeftHandOffset), std::end(handSource.LeftHandOffset), std::begin(vrPose.LeftHandOffset));
                    std::copy(std::begin(handSource.RightHandOffset), std::end(handSource.RightHandOffset), std::begin(vrPose.RightHandOffset));
                }
            }

            // Where the weapons are held, sent when the grip changes.
            // Like the fingers, the newest set around the pose tick is passed on. No blending: a grip is a step.
            const VRPose& weaponSource = pAfter->VRPoseData.HasWeapons ? pAfter->VRPoseData : pBefore->VRPoseData;
            if (weaponSource.HasWeapons)
            {
                vrPose.HasWeapons = true;
                vrPose.WeaponHeld = weaponSource.WeaponHeld;
                vrPose.WeaponRotation = weaponSource.WeaponRotation;
                std::copy(&weaponSource.WeaponOffset[0][0], &weaponSource.WeaponOffset[0][0] + 6, &vrPose.WeaponOffset[0][0]);
            }
        }
    }

#ifdef SKYRIMVR
    VRBodySync::SetRemotePose(apActor, vrPose);
#endif

    if (!apActor)
        return;

    // A dying body falls with its own ragdoll, and a downed one has to be able to get back up where it fell.
    // Forcing either to follow its owner's position pulls the body away from the animation playing on it.
    if (apActor->actorState.IsDying() || apActor->actorState.IsBleedingOut())
        return;

    if (apActor->actorState.IsDead())
    {
        // A dead body is moved to where the owner's corpse lies, but only when it is clearly elsewhere.
        // ForcePosition moves only the reference; the ragdoll the corpse is drawn from is moved by PlaceCorpse (live-corpse-place).
        constexpr float cCorpseSnapDistance = 64.f;
#ifdef SKYRIMVR
        // What is drawn, the ragdoll, goes where the owner has the body (VRBodySync::PlaceCorpse).
        VRBodySync::PlaceCorpse(apActor, position);
#endif
        const glm::vec3 current{apActor->position.x, apActor->position.y, apActor->position.z};
        const float cApart = glm::distance(current, position);
        if (cApart > cCorpseSnapDistance)
            apActor->ForcePosition(position);
        return;
    }

#ifdef SKYRIMVR
    // SlideDiag: a body that moves while the animation variables driving its legs do not change is sliding.
    // Both numbers are already here, so the check costs almost nothing.
    {
        static uint32_t s_moved = 0;
        static uint32_t s_movedFrozen = 0;
        static uint32_t s_worstActor = 0;
        static std::chrono::steady_clock::time_point s_nextLog = std::chrono::steady_clock::now() + 10s;

        if (glm::distance(static_cast<glm::vec3>(apActor->position), position) > 2.f)
        {
            ++s_moved;
            // Counted only when the variables are unchanged and all zero.
            // A body running at a steady speed sends the same variables in every snapshot while its legs still move.
            if (first.Variables == second.Variables)
            {
                bool saysSomething = false;
                for (const float value : second.Variables.Floats)
                {
                    if (value != 0.f)
                    {
                        saysSomething = true;
                        break;
                    }
                }
                if (!saysSomething)
                {
                    ++s_movedFrozen;
                    s_worstActor = apActor->formID;
                }
            }
        }

        const auto slideNow = std::chrono::steady_clock::now();
        if (slideNow >= s_nextLog)
        {
            s_nextLog = slideNow + 10s;
            if (s_moved)
                spdlog::info("SlideDiag: {} of {} moves of a remote body came with animation variables that had not changed and said nothing (no speed, no direction); last was {:X}. Those are a body sliding rather than walking.",
                             s_movedFrozen, s_moved, s_worstActor);
            s_moved = 0;
            s_movedFrozen = 0;
            s_worstActor = 0;
        }
    }
#endif

    apActor->ForcePosition(position);
    SpawnReveal::Placed(apActor);
    // A creature of another kind than the owner's (see MarkForeignGraph) keeps its own animation state.
    if (!aInterpolationComponent.ForeignGraph)
        apActor->LoadAnimationVariables(second.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        apActor->currentProcess->middleProcess->direction = second.Direction;
    }

    auto rotA = first.Rotation;
    auto rotB = second.Rotation;

    const auto deltaX = TiltedPhoques::DeltaAngle(rotA.x, rotB.x, true) * delta;
    const auto deltaY = TiltedPhoques::DeltaAngle(rotA.y, rotB.y, true) * delta;
    const auto deltaZ = TiltedPhoques::DeltaAngle(rotA.z, rotB.z, true) * delta;

    auto finalX = TiltedPhoques::Mod(rotA.x + deltaX, float(TiltedPhoques::Pi * 2));
    if (finalX > 0.f && finalX > float(TiltedPhoques::Pi / 2))
        finalX -= TiltedPhoques::Pi * 2;

    const auto finalY = TiltedPhoques::Mod(rotA.y + deltaY, float(TiltedPhoques::Pi * 2));
    const auto finalZ = TiltedPhoques::Mod(rotA.z + deltaZ, float(TiltedPhoques::Pi * 2));

    apActor->SetRotation(finalX, finalY, finalZ);
}

void InterpolationSystem::AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint) noexcept
{
    // Interpolating between two points too far apart for any actor to travel would drag (or launch) the actor across.
    // The buffer keeps no worldspace, so points either side of a worldspace change can be hundreds of cells apart.
    // Points are about 0.1 s apart, so a step over a cell (4096 units) drops the buffer and the actor arrives at the new place.
    if (!aInterpolationComponent.TimePoints.empty())
    {
        constexpr float cImpossibleStep = 4096.f;
        const glm::vec3& previous = aInterpolationComponent.TimePoints.back().Position;
        const float jump = glm::distance(previous, static_cast<glm::vec3>(acPoint.Position));
        if (jump > cImpossibleStep)
        {
            aInterpolationComponent.TimePoints.clear();
            aInterpolationComponent.Position = acPoint.Position;

            // Logged because the worldspace-change cause is a hypothesis; the log shows how often, how far and from where.
            static std::chrono::steady_clock::time_point s_nextLog;
            static uint32_t s_since = 0;
            ++s_since;
            const auto now = std::chrono::steady_clock::now();
            if (now >= s_nextLog)
            {
                s_nextLog = now + std::chrono::seconds(10);
                spdlog::info("JumpDiag: a buffered point was {:.0f} units from the one before it, at ({:.0f}, {:.0f}, {:.0f}); dropped the buffer so the actor arrives instead of flying. {} since the last line.",
                             jump, acPoint.Position.x, acPoint.Position.y, acPoint.Position.z, s_since);
                s_since = 0;
            }
        }
    }

    auto itor = std::begin(aInterpolationComponent.TimePoints);
    const auto end = std::cend(aInterpolationComponent.TimePoints);

    bool inserted = false;
    while (itor != end)
    {
        if (itor->Tick > acPoint.Tick)
        {
            aInterpolationComponent.TimePoints.insert(itor, acPoint);
            inserted = true;
            break;
        }

        ++itor;
    }

    if (!inserted)
        aInterpolationComponent.TimePoints.push_back(acPoint);

    // Normally Update consumes old points each frame.
    // If the playback clock stalls or the sender's clock runs ahead, the buffer would grow without bound, so keep the newest few seconds.
    // Update can hold the oldest retained point until the clock catches up.
    constexpr size_t cMaxBufferedPoints = 64;
    while (aInterpolationComponent.TimePoints.size() > cMaxBufferedPoints)
        aInterpolationComponent.TimePoints.pop_front();
}

InterpolationComponent& InterpolationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    return aWorld.emplace_or_replace<InterpolationComponent>(aEntity);
}

void InterpolationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<InterpolationComponent>(aEntity))
        aWorld.remove<InterpolationComponent>(aEntity);
}
