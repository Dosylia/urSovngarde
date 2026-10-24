#include <Forms/TESQuest.h>

#include <Services/PapyrusService.h>

#include <Games/Overrides.h>

TESObjectREFR* TESQuest::GetAliasedRef(uint32_t aAliasID) noexcept
{
    TP_THIS_FUNCTION(TGetAliasedRef, BSPointerHandle<TESObjectREFR>*, TESQuest, BSPointerHandle<TESObjectREFR>*, uint32_t);
    POINTER_SKYRIMSE(TGetAliasedRef, getAliasedRef, 25066, 25066);

    BSPointerHandle<TESObjectREFR> result{};
    TiltedPhoques::ThisCall(getAliasedRef, this, &result, aAliasID);

    return TESObjectREFR::GetByHandle(result.handle.iBits);
}

TESQuest::State TESQuest::getState()
{
    if (flags >= 0)
    {
        if (unkFlags)
            return State::WaitingPromotion;
        else if (flags & 1)
            return State::Running;
        else
            return State::Stopped;
    }

    return State::WaitingForStage;
}

void TESQuest::SetCompleted(bool force)
{
    TP_THIS_FUNCTION(TSetCompleted, void, TESQuest, bool);
    POINTER_SKYRIMSE(TSetCompleted, SetCompleted, 24991, 24991);
    SetCompleted(this, force);
}

void TESQuest::CompleteAllObjectives()
{
    TP_THIS_FUNCTION(TCompleteAllObjectives, void, TESQuest);
    POINTER_SKYRIMSE(TCompleteAllObjectives, CompleteAll, 23231, 23231);
    CompleteAll(this);
}

void TESQuest::SetActive(bool toggle)
{
    if (toggle)
        flags |= 0x800;
    else
        flags &= 0xF7FF;
}

bool TESQuest::IsStageDone(uint16_t stageIndex)
{
    // The game's own answer: no stage flag here is known to mean "done" (CommonLibVR names none at bit 0).
    using Quest = TESQuest;
    PAPYRUS_FUNCTION(bool, Quest, GetStageDone, int);
    return s_pGetStageDone(this, stageIndex);
}

bool TESQuest::Kill()
{
    using TSetStopped = void(TESQuest*, bool);
    POINTER_SKYRIMSE(TSetStopped, SetStopped, 24987, 24987);

    if (flags & Flags::Enabled)
    {
        unkFlags = 0;
        flags = Flags::Completed;
        MarkChanged(2);

        // SetStopped(this, false);
        return true;
    }

    return false;
}

bool TESQuest::EnsureQuestStarted(bool& success, bool force)
{
    TP_THIS_FUNCTION(TSetRunning, bool, TESQuest, bool*, bool);
    POINTER_SKYRIMSE(TSetRunning, SetRunning, 25003, 24481);
    return SetRunning(this, &success, force);
}

bool TESQuest::SetStage(uint16_t newStage)
{
    ScopedQuestOverride _;

    TP_THIS_FUNCTION(TSetStage, bool, TESQuest, uint16_t);
    POINTER_SKYRIMSE(TSetStage, SetStage, 25004, 25004);
    return SetStage(this, newStage);
}

bool TESQuest::ScriptSetStage(uint16_t stageIndex)
{
    if (IsStageReached(stageIndex))
        return false;

    using Quest = TESQuest;
    PAPYRUS_FUNCTION(void, Quest, SetCurrentStageID, int);
    s_pSetCurrentStageID(this, stageIndex);
    return IsStageReached(stageIndex);
}

bool TESQuest::IsStageReached(uint16_t aStage) noexcept
{
    // A stopped quest reads stage 0 before it ever ran, so its current stage counts only while it runs.
    // Otherwise a remote start, always at stage 0, would be taken as reached and never start it here.
    return (currentStage == aStage && !IsStopped()) || IsStageDone(aStage);
}

void TESQuest::SetStopped()
{
    flags &= 0xFFFE;
    MarkChanged(2);
}

static TiltedPhoques::Initializer s_questInitHooks(
    []()
    {
        // kill quest init in cold blood
        // TiltedPhoques::Write<uint8_t>(25003, 0xC3);
    });
