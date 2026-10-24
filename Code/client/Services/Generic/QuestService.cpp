#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/UpdateEvent.h>

#include <Services/QuestService.h>
#include <Services/ImguiService.h>

#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <Games/Memory.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>

static TESQuest* FindQuestByNameId(const String& name)
{
    auto& questRegistry = ModManager::Get()->quests;
    auto it = std::find_if(questRegistry.begin(), questRegistry.end(), [name](auto* it) { return std::strcmp(it->idName.AsAscii(), name.c_str()); });

    return it != questRegistry.end() ? *it : nullptr;
}

using HeldStages = QuestService::HeldStages;

static void ApplyHeldStages(TESQuest* apQuest, HeldStages& aHeld, size_t aCount) noexcept;
static void ReleaseStagesForWaitingPhases(TESQuest* apQuest, HeldStages& aHeld) noexcept;
static void ReleaseUncheckedStages(TESQuest* apQuest, HeldStages& aHeld) noexcept;

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestService::OnUpdate>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&QuestService::OnDisconnected>(this);
    m_leftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&QuestService::OnPartyLeft>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);

    // Game events:
    // Starts, stops and stages are sent from TESQuestStartStopEvent and TESQuestStageEvent, which fire while the quest
    // changes. Outside a party nothing is sent.
    // TESQuestStageItemDoneEvent fires for a stage's log entries whose fragment ran, after it ran. That's too late to
    // send on, but it tells when an applied held stage is done.
    // The scene, scene action, phase and dialogue line events are only logged, to follow scenes when debugging held
    // stages. OnUpdate explains why they can't trigger the release of held stages.

    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
    pEventList->questStageItemDoneEvent.RegisterSink(this);
    pEventList->sceneEvent.RegisterSink(this);
    pEventList->sceneActionEvent.RegisterSink(this);
    pEventList->scenePhaseEvent.RegisterSink(this);
    pEventList->topicInfoEvent.RegisterSink(this);
    pEventList->loadGameEvent.RegisterSink(this);
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // TODO: this should be followed with whatever the quest leader selected
    /*
    // deselect any active quests
    auto* pPlayer = PlayerCharacter::Get();
    for (auto& objective : pPlayer->objectives)
    {
        if (auto* pQuest = objective.instance->quest)
            pQuest->SetActive(false);
    }
    */
}

void QuestService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_heldStages.empty())
        return;

    // Only the leader holds member stages.
    // A game that no longer leads applies what it held, before newer stages come in to apply at once.
    if (!m_world.GetPartyService().IsLeader())
    {
        FlushHeldStages("this game no longer leads the party");
        return;
    }

    // A playing scene can wait on what this game cannot make happen, a package another game runs for one.
    // Its phase then never ends here, so after this long without progress the held stages are applied anyway.
    constexpr auto cNoProgressFlush = 30s;
    const auto cNow = std::chrono::steady_clock::now();

    // Checked every frame rather than on scene events: an action's done event fires before its fragment runs, and the
    // action only counts as complete afterwards, with no event of its own
    Vector<uint32_t> questFormIds;
    for (const auto& [questFormId, _] : m_heldStages)
        questFormIds.push_back(questFormId);

    for (uint32_t questFormId : questFormIds)
    {
        TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(questFormId));
        if (HeldStages* pHeld = GetHeldStages(questFormId, pQuest))
        {
            ReleaseStagesForWaitingPhases(pQuest, *pHeld);
            ReleaseUncheckedStages(pQuest, *pHeld);

            if (!pHeld->Stages.empty() && cNow - pHeld->LastProgress > cNoProgressFlush)
            {
                spdlog::info("QuestHold: no progress on the held member quest stages for {:X} in 30 s; applying the {} left in order", questFormId, pHeld->Stages.size());
                ApplyHeldStages(pQuest, *pHeld, pHeld->Stages.size());
            }
        }
    }
}

void QuestService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    FlushHeldStages("disconnected");
}

void QuestService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    FlushHeldStages("this player left the party");
}

void QuestService::FlushHeldStages(const char* acpReason) noexcept
{
    // Quest scripts are not run while the game closes.
    if (IsProcessExiting())
    {
        m_heldStages.clear();
        return;
    }

    Vector<uint32_t> questFormIds;
    for (const auto& [questFormId, _] : m_heldStages)
        questFormIds.push_back(questFormId);

    for (uint32_t questFormId : questFormIds)
    {
        TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(questFormId));
        HeldStages* pHeld = GetHeldStages(questFormId, pQuest);
        if (!pHeld || pHeld->Stages.empty())
            continue;

        spdlog::info("QuestHold: applying the {} held member quest stages for {:X} now, {}", pHeld->Stages.size(), questFormId, acpReason);
        ApplyHeldStages(pQuest, *pHeld, pHeld->Stages.size());
    }

    m_heldStages.clear();
}

