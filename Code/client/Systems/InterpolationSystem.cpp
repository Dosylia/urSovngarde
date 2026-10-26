#include <TiltedOnlinePCH.h>

#include <unordered_set>

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

namespace
{
// Past the newest point the actor keeps moving for at most this long, then holds still until data arrives.
constexpr double cMaxExtrapolation = 150.0;
// Larger corrections snap instead of sliding the actor into place.
constexpr float cMaxPositionCorrection = 256.f;
// Time constant of a correction fading out: about 95% of it is gone after three times this.
constexpr double cPositionCorrectionTime = 50.0;
// Two points further apart than this are a teleport, not movement to cross.
constexpr float cTeleportDistance = 1024.f;

// A copy that got up away from where its owner has it closes the gap at most this many units a second faster than its owner moves.
constexpr float cGetUpCatchUpSpeed = 300.f;
// A gap wider than this is closed at once: walking it would take too long.
constexpr float cGetUpCatchUpDistance = 512.f;

// An NPC copy that just got up, while it closes the gap to where its owner has it and the speed of its steps is measured.
struct Rising
{
    std::chrono::steady_clock::time_point Since;
    float Fastest;
    // Where it stands against where its owner has it; shrinks to nothing.
    glm::vec3 Offset;
};
TiltedPhoques::Map<uint32_t, Rising> s_rising;

// NPC copies lying in their own ragdoll: one line when one goes down, one when it is up again, and one if it still lies after 15 s.
struct Down
{
    std::chrono::steady_clock::time_point Since;
    std::chrono::steady_clock::time_point QueuedSince;
    uint32_t GetUpsSent;
    bool SaidLong;
};
TiltedPhoques::Map<uint32_t, Down> s_down;

// Copies dead here while their owner's game has not said they died, so that it is logged once per copy.
std::unordered_set<uint32_t> s_saidDeadAlone;

// A copy whose owner went quiet (a crash, a frozen game, a lost connection) would play the newest report's walk on the spot.
// After this long with no report, a copy that walks is stopped instead.
constexpr double cQuietStopAfter = 1500.0;
// Copies stopped that way, by form id: when, and when reports came back after (zero while none have).
// Kept here, not in the component: it is made again when the actor changes hands, and the stop still has to be undone after.
struct QuietStop
{
    std::chrono::steady_clock::time_point Since;
    std::chrono::steady_clock::time_point Back;
};
TiltedPhoques::Map<uint32_t, QuietStop> s_quietStopped;

// Not sitting, riding, sleeping or flying: the sit and sleep state (bits 14-17) and the fly state (bits 18-20) are clear.
// Only then do the graph's own move events stop and start the legs.
bool IsOnFoot(const Actor* apActor) noexcept
{
    const uint32_t cFlags = apActor->actorState.flags1;
    return ((cFlags >> 14) & 0xF) == 0 && ((cFlags >> 18) & 0x7) == 0;
}

// How fast the copy's legs go: the graph's own Speed, or the actor's when that is higher.
// Some animation frameworks (MGO's) drive the legs from the graph while the actor's speed reads zero, and the walk went on.
float LegSpeed(Actor* apActor) noexcept
{
    static BSFixedString s_speed("Speed");
    float graphSpeed = 0.f;
    apActor->animationGraphHolder.GetVariableFloat(&s_speed, &graphSpeed);
    return std::max(graphSpeed, apActor->GetSpeed());
}

// Copies that went quiet without being stopped, so that why is logged once per quiet spell.
TiltedPhoques::Set<uint32_t> s_quietNotStopped;

// The graph's own stop, then no speed for the legs, as when the owner's character stands still.
void StopQuietCopy(Actor* apActor, const double aQuietFor) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (const auto stopped = s_quietStopped.find(apActor->formID); stopped != s_quietStopped.end())
        stopped.value().Back = {};
    else
    {
        // Copies removed while stopped leave their entry behind.
        for (auto it = s_quietStopped.begin(); it != s_quietStopped.end();)
            it = cNow - it->second.Since > 10min ? s_quietStopped.erase(it) : std::next(it);

        static BSFixedString s_moveStop("moveStop");
        const float cSpeed = LegSpeed(apActor);
        const bool cTaken = apActor->SendAnimationEvent(&s_moveStop);
        s_quietStopped[apActor->formID] = QuietStop{cNow, {}};
        spdlog::info("QuietStop: copy {:X} had no report for {:.1f} s while moving (speed {:.0f}); stopped ('moveStop' {})", apActor->formID, aQuietFor / 1000.0, cSpeed,
                     cTaken ? "taken" : "not taken");
    }

