#pragma once

#include <cstdint>

struct Actor;
struct World;

/**
 * Decides whether this player and another can hurt each other.
 * The server's PvP setting decides; a flat Skyrim player and a Skyrim VR player never can, until combat rules for the two exist.
 * Fighting NPCs together is not affected.
 */
namespace PvpRules
{
/**
 * Check if this player and the player behind a copy in this game can hurt each other.
 * Safe on any thread: it reads only the server's setting and the copy's own mark.
 *
 * @param apCopy Another player's copy in this game.
 * @return True when PvP is on and the copy's player plays the same game as this player.
 */
bool IsAllowedWithCopy(Actor* apCopy) noexcept;

/**
 * Check if this player and another player, named by id, can hurt each other, as when a message names its attacker.
 * Main thread only.
 *
 * @param aPlayerId The other player's id.
 * @return True when PvP is on and that player plays the same game as this player.
 */
bool IsAllowedWithPlayer(uint32_t aPlayerId) noexcept;

/**
 * Mark each other player's copy whose player plays the other game, flat Skyrim or Skyrim VR, so IsAllowedWithCopy can tell on any thread.
 * Main thread only; run every frame, so a copy is marked before it can hit or be hit.
 *
 * @param aWorld The client's world, which lists the other players' copies.
 */
void MarkOtherGameCopies(World& aWorld) noexcept;
} // namespace PvpRules
