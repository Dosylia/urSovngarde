#include <TiltedOnlinePCH.h>

#include <Copies/Takeover.h>

#include <World.h>
#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>
#include <Systems/InterpolationSystem.h>

#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

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

// The furniture each copy here last used, by the copy's form id, and the NPCs gained while their copy sat in it by its graph alone.
// Its graph then holds the furniture's idle while the game has no sit state, so every action of its AI is refused and it stays where it is.
// A moment after the gain, once this game runs it, its graph is sent the furniture's exit, and its AI acts again.
constexpr size_t kMaxCopyFurniture = 1024;
constexpr auto kFurnitureResetAfter = std::chrono::milliseconds(250);
std::mutex s_furnitureLock;
TiltedPhoques::Map<uint32_t, uint32_t> s_copyFurniture;
struct GainedInFurniture
{
    uint32_t FormId{};
    std::chrono::steady_clock::time_point At{};
};
TiltedPhoques::Vector<GainedInFurniture> s_gainedInFurniture;

/**
 * Read whether an NPC's graph is driven by its animation, as in a furniture's idle.
 *
 * @param apActor The NPC.
 * @return The graph's bAnimationDriven; nothing when its graph has no such variable.
 */
std::optional<bool> GraphAnimationDriven(Actor* apActor) noexcept
{
    static BSFixedString s_animationDriven("bAnimationDriven");
    bool driven = false;
    if (!apActor->animationGraphHolder.GetVariableBool(&s_animationDriven, &driven))
        return std::nullopt;
    return driven;
}

/**
 * Check whether an NPC sits in its furniture by its graph alone: its graph is animation-driven while the game has no sit state.
 *
 * @param apActor The NPC.
 * @return True when its graph holds a furniture's idle the game does not know of.
 */
bool SitsByGraphAlone(Actor* apActor) noexcept
{
    return apActor->actorState.SitSleepState() == 0 && GraphAnimationDriven(apActor).value_or(false);
}

// The events that take a graph out of a furniture's idle, from Skyrim.esm's exit idles: furniture in general, a frostbite spider's three ceiling holes, a draugr's coffin, an ice wraith's or a wisp's ambush, a spriggan's tree.
// A graph takes only the events of its own behaviour, so they are tried in turn until one is taken; the reset to standing, which only human graphs have, comes last.
constexpr std::array<const char*, 8> kFurnitureExitEvents{"IdleFurnitureExit", "idleSleepHoleExitStart", "idleSleepHoleExitMediumStart", "idleSleepHoleExitSmallStart",
                                                         "IdleSleepSarcExitStart", "AmbushIdleStop", "idleLeanExitStart", "IdleForceDefaultState"};

/**
 * Send an NPC's graph the events that leave a furniture's idle, until one is taken.
 *
 * @param apActor The NPC.
 * @return The event its graph took; null when it took none.
 */
const char* LeaveFurnitureIdle(Actor* apActor) noexcept
{
    for (const char* cpEvent : kFurnitureExitEvents)
    {
        BSFixedString event(cpEvent);
        if (apActor->SendAnimationEvent(&event))
            return cpEvent;
    }
    return nullptr;
}

/**
 * Take each NPC gained in its furniture out of the furniture's idle once it is due, if this game still runs it and it still sits by its graph alone.
 */
