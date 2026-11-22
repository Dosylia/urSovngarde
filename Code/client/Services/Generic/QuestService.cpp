#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/UpdateEvent.h>

#include <Services/QuestService.h>
#include <Services/ImguiService.h>
#include <Services/WarFollow.h>

#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <Games/Memory.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/RequestWarState.h>
#include <Messages/NotifyWarState.h>

#include <Forms/BGSKeyword.h>
#include <Interface/UI.h>
#include <Services/PapyrusService.h>
#include <Services/Generic/IntroWaitRepair.h>

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
    m_warStateConnection = aDispatcher.sink<NotifyWarState>().connect<&QuestService::OnWarState>(this);

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

    ForgetAppliedWarState();
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

// A load replaces the quests and the holds' data, so neither is read or written until it is over.
static bool IsWarReadable() noexcept
{
    static const BSFixedString s_loadingMenu("Loading Menu");
    static const BSFixedString s_mainMenu("Main Menu");
    const UI* pUI = UI::Get();
    return pUI && !pUI->GetMenuOpen(s_loadingMenu) && !pUI->GetMenuOpen(s_mainMenu);
}

static float GetHoldOwner(const TESForm* apHold, BGSKeyword* apOwnerKeyword) noexcept
{
    using Location = TESForm;
    PAPYRUS_FUNCTION(float, Location, GetKeywordData, BGSKeyword*);
    return s_pGetKeywordData(apHold, apOwnerKeyword);
}

static uint8_t GetObjectiveFlags(const TESQuest* apQuest, int aObjective) noexcept
{
    using Quest = TESQuest;
    PAPYRUS_FUNCTION(bool, Quest, IsObjectiveDisplayed, int);
    PAPYRUS_FUNCTION(bool, Quest, IsObjectiveCompleted, int);
    PAPYRUS_FUNCTION(bool, Quest, IsObjectiveFailed, int);
    return static_cast<uint8_t>((s_pIsObjectiveDisplayed(apQuest, aObjective) ? WarState::kDisplayed : 0) | (s_pIsObjectiveCompleted(apQuest, aObjective) ? WarState::kCompleted : 0) |
                                (s_pIsObjectiveFailed(apQuest, aObjective) ? WarState::kFailed : 0));
}

// False while a form is missing, which only a game without Skyrim.esm's war would have.
static bool ReadWarState(const TESQuest* apQuest, WarState& aState) noexcept
{
    auto* pKeyword = Cast<BGSKeyword>(TESForm::GetById(WarState::kOwnerKeyword));
    if (!pKeyword)
        return false;

    for (size_t i = 0; i < WarState::kHolds.size(); ++i)
    {
        const TESForm* pHold = TESForm::GetById(WarState::kHolds[i]);
        if (!pHold)
            return false;
        aState.Owners[i] = static_cast<uint8_t>(GetHoldOwner(pHold, pKeyword));
    }

    for (size_t i = 0; i < WarState::kObjectives.size(); ++i)
        aState.Objectives[i] = GetObjectiveFlags(apQuest, WarState::kObjectives[i]);

    return true;
}

void QuestService::SendWarState() noexcept
{
    constexpr auto cCheckEvery = 2s;
    constexpr auto cSendEvery = 30s;

    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextWarCheck)
        return;
    m_nextWarCheck = cNow + cCheckEvery;

    auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty() || !partyService.IsLeader() || !m_world.GetTransport().IsConnected() || !IsWarReadable())
        return;

    // A leader not in the war yet leaves its members' war alone.
    const TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(WarState::kObjectiveQuest));
    WarState state{};
    if (!pQuest || pQuest->IsStopped() || !ReadWarState(pQuest, state))
        return;

    const bool cChanged = !(state == m_sentWarState);
    if (!cChanged && cNow < m_nextWarSend)
        return;
    m_nextWarSend = cNow + cSendEvery;

    if (cChanged)
        spdlog::info("WarState: the war changed here; sent to the party");
    m_sentWarState = state;

    RequestWarState request{};
    request.State = state;
    m_world.GetTransport().Send(request);
}

