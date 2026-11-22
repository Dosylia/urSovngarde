#pragma once

class LocalSleepVote;

/**
 * The Sleep/Wait menu's OK button while connected: a press is held as this player's vote, and the game's own OK runs once every player agrees.
 * Offline the button works as in the game.
 */
namespace SleepWaitOk
{
/**
 * Give this game's part in the vote, shared by the OK hook, the client's update and the main loop.
 *
 * @return The one vote of this game.
 */
LocalSleepVote& GetLocalVote() noexcept;

/**
 * Find the Sleep/Wait menu that is open now.
 *
 * @return The menu; null when none is open.
 */
const void* FindOpenMenu() noexcept;

/**
 * Press the game's own OK for the agreed length, once the vote has settled it.
 * Called from the main loop, on the main thread, before the frame's update.
 */
void PressAgreedOk() noexcept;

/**
 * Let the OK button hold presses as votes: the main loop now calls PressAgreedOk.
 * Until then the button works as in the game, so a vote can never wait on a main loop that does not run it.
 */
void EnableHold() noexcept;
} // namespace SleepWaitOk
