#pragma once

/**
 * Keeps this game's downed player in its bleedout until a revive, giving up, the end of the wait or a fallen party ends the down.
 * The game stands a bleeding-out player up as soon as its health is above zero (a level-up, a potion, a healing spell), from its own update, before ours runs.
 * So, while the down holds, the game's change of the player's life state from bleedout to alive is refused (Actor::SetLifeState).
 */
namespace DownedHold
{
/**
 * Say whether the player's down holds now.
 * PlayerService sets it while the player is down and waiting, and clears it the moment a revive, a respawn or a loaded save ends the down.
 *
 * @param aHolds True while only a revive, giving up, the end of the wait or a fallen party may end the down.
 */
void Set(bool aHolds) noexcept;
} // namespace DownedHold