BSTEventResult QuestService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    // A load brings the save's quests back: fragments applied before it will not report, and its scenes start again.
    // The held stages stay held, and their waits start over.
    m_world.GetRunner().Queue(
        [this]()
        {
            const auto cNow = std::chrono::steady_clock::now();
            for (auto it = m_heldStages.begin(); it != m_heldStages.end(); ++it)
            {
                it.value().Applied.clear();
                it.value().LastProgress = cNow;
            }
        });

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    if (!m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest start/stop event: {:X}", apEvent->formId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;
     
        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(),  pQuest->currentStage, pQuest->IsStopped() ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), 
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }
        
        m_world.GetRunner().Queue(
            [&, formId = pQuest->formID, stageId = pQuest->currentStage, stopped = pQuest->IsStopped(), type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type); 

                    m_world.GetTransport().Send(update);
                }
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    if (!m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest stage event: {:X}, stage: {}", apEvent->formId, apEvent->stageId);

    // there is no reason to even fetch the quest object, since the event provides everything already....
    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(), pQuest->currentStage,
                             RequestQuestUpdate::StageUpdate,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [&, formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = RequestQuestUpdate::StageUpdate;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                }
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageItemDoneEvent* apEvent, const EventDispatcher<TESQuestStageItemDoneEvent>*)
{
    // Sent once the fragment of one of the stage's log entries has run, usually from a script thread
    m_world.GetRunner().Queue(
        [this, questFormId = apEvent->formId, stage = apEvent->stageId]()
        {
            auto it = m_heldStages.find(questFormId);
            if (it == m_heldStages.end())
                return;

            Vector<HeldStages::AppliedStage>& applied = it.value().Applied;
            auto appliedIt = std::find_if(applied.begin(), applied.end(), [stage](const HeldStages::AppliedStage& acApplied) { return acApplied.Stage == stage; });
            if (appliedIt == applied.end())
                return;

            applied.erase(appliedIt);
            it.value().LastProgress = std::chrono::steady_clock::now();
            spdlog::debug("Fragment of applied member quest stage {} for {:X} done, {} applied stages left", stage, questFormId, applied.size());
        });

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESSceneEvent* apEvent, const EventDispatcher<TESSceneEvent>*)
{
    spdlog::debug("Scene {:X} {}", apEvent->sceneFormId, apEvent->type == 0 ? "begin" : "end");
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESSceneActionEvent* apEvent, const EventDispatcher<TESSceneActionEvent>*)
{
    spdlog::debug("Scene {:X} action {} done, quest: {:X}, alias: {}", apEvent->sceneFormId, apEvent->actionIndex, apEvent->questFormId, apEvent->aliasId);
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESScenePhaseEvent* apEvent, const EventDispatcher<TESScenePhaseEvent>*)
{
    if (!spdlog::should_log(spdlog::level::debug))
        return BSTEventResult::kOk;

    BGSScene* pScene = Cast<BGSScene>(TESForm::GetById(apEvent->sceneFormId));
    if (!pScene)
        return BSTEventResult::kOk;

    // A phase without completion conditions ends once every action running in it is complete
    String actions;
    for (BGSSceneAction* pAction : pScene->actions)
    {
        if (!pAction || !pAction->IsInPhase(apEvent->phaseIndex))
            continue;

        constexpr std::array cTypeNames{"dialogue", "package", "timer"};
        const uint32_t cType = static_cast<uint32_t>(pAction->GetType());
        actions += spdlog::fmt_lib::format(
            " [{} {} alias {} {}]", pAction->GetIndex(), cType < cTypeNames.size() ? cTypeNames[cType] : "unknown", pAction->actorId,
            pAction->IsComplete() ? "complete" : "running");
    }

    spdlog::debug("Scene {:X} phase {} {}, actions:{}", apEvent->sceneFormId, apEvent->phaseIndex, apEvent->type == 0 ? "begin" : "end", actions);
    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESTopicInfoEvent* apEvent, const EventDispatcher<TESTopicInfoEvent>*)
{
    spdlog::debug(
        "Dialogue line {:X} {}, speaker: {:X}", apEvent->uiTopicInfoFormID, apEvent->eType == 0 ? "begin" : "end", apEvent->pSpeakerRef ? apEvent->pSpeakerRef->formID : 0);
    return BSTEventResult::kOk;
}

