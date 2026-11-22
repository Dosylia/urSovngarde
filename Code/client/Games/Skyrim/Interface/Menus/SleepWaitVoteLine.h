#pragma once

#include <string>

/**
 * The line the Sleep/Wait menu shows while this player's vote waits on the others, in place of the menu's question.
 * A VR player sees no HUD line over the open menu, so the count goes into the menu itself.
 * The question comes back when the vote is released and the menu stays open.
 */
namespace SleepWaitVoteLine
{
/**
 * Show a line in the open menu instead of its question. Any thread.
 *
 * @param acLine The line, short enough for one line of the question's field.
 */
void Show(const std::string& acLine) noexcept;

/**
 * Show the menu's own question again. Any thread.
 */
void Clear() noexcept;

/**
 * Write the line into the open menu, or put the question back, once it changed.
 * Called from the main loop, on the main thread, where the game's own menu code runs.
 */
void Update() noexcept;
} // namespace SleepWaitVoteLine