    static BSFixedString s_speed("Speed");
    apActor->animationGraphHolder.SetVariableFloat(&s_speed, 0.f);
    apActor->SetSpeed(0.f);
}

// Reports came back, and their variables are on the copy.
// It is started again once they say it moves, or it would slide in its standing state: its owner's own start is long gone.
// The first reports back can say it stands, then speed up (live-copy-quiet), so it is watched until it moves.
// One that stands through 2 s of reports is left to them.
void WatchQuietCopy(Actor* apActor) noexcept
{
    const auto stopped = s_quietStopped.find(apActor->formID);
    if (stopped == s_quietStopped.end())
        return;

    const auto cNow = std::chrono::steady_clock::now();
    if (stopped->second.Back == std::chrono::steady_clock::time_point{})
        stopped.value().Back = cNow;
    const auto cBackFor = std::chrono::duration_cast<std::chrono::milliseconds>(cNow - stopped->second.Back).count();

    const float cSpeed = LegSpeed(apActor);
    if (cSpeed > 1.f)
    {
        static BSFixedString s_moveStart("moveStart");
        const bool cTaken = apActor->SendAnimationEvent(&s_moveStart);
        spdlog::info("QuietStop: copy {:X} moves again, {} ms after its reports came back (speed {:.0f}); started ('moveStart' {})", apActor->formID, cBackFor, cSpeed,
                     cTaken ? "taken" : "not taken");
        s_quietStopped.erase(stopped);
    }
    else if (cNow - stopped->second.Back > 2s)
    {
        spdlog::info("QuietStop: copy {:X} stood through {} ms of reports since they came back; left to them", apActor->formID, cBackFor);
        s_quietStopped.erase(stopped);
    }
}
} // namespace

uint64_t InterpolationSystem::GetPlaybackTick(const InterpolationComponent& acInterpolationComponent) noexcept
{
    const double cTick = acInterpolationComponent.Clock.GetRenderTick();
    return acInterpolationComponent.Clock.IsRunning() && cTick > 0.0 ? static_cast<uint64_t>(cTick) : 0;
}

