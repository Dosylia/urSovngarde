#pragma once

/**
 * Holds the game's own input back while the menu is open, without pausing the game.
 * The devices are still polled, so the menu reads the controllers as before; only what the game's listeners get changes.
 * Releases still reach the game, so no button stays down behind the menu, and sticks reach it at rest, so the player does not walk on.
 * A button pressed while the menu is open stays the menu's until it is let go, so the click that closed it never swings or casts.
 * Plugins that read the controllers on their own (HIGGS and PLANCK through SKSE VR) are not held.
 */
namespace GameInputHold
{
/**
 * Put the hold on the game's input poll, at the call that hands each poll's events to the game.
 * Called at the first update, once the plugins that hook the same call have done so (SKSE, at input load); later calls do nothing.
 * When the game's code there is not the expected one, nothing is changed and an error is logged.
 */
void Install() noexcept;

/**
 * Hold the game's input back, or let it through again.
 *
 * @param aHeld True while the menu is open in a way that should keep the game from acting on input.
 */
void SetHeld(bool aHeld) noexcept;
} // namespace GameInputHold
