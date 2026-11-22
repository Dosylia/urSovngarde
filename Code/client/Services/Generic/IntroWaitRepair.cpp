#include <TiltedOnlinePCH.h>
#include <Services/Generic/IntroWaitRepair.h>

#include <Forms/TESQuest.h>
#include <PlayerCharacter.h>

#include <algorithm>
#include <array>

namespace
{
// The intro's stages whose scripts switch waiting off.
constexpr std::array<uint16_t, 3> kWaitingOffStages{10, 65, 80};
// The intro's last stage, which completes it.
constexpr uint16_t kIntroLastStage = 1000;
// The intro's stage whose script switches waiting, sleeping and saving back on.
constexpr uint16_t kWaitingBackOnStage = 255;

/**
 * Read which of the intro's stages that touch waiting ran in this game.
 *
 * @param apIntro The intro quest.
 * @return Which of them ran.
 */
IntroWaitRepair::IntroStages ReadIntroStages(TESQuest* apIntro) noexcept
{
    const bool cSwitchedOff = std::ranges::any_of(kWaitingOffStages, [apIntro](const uint16_t aStage) { return apIntro->IsStageDone(aStage); });
    return {.switchedWaitingOff = cSwitchedOff, .over = apIntro->IsStageDone(kIntroLastStage), .switchedWaitingOn = apIntro->IsStageDone(kWaitingBackOnStage)};
}
} // namespace

/**
 * Switch waiting back on when the intro left it off, and say in the log when a script keeps it off.
 * Called on the main thread after each load and after another game's intro stage is applied here.
 */
void IntroWaitRepair::RepairWaiting() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESQuest* pIntro = Cast<TESQuest>(TESForm::GetById(kIntroQuestId));
    if (!pPlayer || !pIntro)
        return;

    const uint8_t cFlags = pPlayer->chargenFlags;
    const IntroStages cIntro = ReadIntroStages(pIntro);
    if (IsWaitingLeftOffByIntro(cIntro, cFlags))
    {
        pPlayer->chargenFlags = WithWaitingOn(cFlags);
        spdlog::info("IntroWaitRepair: the intro switched waiting off and is over, but its stage {} never ran here, so every wait and bed was refused; waiting switched back on",
                     kWaitingBackOnStage);
        return;
    }

    if (IsWaitingOff(cFlags))
        spdlog::info("IntroWaitRepair: a script keeps waiting off (the intro switched it off here {}, is over {}, ran its stage {} {}); left as it is", cIntro.switchedWaitingOff,
                     cIntro.over, kWaitingBackOnStage, cIntro.switchedWaitingOn);
}
