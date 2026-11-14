#include <TiltedOnlinePCH.h>

#include <Takeover.h>

#include <World.h>
#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>
#include <Systems/InterpolationSystem.h>

namespace
{
// NPCs gained while their owner's reports had them moving.
// This game's AI shows what it wants a moment later: a speed if it walks them on, none if it has them standing.
// Standing, it never had them moving, so it never stops the legs, and they would hold a stride until its next move.
struct GainedMoving
{
    uint32_t FormId{};
    float CopySpeed{};
    std::chrono::steady_clock::time_point At{};
};
TiltedPhoques::Vector<GainedMoving> s_gainedMoving;

void RunGainedMoving() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    for (auto it = s_gainedMoving.begin(); it != s_gainedMoving.end();)
    {
        if (cNow < it->At)
        {
            ++it;
            continue;
        }

        Actor* pActor = Cast<Actor>(TESForm::GetById(it->FormId));
        // Given away again by now, or killed, thrown or mounted: its legs are not this stop's to touch.
        const bool cStillOurs = pActor && pActor->GetNiNode() && !pActor->GetExtension()->IsRemote();
        if (cStillOurs && !pActor->IsMount() && InterpolationSystem::OnFoot(pActor) && !pActor->actorState.IsDeadOrDying() && !pActor->actorState.IsBleedingOut() &&
            !pActor->actorState.IsKnockedDown())
        {
            const float cSpeed = InterpolationSystem::FootSpeed(pActor);
            if (cSpeed > 1.f)
                spdlog::info("Takeover: NPC {:X} was moving as a copy here (speed {:.0f}), and this game's AI walks it on (speed {:.0f})", pActor->formID, it->CopySpeed, cSpeed);
            else
            {
                static BSFixedString s_moveStop("moveStop");
                const bool cTaken = pActor->SendAnimationEvent(&s_moveStop);
                spdlog::info("Takeover: NPC {:X} was moving as a copy here (speed {:.0f}), and this game's AI has it standing; stopped ('moveStop' {})", pActor->formID,
                             it->CopySpeed, cTaken ? "taken" : "not taken");
            }
        }
        it = s_gainedMoving.erase(it);
    }
}

} // namespace

namespace Takeover
{
// By its owner's reports, not its graph: this game's AI writes its own speed there, even on a copy.
// A slow walker reports about 60 units a second, and a standing body none.
void NoteGainedMoving(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept
{
    constexpr float cMovingSpeed = 20.f;
    const bool cOwnGraphCopy = apActor->GetExtension()->IsRemote() && apInterpolation && !apInterpolation->ForeignGraph;
    if (const float cCopySpeed = cOwnGraphCopy ? InterpolationSystem::ReportedSpeed(apActor->formID, *apInterpolation) : 0.f; cCopySpeed > cMovingSpeed)
        s_gainedMoving.push_back({apActor->formID, cCopySpeed, std::chrono::steady_clock::now() + std::chrono::milliseconds(500)});
}

} // namespace Takeover

namespace
{
// TEMPORARY: what a living NPC carries over from being a copy, read as it is gained and 3 s later.
// In flags1, bits 0-3 are its moving directions and bits 6-8 walking, running and sprinting.
// Its weapon is logged as the number of ActorState::WeaponState.
struct TakeoverWatch
{
    uint32_t FormId{};
    std::chrono::steady_clock::time_point At{};
};
TiltedPhoques::Vector<TakeoverWatch> s_takeoverWatches;

void LogTakeoverState(Actor* apActor, const char* acpWhen) noexcept
{
    static BSFixedString s_speed("Speed");
    static BSFixedString s_state("iState");
    float graphSpeed = 0.f;
    uint32_t graphState = 0;
    apActor->animationGraphHolder.GetVariableFloat(&s_speed, &graphSpeed);
    apActor->animationGraphHolder.GetVariableInt(&s_state, &graphState);
    spdlog::info("TakeoverDiag: NPC {:X} {}: flags1 {:#x}, flags2 {:#x}, weapon {}, speed {:.0f}, graph speed {:.0f}, graph state {}", apActor->formID, acpWhen,
                 apActor->actorState.flags1, apActor->actorState.flags2, static_cast<uint32_t>(apActor->actorState.GetWeaponState()), apActor->GetSpeed(), graphSpeed, graphState);
}

// TEMPORARY: how far from its last owner's newest report a character is gained.
// The other games play it again from where this game has it, so this is about the jump they see.
void LogGainedPlace(const Actor* apActor, const InterpolationComponent* apInterpolation) noexcept
{
    if (!apInterpolation || apInterpolation->TimePoints.empty())
    {
        spdlog::info("GainDiag: NPC {:X} gained with no report from its last owner held here", apActor->formID);
        return;
    }
    const glm::vec3 cNewest = apInterpolation->TimePoints.back().Position;
    spdlog::info("GainDiag: NPC {:X} gained {:.0f} units from its last owner's newest report{}", apActor->formID, glm::distance(glm::vec3(apActor->position), cNewest),
                 apInterpolation->IsExtrapolating ? ", while extrapolating" : "");
}

// TEMPORARY: the second reading of each gained NPC, and a line each time an NPC this game runs starts or stops swimming.
void RunTakeoverDiag(World& aWorld) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    for (auto it = s_takeoverWatches.begin(); it != s_takeoverWatches.end();)
    {
        if (cNow < it->At)
        {
            ++it;
            continue;
        }
        if (Actor* pActor = Cast<Actor>(TESForm::GetById(it->FormId)))
            LogTakeoverState(pActor, "3 s after the gain");
        it = s_takeoverWatches.erase(it);
    }

    static std::chrono::steady_clock::time_point s_nextSwimCheck;
    if (cNow < s_nextSwimCheck)
        return;
    s_nextSwimCheck = cNow + std::chrono::seconds(1);

    static TiltedPhoques::Map<uint32_t, bool> s_swimming;
    auto view = aWorld.view<FormIdComponent, LocalComponent>();
    for (const auto cEntity : view)
    {
        const Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(cEntity).Id));
        if (!pActor || pActor == PlayerCharacter::Get())
            continue;
        const bool cSwimming = pActor->actorState.IsSwimming();
        const auto it = s_swimming.find(pActor->formID);
        if (cSwimming != (it != s_swimming.end() && it->second))
            spdlog::info("SwimDiag: own NPC {:X} {} swimming here at height {:.0f}", pActor->formID, cSwimming ? "starts" : "stops", pActor->position.z);
        s_swimming[pActor->formID] = cSwimming;
    }
}

} // namespace

namespace Takeover
{
void LogGain(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept
{
    if (apActor->IsDead())
        return;
    LogTakeoverState(apActor, "as it is gained");
    LogGainedPlace(apActor, apInterpolation);
    s_takeoverWatches.push_back({apActor->formID, std::chrono::steady_clock::now() + std::chrono::seconds(3)});
}

void Run(World& aWorld) noexcept
{
    RunGainedMoving();
    RunTakeoverDiag(aWorld);
}
} // namespace Takeover