static bool Compare(TESConditionItem::OpCode aOpCode, float aLhs, float aRhs) noexcept
{
    switch (aOpCode)
    {
    case TESConditionItem::OpCode::kEqualTo: return aLhs == aRhs;
    case TESConditionItem::OpCode::kNotEqualTo: return aLhs != aRhs;
    case TESConditionItem::OpCode::kGreaterThan: return aLhs > aRhs;
    case TESConditionItem::OpCode::kGreaterThanOrEqualTo: return aLhs >= aRhs;
    case TESConditionItem::OpCode::kLessThan: return aLhs < aRhs;
    case TESConditionItem::OpCode::kLessThanOrEqualTo: return aLhs <= aRhs;
    default: return false;
    }
}

// Whether setting the stage turns a GetStage/GetStageDone check on the quest from false to true.
// With aAnyChange, also from true to false (a phase waiting on GetStage == N).
static bool IsCheckedByStage(const TESCondition& acConditions, TESQuest* apQuest, uint16_t aStage, bool aAnyChange) noexcept
{
    for (const TESConditionItem* pItem = acConditions.pHead; pItem; pItem = pItem->pNext)
    {
        if (pItem->UsesGlobal() || pItem->params[0] != apQuest)
            continue;

        const TESConditionItem::OpCode cOpCode = pItem->GetOpCode();
        bool before = false;
        bool after = false;
        if (pItem->function == TESConditionItem::kGetStage)
        {
            // Stages only go up, GetStage returns the highest one set
            before = Compare(cOpCode, apQuest->currentStage, pItem->comparisonValue);
            after = Compare(cOpCode, std::max(apQuest->currentStage, aStage), pItem->comparisonValue);
        }
        else if (pItem->function == TESConditionItem::kGetStageDone)
        {
            const uint16_t cStage = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(pItem->params[1]));
            if (cStage == aStage)
            {
                before = Compare(cOpCode, 0.f, pItem->comparisonValue);
                after = Compare(cOpCode, 1.f, pItem->comparisonValue);
            }
        }
        else
        {
            continue;
        }

        if (!before && after)
            return true;

        if (aAnyChange && before != after)
            return true;
    }

    return false;
}

// Whether the phase's own lines are done, so passing its completion conditions won't cut one off. Timers and packages
// don't hold such a phase back, and packages never complete on clients that don't own the NPC.
static bool IsPhaseWaitingOnConditions(BGSScene* apScene, uint32_t aPhaseIndex) noexcept
{
    for (BGSSceneAction* pAction : apScene->actions)
    {
        if (!pAction || !pAction->IsInPhase(aPhaseIndex))
            continue;

        if (pAction->GetType() != BGSSceneAction::Type::kDialogue || pAction->IsComplete() || pAction->Loops())
            continue;

        // The player's own lines wait for this player to answer, while a member may have answered instead
        if (apScene->owningQuest && apScene->owningQuest->GetAliasedRef(pAction->actorId) == PlayerCharacter::Get())
            continue;

        return false;
    }

    return true;
}

// Whether the stage changes a check of a playing scene's running phase, or of the phase after it.
// Phases further on are not waited for: a long scene, an escort through a dungeon, checks most of its quest's stages somewhere.
// Scenes that only end or repeat on it don't count (MQ105's sprint trial).
// apSceneId and apPhase, when given, say which scene and phase check it.
static bool IsCheckedByPlayingScene(TESQuest* apQuest, uint16_t aStage, uint32_t* apSceneId = nullptr, uint32_t* apPhase = nullptr) noexcept
{
    for (BGSScene* pScene : apQuest->scenes)
    {
        if (!pScene)
            continue;

        const uint32_t cFirst = pScene->GetNextPhaseIndex();
        const uint32_t cEnd = std::min<uint32_t>(cFirst + 2, pScene->phases.length);
        for (uint32_t i = cFirst; i < cEnd; ++i)
        {
            const BGSScenePhase* pPhase = pScene->phases[i];
            if (!pPhase)
                continue;

            // The running phase is past its start conditions
            const bool cIsRunning = pScene->currentPhaseIndex == i;
            if ((!cIsRunning && IsCheckedByStage(pPhase->startConditions, apQuest, aStage, true)) ||
                IsCheckedByStage(pPhase->completionConditions, apQuest, aStage, true))
            {
                if (apSceneId)
                    *apSceneId = pScene->formID;
                if (apPhase)
                    *apPhase = i;
                return true;
            }
        }
    }

    return false;
}