void RunGainedInFurniture() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    for (auto it = s_gainedInFurniture.begin(); it != s_gainedInFurniture.end();)
    {
        if (cNow < it->At)
        {
            ++it;
            continue;
        }

        Actor* pActor = Cast<Actor>(TESForm::GetById(it->FormId));
        // Given away again by now, or killed: its graph is not this reset's to touch.
        const bool cStillOurs = pActor && pActor->GetNiNode() && !pActor->GetExtension()->IsRemote() && !pActor->actorState.IsDeadOrDying();
        if (cStillOurs && SitsByGraphAlone(pActor))
        {
            const char* cpTaken = LeaveFurnitureIdle(pActor);
            spdlog::info("Takeover: NPC {:X} sat in its furniture as a copy here by its graph alone; {}", pActor->formID,
                         cpTaken ? fmt::format("its graph took '{}' to leave it", cpTaken) : std::string("its graph took none of the furniture exits"));
        }
        it = s_gainedInFurniture.erase(it);
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

/**
 * Note that a copy here used a piece of furniture, which this game's AI or an idle replayed on it made it do.
 * Read when this game gains the NPC (NoteGainedInFurniture).
 *
 * @param aFormId The copy's form id.
 * @param aFurnitureId The furniture reference's form id.
 */
void NoteCopyFurniture(uint32_t aFormId, uint32_t aFurnitureId) noexcept
{
    std::scoped_lock _{s_furnitureLock};
    if (s_copyFurniture.size() > kMaxCopyFurniture)
        s_copyFurniture.clear();
    s_copyFurniture[aFormId] = aFurnitureId;
}

/**
 * Note an NPC gained whose copy here used a piece of furniture and sits in it by its graph alone: its graph is sent the furniture's exit a moment later.
 *
 * @param apActor The NPC gained, still a copy here.
 */
void NoteGainedInFurniture(Actor* apActor) noexcept
{
    bool usedFurniture = false;
    {
        std::scoped_lock _{s_furnitureLock};
        usedFurniture = s_copyFurniture.erase(apActor->formID) > 0;
    }
    if (usedFurniture && !apActor->IsDead() && SitsByGraphAlone(apActor))
        s_gainedInFurniture.push_back({apActor->formID, std::chrono::steady_clock::now() + kFurnitureResetAfter});
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

// TEMPORARY: the last actions replayed on each NPC copy and whether its graph took them ("+") or refused them ("-").
// They show what state a copy's graph was left in: logged 5 s after a copy's first replay and again when it is gained.
// After a gain, what this game's AI performs on that NPC for 6 s, and whether its graph takes it.
struct ReplayTrail
{
    std::deque<std::string> Last;
    std::chrono::steady_clock::time_point FirstAt{};
    std::chrono::steady_clock::time_point LastAt{};
    bool Said = false;
};
struct OwnActions
{
    std::chrono::steady_clock::time_point Until{};
    std::map<std::string, uint32_t> Counts;
};
std::mutex s_trailLock;
TiltedPhoques::Map<uint32_t, ReplayTrail> s_trails;
TiltedPhoques::Map<uint32_t, OwnActions> s_ownActions;

std::string TrailText(const ReplayTrail& acTrail) noexcept
{
    std::string text;
    for (const auto& cEntry : acTrail.Last)
        text += " " + cEntry;
    return text.empty() ? " none" : text;
}

void RunTrails() noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    TiltedPhoques::Vector<std::pair<uint32_t, std::string>> copies;
    TiltedPhoques::Vector<std::pair<uint32_t, std::string>> gained;
    {
        std::scoped_lock _{s_trailLock};
        for (auto it = s_trails.begin(); it != s_trails.end(); ++it)
        {
            if (!it->second.Said && cNow - it->second.FirstAt >= std::chrono::seconds(5))
            {
                it.value().Said = true;
                copies.push_back({it->first, TrailText(it->second)});
            }
        }
        for (auto it = s_ownActions.begin(); it != s_ownActions.end();)
        {
            if (cNow < it->second.Until)
            {
                ++it;
                continue;
            }
            std::string text;
            for (const auto& [cEvent, cCount] : it->second.Counts)
                text += fmt::format(" {} x{}", cEvent, cCount);
            gained.push_back({it->first, text.empty() ? " none" : text});
            it = s_ownActions.erase(it);
        }
    }
    for (const auto& [cFormId, cText] : copies)
        spdlog::info("TakeoverDiag: copy {:X}, its last replays 5 s after its first:{}", cFormId, cText);
    for (const auto& [cFormId, cText] : gained)
        spdlog::info("TakeoverDiag: NPC {:X}, what this game's AI performed on it in the 6 s after the gain:{}", cFormId, cText);
}

/**
 * TEMPORARY: what might show an NPC waiting in a scripted idle (a ceiling hole, a coffin), read as this game gains it.
 * Its owner's game has it in its furniture, while the copy here only replays the idle, so the game sets no sit state here.
 *
 * @param apActor The NPC gained.
 * @param apInterpolation Its playback of its last owner's reports, if it has one.
 */
void LogScriptedIdleCandidates(Actor* apActor, const InterpolationComponent* apInterpolation) noexcept
{
    const std::optional<bool> cAnimationDriven = GraphAnimationDriven(apActor);

    uint32_t furnitureId = 0;
    {
        std::scoped_lock _{s_furnitureLock};
        if (const auto it = s_copyFurniture.find(apActor->formID); it != s_copyFurniture.end())
            furnitureId = it->second;
    }

    const float cOwnerSpeed = apInterpolation ? InterpolationSystem::ReportedSpeed(apActor->formID, *apInterpolation) : 0.f;
    spdlog::info("ClaimDiag: NPC {:X} gained: sit/sleep state {}, furniture used here as a copy {}, its last owner's speed {:.0f}, bAnimationDriven {}", apActor->formID,
                 apActor->actorState.SitSleepState(), furnitureId ? fmt::format("{:X}", furnitureId) : std::string("none"), cOwnerSpeed,
                 cAnimationDriven ? (*cAnimationDriven ? "true" : "false") : "absent");
}

// TEMPORARY: the second reading of each gained NPC, the action trails that are due, and a line each time an NPC this game runs starts or stops swimming.
void RunTakeoverDiag(World& aWorld) noexcept
{
    RunTrails();
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
    LogScriptedIdleCandidates(apActor, apInterpolation);
    LogGainedPlace(apActor, apInterpolation);
    s_takeoverWatches.push_back({apActor->formID, std::chrono::steady_clock::now() + std::chrono::seconds(3)});
    LogTrailAtGain(apActor, "");
}

void LogTrailAtGain(const Actor* apActor, const char* acpHow) noexcept
{
    std::string trail = " none";
    {
        std::scoped_lock _{s_trailLock};
        if (const auto it = s_trails.find(apActor->formID); it != s_trails.end())
        {
            trail = TrailText(it->second);
            s_trails.erase(it);
        }
        s_ownActions[apActor->formID] = OwnActions{std::chrono::steady_clock::now() + std::chrono::seconds(6), {}};
    }
    spdlog::info("TakeoverDiag: NPC {:X} gained{}, its last replays as a copy:{}", apActor->formID, acpHow, trail);
}

void NoteReplay(uint32_t aFormId, const char* acpEvent, bool aTaken) noexcept
{
    std::scoped_lock _{s_trailLock};
    if (s_trails.size() > 1024)
        s_trails.clear();
    auto& trail = s_trails[aFormId];
    const auto cNow = std::chrono::steady_clock::now();
    // A copy with no replay for a minute is most likely a new one of the same NPC: its trail starts again.
    if (cNow - trail.LastAt > std::chrono::seconds(60))
        trail = ReplayTrail{{}, cNow, cNow, false};
    trail.LastAt = cNow;
    trail.Last.push_back(std::string(acpEvent && *acpEvent ? acpEvent : "?") + (aTaken ? "+" : "-"));
    if (trail.Last.size() > 12)
        trail.Last.pop_front();
}

void NoteOwnAction(uint32_t aFormId, const char* acpEvent, bool aTaken) noexcept
{
    std::scoped_lock _{s_trailLock};
    const auto it = s_ownActions.find(aFormId);
    if (it == s_ownActions.end() || std::chrono::steady_clock::now() >= it->second.Until)
        return;
    it.value().Counts[std::string(acpEvent && *acpEvent ? acpEvent : "?") + (aTaken ? "+" : "-")]++;
}

void Run(World& aWorld) noexcept
{
    RunGainedMoving();
    RunGainedInFurniture();
    RunTakeoverDiag(aWorld);
}
} // namespace Takeover