void InterpolationSystem::Update(Actor* apActor, InterpolationComponent& aInterpolationComponent, const double aNow, const bool aDeadAtOwner, const uint64_t aLastActionTick) noexcept
{
    const double cDeltaTime = aInterpolationComponent.Clock.Advance(aNow);

    auto& movements = aInterpolationComponent.TimePoints;

    if (movements.size() < 2)
        return;

    const double cRenderTick = aInterpolationComponent.Clock.GetRenderTick();
    const uint64_t aTick = GetPlaybackTick(aInterpolationComponent);

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

        // Each remote player's playback clock: the delay it settled on and what it is made of.
        static TiltedPhoques::Map<uint32_t, std::chrono::steady_clock::time_point> s_nextClockLog;
        const auto cClockNow = std::chrono::steady_clock::now();
        if (apActor && apActor->GetExtension() && apActor->GetExtension()->IsRemotePlayer() && aInterpolationComponent.Clock.IsRunning())
        {
            auto& next = s_nextClockLog[apActor->formID];
            if (cClockNow >= next)
            {
                next = cClockNow + 10s;
                const auto& clock = aInterpolationComponent.Clock;
                spdlog::info("PlaybackDiag: player copy {:X} shown {:.0f} ms behind its newest possible update (snapshot interval {:.0f} ms, jitter {:.0f} ms), "
                             "playback rate {:.2f}, extrapolating {}",
                             apActor->formID, clock.GetTargetDelay(), clock.GetSnapshotInterval(), clock.GetJitter(), clock.GetPlaybackRate(),
                             aInterpolationComponent.IsExtrapolating);
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

    // Before the first point it holds the first position; past the newest one it keeps going the way the actor moved.
    const double cTickDelta = second.Tick > first.Tick ? static_cast<double>(second.Tick - first.Tick) : 0.0;
    const bool cIsTeleport = glm::distance(first.Position, second.Position) > cTeleportDistance;
    float delta = 1.f;
    bool isExtrapolating = false;
    glm::vec3 target{};
    if (cRenderTick <= static_cast<double>(second.Tick))
    {
        if (cTickDelta > 0.0)
            delta = static_cast<float>(std::clamp((cRenderTick - static_cast<double>(first.Tick)) / cTickDelta, 0.0, 1.0));
        // A teleport happens at once, when its point is reached.
        if (cIsTeleport && delta < 1.f)
            delta = 0.f;
        target = TiltedPhoques::Lerp(first.Position, second.Position, delta);
    }
    else
    {
        target = second.Position;
        if (!cIsTeleport && cTickDelta > 0.0)
        {
            const double cExtrapolationTime = std::min(cRenderTick - static_cast<double>(second.Tick), cMaxExtrapolation);
            target = TiltedPhoques::Lerp(first.Position, second.Position, static_cast<float>(1.0 + cExtrapolationTime / cTickDelta));
            isExtrapolating = true;
        }
    }

    // The late point arrived, so the actor is no longer where extrapolation put it: the difference fades out.
    auto& positionError = aInterpolationComponent.PositionError;
    if (aInterpolationComponent.HasNewTimePoint && aInterpolationComponent.IsExtrapolating)
        positionError = aInterpolationComponent.Position - target;
    if (cIsTeleport || glm::length(positionError) > cMaxPositionCorrection)
        positionError = {};
    else
        positionError *= static_cast<float>(std::exp(-cDeltaTime / cPositionCorrectionTime));
    aInterpolationComponent.HasNewTimePoint = false;
    aInterpolationComponent.IsExtrapolating = isExtrapolating;

    aInterpolationComponent.Position = target + positionError;
    const NiPoint3 position{aInterpolationComponent.Position};

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

    // A copy swims as its own animation has it, not as its owner says: one line each time that starts or stops here.
    {
        static TiltedPhoques::Map<uint32_t, bool> s_swimming;
        const bool cSwimming = apActor->actorState.IsSwimming();
        if (auto it = s_swimming.find(apActor->formID); it == s_swimming.end())
            s_swimming[apActor->formID] = cSwimming;
        else if (it->second != cSwimming)
        {
            it.value() = cSwimming;
            spdlog::info("SwimDiag: copy {:X} {} swimming here at height {:.0f}; its owner has it at height {:.0f}", apActor->formID, cSwimming ? "starts" : "stops",
                         apActor->position.z, position.z);
        }
    }

    // A dying body falls with its own ragdoll, and a downed one has to be able to get back up where it fell.
    // Forcing either to follow its owner's position pulls the body away from the animation playing on it.
    if (apActor->actorState.IsDying() || apActor->actorState.IsBleedingOut())
        return;

    // An NPC copy thrown to the floor lies in its own ragdoll until it gets up here (its knock state is its own).
    // Moved meanwhile, the body would glide along the floor behind its owner (live-copy-knockdown).
    const ActorExtension* pExtension = apActor->GetExtension();
    const bool cKnockedNpc = !apActor->actorState.IsDead() && apActor->actorState.IsKnockedDown() && pExtension && !pExtension->IsRemotePlayer();
    {
        // KNOCK_STATE_ENUM 5: the ragdoll has come to rest and the get-up is queued.
        constexpr uint32_t cKnockQueued = 5;
        const uint32_t cKnockState = (apActor->actorState.flags1 >> 25) & 0x7;
        const auto cNow = std::chrono::steady_clock::now();
        const auto it = s_down.find(apActor->formID);
        if (cKnockedNpc && it == s_down.end())
        {
            s_down[apActor->formID] = Down{cNow, {}, 0, false};
            spdlog::info("KnockDown: NPC copy {:X} lies in its own ragdoll (knock state {}); it stays where it fell until it gets up", apActor->formID, cKnockState);
        }
        else if (cKnockedNpc)
        {
            // Here the owner is no longer bleeding out (that returned above), so the copy should get up too.
            // A copy that fell into its own bleedout has the owner's life state written over it, and its queued get-up then never starts.
            // The graph's own get-up event starts it (live-copy-bleedout).
            // A get-up can fall back into the queue, as when the owner's bleedOutStop comes while it plays.
            // So it is sent again each time it waits queued that long once more, a few times at most.
            constexpr uint32_t cMaxGetUps = 3;
            Down& down = it.value();
            if (cKnockState != cKnockQueued)
                down.QueuedSince = {};
            else if (down.QueuedSince == std::chrono::steady_clock::time_point{})
                down.QueuedSince = cNow;
            else if (down.GetUpsSent < cMaxGetUps && cNow - down.QueuedSince > 1500ms)
            {
                ++down.GetUpsSent;
                BSFixedString event("GetUpBegin");
                const bool cTaken = apActor->SendAnimationEvent(&event);
                spdlog::info("KnockDown: NPC copy {:X} waited {} ms with its get-up queued; sent it 'GetUpBegin' ({}, try {})", apActor->formID,
                             std::chrono::duration_cast<std::chrono::milliseconds>(cNow - down.QueuedSince).count(), cTaken ? "taken" : "not taken", down.GetUpsSent);
                down.QueuedSince = {};
            }
            if (!down.SaidLong && cNow - down.Since > 15s)
            {
                down.SaidLong = true;
                spdlog::warn("KnockDown: NPC copy {:X} still lies after 15 s (knock state {})", apActor->formID, cKnockState);
            }
        }
        else if (!cKnockedNpc && it != s_down.end())
        {
            // How far it stood up from where its owner has it: the copy jumps that far when it follows again.
            spdlog::info("KnockDown: NPC copy {:X} is up again after {} ms, {:.0f} units from where its owner has it", apActor->formID,
                         std::chrono::duration_cast<std::chrono::milliseconds>(cNow - it->second.Since).count(),
                         glm::distance(static_cast<glm::vec3>(apActor->position), static_cast<glm::vec3>(position)));
            s_down.erase(it);
            const glm::vec3 cGap = static_cast<glm::vec3>(apActor->position) - static_cast<glm::vec3>(position);
            s_rising[apActor->formID] = Rising{cNow, 0.f, glm::length(cGap) <= cGetUpCatchUpDistance ? cGap : glm::vec3{}};
        }
    }
    if (cKnockedNpc)
        return;

    if (apActor->actorState.IsDead())
    {
        // A copy that died here alone lies where it fell.
        // The owner's position is then a living actor's, and the body would be dragged after it (live-corpse-owner-alive).
        if (!aDeadAtOwner)
        {
            if (s_saidDeadAlone.insert(apActor->formID).second)
                spdlog::info("CorpseDiag: {:X} is dead here, but its owner's game has not said it died; left where it lies", apActor->formID);
            return;
        }
        s_saidDeadAlone.erase(apActor->formID);

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

        // A move is counted by speed, over 50 units a second, so it does not depend on the frame rate.
        const float cStep = glm::distance(static_cast<glm::vec3>(apActor->position), static_cast<glm::vec3>(position));
        if (cDeltaTime > 0.0 && cStep / static_cast<float>(cDeltaTime) > 0.05f)
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

    // A copy that just got up walks on to where its owner has it instead of jumping there.
    // The fastest it moves meanwhile is logged: a jump is far faster than a walk.
    NiPoint3 placed = position;
    if (const auto rising = s_rising.find(apActor->formID); rising != s_rising.end())
    {
        glm::vec3& offset = rising.value().Offset;
        const float cLeft = glm::length(offset);
        const float cClosed = cGetUpCatchUpSpeed * static_cast<float>(cDeltaTime) / 1000.f;
        offset = cLeft > cClosed ? offset * ((cLeft - cClosed) / cLeft) : glm::vec3{};
        placed = NiPoint3(static_cast<glm::vec3>(position) + offset);

        const float cStep = glm::distance(static_cast<glm::vec3>(apActor->position), static_cast<glm::vec3>(placed));
        if (cDeltaTime > 0.0)
            rising.value().Fastest = std::max(rising->second.Fastest, cStep * 1000.f / static_cast<float>(cDeltaTime));
        const auto cUpFor = std::chrono::steady_clock::now() - rising->second.Since;
        if ((cUpFor > 2s && offset == glm::vec3{}) || cUpFor > 5s)
        {
            spdlog::info("KnockDown: NPC copy {:X} moved at most {:.0f} units a second in its first {} ms up", apActor->formID, rising->second.Fastest,
                         std::chrono::duration_cast<std::chrono::milliseconds>(cUpFor).count());
            s_rising.erase(rising);
        }
    }

    apActor->ForcePosition(placed);
    SpawnReveal::Placed(apActor);
    // A creature of another kind than the owner's (see MarkForeignGraph) keeps its own animation state.
    if (!aInterpolationComponent.ForeignGraph)
    {
        const double cQuietFor = cRenderTick - static_cast<double>(movements.back().Tick);
        const bool cStopped = s_quietStopped.find(apActor->formID) != s_quietStopped.end();
        // Read only once quiet: it asks the graph for a variable.
        const float cLegSpeed = cQuietFor > cQuietStopAfter ? LegSpeed(apActor) : 0.f;
        if (cQuietFor > cQuietStopAfter && IsOnFoot(apActor) && (cStopped || cLegSpeed > 1.f))
            StopQuietCopy(apActor, cQuietFor);
        else
        {
            // TEMPORARY: a copy still moving by some measure but not stopped, and why, once per quiet spell.
            if (cQuietFor <= cQuietStopAfter)
                s_quietNotStopped.erase(apActor->formID);
            else if (!cStopped && cLegSpeed > 1.f && s_quietNotStopped.insert(apActor->formID).second)
            {
                static BSFixedString s_speed("Speed");
                float graphSpeed = 0.f;
                apActor->animationGraphHolder.GetVariableFloat(&s_speed, &graphSpeed);
                spdlog::info("QuietStop: copy {:X} had no report for {:.1f} s and moves, but was not stopped (on foot {}, speed {:.0f}, graph speed {:.0f}, flags1 {:#x})",
                             apActor->formID, cQuietFor / 1000.0, IsOnFoot(apActor), apActor->GetSpeed(), graphSpeed, apActor->actorState.flags1);
            }

            // Variables from before the last action played would undo what it set, until a newer point comes.
            // Movement can be lost, so the point after an action may never come.
            if (second.Tick >= aLastActionTick)
                apActor->LoadAnimationVariables(second.Variables);
            if (cStopped && cQuietFor <= cQuietStopAfter)
                WatchQuietCopy(apActor);
        }
    }

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

void InterpolationSystem::AddPoint(InterpolationComponent& aInterpolationComponent, const InterpolationComponent::TimePoint& acPoint, const double aArrivalTime) noexcept
{
    // A duplicate carries nothing new.
    if (!aInterpolationComponent.Clock.AddSample(aArrivalTime, acPoint.Tick))
        return;

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
    aInterpolationComponent.HasNewTimePoint = true;

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

void InterpolationSystem::Forget(const uint32_t aFormId) noexcept
{
    if (s_quietStopped.erase(aFormId))
        spdlog::info("QuietStop: copy {:X} forgotten while stopped (removed, or run by this game now)", aFormId);
    s_quietNotStopped.erase(aFormId);
    // A new copy under the same id would otherwise get up, slide or stay put on the old one's account.
    s_down.erase(aFormId);
    s_rising.erase(aFormId);
    s_saidDeadAlone.erase(aFormId);
#ifdef SKYRIMVR
    VRBodySync::ForgetCorpse(aFormId);
#endif
}
