#pragma once

#include <Structs/Inventory.h>

struct World;
struct Actor;
struct TransportService;

// What an NPC wears, kept the same in every game.
// Its owner's game sends each equip and unequip its hooks see, but the game also equips through paths they do not see.
// After an unequip the hooks saw, a quest put a shield back on with none: the other game kept the unequip.
namespace NpcEquipmentCheck
{
// True when both list the same items worn in the same hands; charges and other extra data are not compared.
bool HasSameWornItems(const Inventory& acLhs, const Inventory& acRhs) noexcept;

// True when the inventory lists that item worn, in either hand or as armor.
bool IsWorn(const Inventory& acInventory, const GameId& acItemId) noexcept;

// After an equipment change of a local NPC is sent: what it wears is read again 1 s and 3 s later.
// A difference from what was sent goes out as an equipment snapshot.
// aUndone: the change was already undone when it was sent, so the first check sends what it wears whatever it finds.
void Note(uint32_t aFormId, uint32_t aServerId, uint32_t aOwnershipEpoch, const Inventory& acSent, bool aUndone) noexcept;

// Each frame: the checks that are due.
void Run(World& aWorld, TransportService& aTransport) noexcept;

// From an equipment snapshot: the shield its owner wears, worn by the copy too, or none.
void ApplyShield(Actor* apActor, const Inventory& acOwnerEquipment) noexcept;
} // namespace NpcEquipmentCheck
