#pragma once

#include <cstdint>

/**
 * Switches waiting back on when the Helgen intro left it off: a repair of the save, not a fix of the quest sync.
 * The intro (MQ101) switches waiting and sleeping off at its stages 10, 65 and 80, and back on only at its stage 255, through the game's SetInChargen.
 * The game keeps that switch in the player's save and refuses every wait and every bed while it is off, with "You cannot wait in this location."
 * An intro that switched waiting off and is over without its stage 255 having run in this game leaves waiting off for good, so the repair switches it back on.
 * An alternate start ends the intro without those stages and handles waiting in its own script, so it is left alone.
 * Saving is not part of the rule: stage 65 switches it back on, so such a save has only the waiting bit.
 * A scene that switches waiting off for a while is cut short when the repair runs during it.
 */
namespace IntroWaitRepair
{
// The Helgen intro, MQ101, from Skyrim.esm, which always loads first.
constexpr uint32_t kIntroQuestId = 0x3372B;

// The bit of the player's chargen flags that keeps waiting and sleeping off.
constexpr uint8_t kWaitingOff = 1 << 1;

/** Which of the intro's stages that touch waiting ran in this game. */
struct IntroStages
{
    bool switchedWaitingOff; // one of its stages 10, 65 and 80 ran
    bool over;               // its last stage ran
    bool switchedWaitingOn;  // its stage 255 ran
};

/**
 * Tell whether a script keeps waiting and sleeping off.
 *
 * @param aChargenFlags The player's chargen flags, as the game keeps them in the save.
 * @return True while waiting is off.
 */
constexpr bool IsWaitingOff(const uint8_t aChargenFlags) noexcept
{
    return (aChargenFlags & kWaitingOff) != 0;
}

/**
 * Tell whether the intro left waiting off: it switched waiting off here, it is over, its stage that switches waiting back on never ran here, and waiting is still off.
 *
 * @param acIntro Which of the intro's stages that touch waiting ran in this game.
 * @param aChargenFlags The player's chargen flags, as the game keeps them in the save.
 * @return True when waiting is still off from the intro.
 */
constexpr bool IsWaitingLeftOffByIntro(const IntroStages& acIntro, const uint8_t aChargenFlags) noexcept
{
    return acIntro.switchedWaitingOff && acIntro.over && !acIntro.switchedWaitingOn && IsWaitingOff(aChargenFlags);
}

/**
 * Give the chargen flags with waiting and sleeping switched back on and the other flags kept.
 *
 * @param aChargenFlags The player's chargen flags, as the game keeps them in the save.
 * @return The same flags without the waiting bit.
 */
constexpr uint8_t WithWaitingOn(const uint8_t aChargenFlags) noexcept
{
    return static_cast<uint8_t>(aChargenFlags & ~kWaitingOff);
}

/**
 * Switch waiting back on when the intro left it off, and say in the log when a script keeps it off.
 * Called on the main thread after each load and after another game's intro stage is applied here.
 */
void RepairWaiting() noexcept;
} // namespace IntroWaitRepair
