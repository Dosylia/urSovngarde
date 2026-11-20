#include <TiltedOnlinePCH.h>

#include <Services/CellLookup.h>

#include <Systems/ModSystem.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

#include <Structs/GridCellCoords.h>

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
TESObjectCELL* FindOrLoad(World& aWorld, const GameId& acCellId, const GameId& acWorldSpaceId, const glm::vec3& acPosition) noexcept
{
    auto& modSystem = aWorld.GetModSystem();
    if (TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(modSystem.GetGameId(acCellId))))
        return pCell;

    // An outdoor cell this game has not loaded: loaded from its world space, at the grid square the position falls in.
    if (!acWorldSpaceId)
        return nullptr;
    TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(modSystem.GetGameId(acWorldSpaceId)));
    if (!pWorldSpace)
        return nullptr;
    const GridCellCoords cGrid = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return pWorldSpace->LoadCell(cGrid.X, cGrid.Y);
}
} // namespace CellLookup