void QuestService::HoldStage(TESQuest* apQuest, uint16_t aStage, uint32_t aSceneId, uint32_t aPhase) noexcept
{
    HeldStages& held = m_heldStages[apQuest->formID];
    // Holding more is no progress: the wait counts from the first stage held.
    if (held.Stages.empty() && held.Applied.empty())
        held.LastProgress = std::chrono::steady_clock::now();

    Vector<uint16_t>& stages = held.Stages;
    auto it = std::lower_bound(stages.begin(), stages.end(), aStage);
    if (it == stages.end() || *it != aStage)
        stages.insert(it, aStage);

    if (aSceneId)
        spdlog::info("Holding member quest stage {} for {:X} until no playing scene of it checks it: scene {:X} phase {} does", aStage, apQuest->formID, aSceneId, aPhase);
    else
        spdlog::info("Holding member quest stage {} for {:X} behind the stages held before it", aStage, apQuest->formID);
}

static void ApplyHeldStages(TESQuest* apQuest, HeldStages& aHeld, size_t aCount) noexcept
{
    ScopedQuestOverride _;

    // In order, a later stage's scripts can rely on the earlier ones
    for (size_t i = 0; i < aCount; ++i)
    {
        // A scene the fragments start only plays once they've run, and the next stage may be what it waits on.
        // The next held stage waits for this one's first done fragment, or for a while when none reports (see GetHeldStages).
        const uint16_t cStage = aHeld.Stages[i];
        if (apQuest->ScriptSetStage(cStage))
            aHeld.Applied.push_back({cStage, std::chrono::steady_clock::now()});
    }

    aHeld.Stages.erase(aHeld.Stages.begin(), aHeld.Stages.begin() + aCount);
    aHeld.LastProgress = std::chrono::steady_clock::now();
}

static void ReleaseStagesForWaitingPhases(TESQuest* apQuest, HeldStages& aHeld) noexcept
{
    // A phase whose own actions are done and whose completion conditions want a held stage only moves on with it.
    // Release up to the first stage that does it, the leader's scene then carries on at its own pace.
    for (BGSScene* pScene : apQuest->scenes)
    {
        if (aHeld.Stages.empty())
            break;

        if (!pScene || !pScene->isPlaying || pScene->currentPhaseIndex >= pScene->phases.length)
            continue;

        BGSScenePhase* pPhase = pScene->phases[pScene->currentPhaseIndex];
        if (!pPhase)
            continue;

        // Conditions first, they rarely mention a held stage and are cheaper than walking the phase's actions
        const auto cStageIt = std::find_if(
            aHeld.Stages.begin(), aHeld.Stages.end(),
            [&](uint16_t aStage) { return IsCheckedByStage(pPhase->completionConditions, apQuest, aStage, false); });
        if (cStageIt == aHeld.Stages.end() || !IsPhaseWaitingOnConditions(pScene, pScene->currentPhaseIndex))
            continue;

        spdlog::info(
            "Releasing held member quest stages for {:X} up to {}, scene {:X} phase {} waits on it", apQuest->formID, *cStageIt, pScene->formID,
            pScene->currentPhaseIndex);
        ApplyHeldStages(apQuest, aHeld, cStageIt - aHeld.Stages.begin() + 1);
    }
}

// Applies held stages in order while no playing scene checks the next one, e.g. once the scenes that did have ended.
// Waits first for each stage it applied to report a done fragment, or for two seconds.
static void ReleaseUncheckedStages(TESQuest* apQuest, HeldStages& aHeld) noexcept
{
    while (!aHeld.Stages.empty() && aHeld.Applied.empty() && !IsCheckedByPlayingScene(apQuest, aHeld.Stages.front()))
    {
        spdlog::info("Applying held member quest stage {} for {:X}, no playing scene checks it", aHeld.Stages.front(), apQuest->formID);
        ApplyHeldStages(apQuest, aHeld, 1);
    }
}

