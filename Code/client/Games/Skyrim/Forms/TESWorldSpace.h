#pragma once

#include <Forms/TESForm.h>

#include <Components/TESFullName.h>

struct TESWorldSpace : TESForm
{
    // aX and aY are coordinates, not positions
    TESObjectCELL* LoadCell(int32_t aXCoordinate, int32_t aYCoordinate) noexcept;

    // Where the game keeps the persistent references that stand outside the loaded cells (CommonLibVR persistentCell, 0x88).
    // Not checked against the game's own use: callers check that it is a cell of this world space.
    TESObjectCELL* GetPersistentCell() const noexcept { return *reinterpret_cast<TESObjectCELL* const*>(reinterpret_cast<const uint8_t*>(this) + 0x88); }

    TESFullName fullName;
};
