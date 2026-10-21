#pragma once

#include <Structs/Inventory.h>

#include <algorithm>
#include <tuple>

//! True when both inventories hold the same items, worn the same way, in whatever order.
//! Setting one over the other would then change nothing.
//! Entries with no count are left out, as setting an inventory skips them.
inline bool HoldsSameItems(const Inventory& acLhs, const Inventory& acRhs)
{
    const auto cSorted = [](const Inventory& acInventory)
    {
        Vector<Inventory::Entry> entries;
        entries.reserve(acInventory.Entries.size());
        for (const auto& entry : acInventory.Entries)
            if (entry.Count != 0)
                entries.push_back(entry);
        std::sort(entries.begin(), entries.end(),
                  [](const Inventory::Entry& acA, const Inventory::Entry& acB)
                  {
                      return std::tie(acA.BaseId.ModId, acA.BaseId.BaseId, acA.Count, acA.ExtraWorn, acA.ExtraWornLeft) <
                             std::tie(acB.BaseId.ModId, acB.BaseId.BaseId, acB.Count, acB.ExtraWorn, acB.ExtraWornLeft);
                  });
        return entries;
    };

    // Two entries alike in the sort key but not in their extra data can end up in another order on each side.
    // That reads as different, so the inventory is set as before: never a false "same".
    return cSorted(acLhs) == cSorted(acRhs);
}
