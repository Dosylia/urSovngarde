// Whether a copy already holds its owner's items (client/InventoryCompare.h), so setting them again can be skipped.

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <InventoryCompare.h>

namespace
{
Inventory::Entry Item(uint32_t aBaseId, int32_t aCount, bool aWorn = false)
{
    Inventory::Entry entry{};
    entry.BaseId = GameId{0, aBaseId};
    entry.Count = aCount;
    entry.ExtraWorn = aWorn;
    return entry;
}

Inventory Holding(std::initializer_list<Inventory::Entry> aEntries)
{
    Inventory inventory{};
    for (const auto& entry : aEntries)
        inventory.Entries.push_back(entry);
    return inventory;
}
} // namespace

TEST_CASE("Two games listing the same items in another order hold the same", "[inventorycompare]")
{
    const Inventory cOwner = Holding({Item(0x12EB7, 1, true), Item(0xF, 25), Item(0x1397E, 2)});
    const Inventory cCopy = Holding({Item(0x1397E, 2), Item(0x12EB7, 1, true), Item(0xF, 25)});
    CHECK(HoldsSameItems(cOwner, cCopy));

    // An entry with no count is skipped when an inventory is set, so it does not make them differ.
    const Inventory cWithEmpty = Holding({Item(0x1397E, 2), Item(0xA, 0), Item(0x12EB7, 1, true), Item(0xF, 25)});
    CHECK(HoldsSameItems(cOwner, cWithEmpty));
}

TEST_CASE("Any difference in the items means the inventory is set", "[inventorycompare]")
{
    const Inventory cOwner = Holding({Item(0x12EB7, 1, true), Item(0xF, 25)});

    // Another count, the item not worn, a missing item, an extra item.
    CHECK_FALSE(HoldsSameItems(cOwner, Holding({Item(0x12EB7, 1, true), Item(0xF, 24)})));
    CHECK_FALSE(HoldsSameItems(cOwner, Holding({Item(0x12EB7, 1, false), Item(0xF, 25)})));
    CHECK_FALSE(HoldsSameItems(cOwner, Holding({Item(0x12EB7, 1, true)})));
    CHECK_FALSE(HoldsSameItems(cOwner, Holding({Item(0x12EB7, 1, true), Item(0xF, 25), Item(0x1397E, 1)})));

    // The same item from another plugin.
    Inventory other = cOwner;
    other.Entries[1].BaseId.ModId = 3;
    CHECK_FALSE(HoldsSameItems(cOwner, other));

    // The same item poisoned on one side only.
    Inventory poisoned = cOwner;
    poisoned.Entries[0].ExtraPoisonId = GameId{0, 0x3A5A4};
    poisoned.Entries[0].ExtraPoisonCount = 2;
    CHECK_FALSE(HoldsSameItems(cOwner, poisoned));
}
