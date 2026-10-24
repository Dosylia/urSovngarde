#pragma once
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

struct Player;

/**
 * @brief Dispatched when a player enters a new exterior cell.
 */
struct CharacterExteriorCellChangeEvent
{
    CharacterExteriorCellChangeEvent(Player* apOwner, const entt::entity aEntity, const GameId aCellId, const GameId aWorldSpaceId, const GridCellCoords aCurrentCoords)
        : Owner(apOwner)
        , Entity{aEntity}
        , CellId(aCellId)
        , WorldSpaceId(aWorldSpaceId)
        , CurrentCoords(aCurrentCoords)
    {
    }

    Player* Owner;
    entt::entity Entity;
    GameId CellId;
    GameId WorldSpaceId;
    GridCellCoords CurrentCoords;
};