HeldStages* QuestService::GetHeldStages(uint32_t aQuestFormId, TESQuest* apQuest) noexcept
{
    auto it = m_heldStages.find(aQuestFormId);
    if (it == m_heldStages.end())
        return nullptr;

    // Setting a stage on a stopped quest starts it again, and scenes also end because their quest stopped
    HeldStages& held = it.value();
    if (!apQuest || apQuest->IsStopped())
    {
        if (!held.Stages.empty())
            spdlog::info("Dropping held member quest stages for {:X}, quest stopped or missing", aQuestFormId);

        m_heldStages.erase(it);
        return nullptr;
    }

    // Reached by the leader's own scripts in the meantime
    std::erase_if(
        held.Stages,
        [apQuest](uint16_t aStage)
        {
            if (!apQuest->IsStageReached(aStage))
                return false;

            spdlog::info("Dropping held member quest stage {} for {:X}, already reached here", aStage, apQuest->formID);
            return true;
        });

    // A stage runs only the log entries whose conditions pass, and one without a fragment may report nothing.
    // So the first done fragment ends a stage's wait, and a stage that reports none is waited on for two seconds.
    constexpr auto cAppliedWait = 2s;
    const auto cNow = std::chrono::steady_clock::now();
    std::erase_if(
        held.Applied,
        [&](const HeldStages::AppliedStage& acApplied)
        {
            if (cNow - acApplied.At < cAppliedWait)
                return false;

            spdlog::info("QuestHold: applied member quest stage {} for {:X}, no fragment reported done within 2 s; the next held stage may go", acApplied.Stage,
                         aQuestFormId);
            held.LastProgress = cNow;
            return true;
        });

    if (held.Stages.empty() && held.Applied.empty())
    {
        m_heldStages.erase(it);
        return nullptr;
    }

    return &held;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    uint32_t formId = modSystem.GetGameId(aUpdate.Id);
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest)
    {
        spdlog::error("Failed to find quest, base id: {:X}, mod id: {:X}", aUpdate.Id.BaseId, aUpdate.Id.ModId);
        return;
    }

    if (aUpdate.Status == NotifyQuestUpdate::StageUpdate && pQuest->IsStageReached(aUpdate.Stage))
    {
        spdlog::info("Remote quest {:X} already reached stage {}, current stage: {}", formId, aUpdate.Stage, pQuest->currentStage);
        return;
    }

    // A member stage the leader's playing scene checks can push it past its own scripts (a phase ending before its lines
    // and scripts ran), so hold it, and the stages after it, until the scene gets there or reaches it itself.
    const bool cIsLeaderStage = aUpdate.Status == NotifyQuestUpdate::StageUpdate && m_world.GetPartyService().IsLeader();
    HeldStages* pHeld = cIsLeaderStage || aUpdate.Status == NotifyQuestUpdate::Stopped ? GetHeldStages(formId, pQuest) : nullptr;
    uint32_t checkingSceneId = 0;
    uint32_t checkingPhase = 0;
    if (cIsLeaderStage && (pHeld || IsCheckedByPlayingScene(pQuest, aUpdate.Stage, &checkingSceneId, &checkingPhase)))
    {
        HoldStage(pQuest, aUpdate.Stage, checkingSceneId, checkingPhase);
        return;
    }

    // The member reached the held stages before stopping the quest, and once it's stopped here they're dropped
    if (pHeld && !pHeld->Stages.empty())
    {
        spdlog::info("Applying {} held member quest stages for {:X} before it stops", pHeld->Stages.size(), formId);
        ApplyHeldStages(pQuest, *pHeld, pHeld->Stages.size());
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
    }

    // The quest events fire synchronously while the stage is set, don't send them back
    ScopedQuestOverride _;

    bool bResult = false;
    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
    {
        pQuest->ScriptSetStage(aUpdate.Stage);
        pQuest->SetActive(true);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        pQuest->ScriptSetStage(aUpdate.Stage);
        bResult = true;
        spdlog::info("Remote quest updated: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default: break;
    }

    if (!bResult)
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        pQuest->SetActive(false);
        pQuest->SetStopped();
        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    // 0xFE014801,  // Unknown dynamic ID, kept as note, maybe lookup correct ID this game?
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    // Quests with no quest stages are never synced. Most TESQues::Type:: quests should
    // be synced, including Type::None and Type::Miscellaneous, but there are a few
    // known exceptions that should be excluded that are in the table.
    return    apQuest->stages.Empty() 
           || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), apQuest->formID) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    auto& quests = ModManager::Get()->quests;
    for (TESQuest* pQuest : quests)
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, pQuest->idName.AsAscii());
}
