#pragma once

#include <Structs/GameId.h>

#include <glm/glm.hpp>

struct World;
struct TESObjectCELL;

/**
 * Finds the cell of a place the server describes: a cell, or a world space and a position in it.
 * Used to move the player to another player, to a safe point after a respawn, and to place another player's map marker.
 */
namespace CellLookup
{
/**
 * Find the cell of a place, loading the outdoor cell there when this game has not loaded it.
 *
 * @param aWorld The client's world, for the plugins' ids.
 * @param acCellId The cell, as the server names it: an interior, or an outdoor cell this game may not have loaded.
 * @param acWorldSpaceId The world space, as the server names it; empty for an interior.
 * @param acPosition Where in the cell, in game units: it picks the outdoor cell to load.
 * @return The cell, or null when this game has neither the cell nor its world space.
 */
TESObjectCELL* FindOrLoad(World& aWorld, const GameId& acCellId, const GameId& acWorldSpaceId, const glm::vec3& acPosition) noexcept;
} // namespace CellLookup