/**
 * Give this game's holds the owners the leader's war gave them.
 * A hold that already has the leader's owner here, or was set to it before, is left alone.
 *
 * @param acLeader The leader's war.
 * @param acHere This game's war.
 * @param apKeyword The keyword whose data on a hold's location names its owner.
 * @param aApplied What this member last set; updated for each hold set.
 * @param aChanges Receives one note per hold set, for the log.
 */
static void FollowLeaderHolds(const WarState& acLeader, const WarState& acHere, BGSKeyword* apKeyword, WarState& aApplied, String& aChanges) noexcept
{
    using Location = TESForm;
    PAPYRUS_FUNCTION(void, Location, SetKeywordData, BGSKeyword*, float);

    for (size_t i = 0; i < WarState::kHolds.size(); ++i)
    {
        const uint8_t cWanted = acLeader.Owners[i];
        if (acHere.Owners[i] == cWanted || aApplied.Owners[i] == cWanted)
            continue;
        const TESForm* pHold = TESForm::GetById(WarState::kHolds[i]);
        if (!pHold)
            continue;

        s_pSetKeywordData(pHold, apKeyword, static_cast<float>(cWanted));
        aApplied.Owners[i] = cWanted;
        aChanges += spdlog::fmt_lib::format(" hold {:X} owner {} to {};", WarState::kHolds[i], acHere.Owners[i], cWanted);
    }
}

/**
 * Add to this game's war objectives the flags the leader's have and these lack.
 * No flag is taken away, and an objective already completed or failed in this game is left as it is (WarFollow::FlagsToAdd).
 *
 * @param apQuest The journal's war quest, running here.
 * @param acLeader The leader's war.
 * @param acHere This game's war.
 * @param aApplied What this member last set; updated for each objective looked at, so that one the game refuses is not tried again.
 * @param aChanges Receives one note per objective changed, for the log.
 */
static void FollowLeaderObjectives(TESQuest* apQuest, const WarState& acLeader, const WarState& acHere, WarState& aApplied, String& aChanges) noexcept
{
    using Quest = TESQuest;
    PAPYRUS_FUNCTION(void, Quest, SetObjectiveDisplayed, int, bool, bool);
    PAPYRUS_FUNCTION(void, Quest, SetObjectiveCompleted, int, bool);
    PAPYRUS_FUNCTION(void, Quest, SetObjectiveFailed, int, bool);

    for (size_t i = 0; i < WarState::kObjectives.size(); ++i)
    {
        const uint8_t cWanted = acLeader.Objectives[i];
        const uint8_t cHere = acHere.Objectives[i];
        if (cHere == cWanted || aApplied.Objectives[i] == cWanted)
            continue;
        aApplied.Objectives[i] = cWanted;

        const uint8_t cAdded = WarFollow::FlagsToAdd(cHere, cWanted);
        if (cAdded == 0)
            continue;

        const int cObjective = WarState::kObjectives[i];
        // Shown last: the journal then announces the objective in its final state.
        if (cAdded & WarState::kCompleted)
            s_pSetObjectiveCompleted(apQuest, cObjective, true);
        if (cAdded & WarState::kFailed)
            s_pSetObjectiveFailed(apQuest, cObjective, true);
        if (cAdded & WarState::kDisplayed)
            s_pSetObjectiveDisplayed(apQuest, cObjective, true, true);
        aChanges += spdlog::fmt_lib::format(" objective {} flags {} to {};", cObjective, cHere, cHere | cAdded);
    }
}

/**
 * Log that the leader's war is behind this game's, once for each state the leader sends.
 *
 * @param acLeader The leader's war.
 * @param aObjectiveIndex The index in WarState::kObjectives of an objective completed or failed in this game but not in the leader's.
 * @param aLastNoted The leader's state last logged as behind; updated.
 */
static void NoteLeaderBehind(const WarState& acLeader, const size_t aObjectiveIndex, WarState& aLastNoted) noexcept
{
    if (acLeader == aLastNoted)
        return;
    aLastNoted = acLeader;
    spdlog::info("WarState: the leader's war is behind this game's: objective {} is completed or failed in this game but not in the leader's, so this game keeps its own holds and finished objectives",
                 WarState::kObjectives[aObjectiveIndex]);
}

