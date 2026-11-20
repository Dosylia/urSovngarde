#pragma once

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

struct World;

/**
 * Moves this game's player to where another player is, as the server describes the place: a cell, or a world space and a position in it.
 * The chat's teleport command and the menu's teleport button both end here.
 */
namespace PlayerTeleport
{
/**
 * Move the player to a place another player is, loading the outdoor cell there when this game has not loaded it.
 * When the place cannot be found, the player stays where they are and the chat says the teleport failed.
 *
 * @param aWorld The client's world, for the plugins' ids and the chat.
 * @param acCellId The cell, as the server names it: an interior, or an outdoor cell this game may not have loaded.
 * @param acWorldSpaceId The world space, as the server names it, to load an outdoor cell this game has not loaded.
 * @param acPosition Where in that cell, in game units.
 */
void MoveTo(World& aWorld, const GameId& acCellId, const GameId& acWorldSpaceId, const Vector3_NetQuantize& acPosition) noexcept;
} // namespace PlayerTeleport