/**
 * Follow the party leader's civil war in this member's game, forward only.
 * The holds take the leader's owners and the journal's war objectives gain the leader's flags.
 * When the leader's war is behind this game's (an objective is completed or failed in this game but not in the leader's), its holds are left alone, as they would undo this game's own wins.
 * No objective loses a flag, and no hold is taken back while the leader's war is behind.
 *
 * @param acMessage The leader's war, as the server relays it.
 */
void QuestService::OnWarState(const NotifyWarState& acMessage) noexcept
{
    if (m_world.GetPartyService().IsLeader() || !IsWarReadable())
        return;

    auto* pQuest = Cast<TESQuest>(TESForm::GetById(WarState::kObjectiveQuest));
    auto* pKeyword = Cast<BGSKeyword>(TESForm::GetById(WarState::kOwnerKeyword));
    WarState here{};
    if (!pQuest || !pKeyword || !ReadWarState(pQuest, here))
        return;

    const WarState& cLeader = acMessage.State;
    String changes;

    if (const auto cFinishedOnlyHere = WarFollow::FirstFinishedOnlyHere(here.Objectives, cLeader.Objectives))
        NoteLeaderBehind(cLeader, *cFinishedOnlyHere, m_behindWarState);
    else
    {
        // Caught up: should the leader fall behind again with the same state, that is logged again.
        m_behindWarState.Objectives.fill(WarState::kNothingApplied);
        FollowLeaderHolds(cLeader, here, pKeyword, m_appliedWarState, changes);
    }

    // Objectives of a war quest that does not run here cannot be shown, and are set once it does.
    if (!pQuest->IsStopped())
        FollowLeaderObjectives(pQuest, cLeader, here, m_appliedWarState, changes);

    if (!changes.empty())
        spdlog::info("WarState: following the leader's war here:{}{}", changes, pQuest->IsStopped() ? " the war quest does not run here, so its objectives wait" : "");
}

void QuestService::OnUpdate(const UpdateEvent&) noexcept
{
    SendWarState();

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

    // Sent and applied afresh on the next connection, to a party that may have moved on.
    m_sentWarState = {};
    ForgetAppliedWarState();
}

/**
 * Forget what this member set from the leader's war, and which of the leader's states it found behind, so that the next state the leader sends is looked at afresh.
 */
void QuestService::ForgetAppliedWarState() noexcept
{
    for (WarState* pState : {&m_appliedWarState, &m_behindWarState})
    {
        pState->Owners.fill(WarState::kNothingApplied);
        pState->Objectives.fill(WarState::kNothingApplied);
    }
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
    // A save whose intro skipped the stage that switches waiting back on gets waiting back.
    m_world.GetRunner().Queue(&IntroWaitRepair::RepairWaiting);

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    // It only failed to start here, for one when a required alias did not fill, and others may be running it.
    if (apEvent->started && apEvent->failed)
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

    // A quest may run a stage more than once, as the war's CW does stage 4 for each hold, and a member follows its leader through them.
    // The leader keeps its own world, so it takes a member's stage only the first time.
    const bool cRepeat = !m_world.GetPartyService().IsLeader() && pQuest->AllowsRepeatedStages();
    const bool cAlreadyReached = pQuest->IsStageReached(aUpdate.Stage);
    if (aUpdate.Status == NotifyQuestUpdate::StageUpdate && cAlreadyReached && !cRepeat)
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
        pQuest->ScriptSetStage(aUpdate.Stage, cRepeat);
        pQuest->SetActive(true);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        pQuest->ScriptSetStage(aUpdate.Stage, cRepeat);
        bResult = true;
        // Whether the quest runs here: a stage set on a quest that cannot start here leaves its scripts unrun.
        spdlog::info("Remote quest updated: {:X}, stage: {}; running here {}{}", formId, aUpdate.Stage, pQuest->IsEnabled() ? "yes" : "no", cAlreadyReached ? "; run again" : "");
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default: break;
    }

    if (!bResult)
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);

    // Another game's intro stage can end the intro here after the load's repair ran.
    if (formId == IntroWaitRepair::kIntroQuestId)
        IntroWaitRepair::RepairWaiting();
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
