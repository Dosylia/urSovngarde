#pragma once

#include <Structs/Inventory.h>

struct World;
struct Actor;
struct TransportService;

/**
 * Reads again what a local NPC wears after its equipment changes are sent, and corrects the other games' copies when it differs.
 * Its owner's game sends each equip and unequip its hooks see, but the game also equips through paths they do not see.
 * After an unequip the hooks saw, a quest put a shield back on with none: the other game kept the unequip.
 * The correction is an equipment snapshot, and a snapshot corrects only the copy's hands, spells and shield; other armor stays as the copy has it.
 */
namespace NpcEquipmentCheck
{
/**
 * Check whether two equipment lists hold the same items, worn in the same hands, with the same spells.
 * Charges and other extra data are not compared.
 *
 * @param acLhs One equipment list.
 * @param acRhs The other equipment list.
 * @return True when both list the same items in the same order, worn the same way, with the same magic equipment.
 */
bool HasSameWornItems(const Inventory& acLhs, const Inventory& acRhs) noexcept;

/**
 * Check whether an equipment list has an item worn, in either hand or as armor.
 *
 * @param acInventory The equipment list.
 * @param acItemId The item, as the server names it.
 * @return True when the list has that item worn.
 */
bool IsWorn(const Inventory& acInventory, const GameId& acItemId) noexcept;

/**
 * Queue two checks of what a local NPC wears after one of its equipment changes is sent: 1 s and 3 s later.
 * A difference from what was sent goes out as an equipment snapshot.
 * A check already waiting for that NPC is merged into the new one, so a change the game undid at once is not lost (NpcEquipmentQueue::Queue).
 *
 * @param aFormId The NPC's form id in this game.
 * @param aServerId The NPC's id on the server.
 * @param aOwnershipEpoch The ownership epoch the change was sent with.
 * @param acSent The equipment sent with the change.
 * @param aUndone True when the change was already undone when it was sent, so the first check sends what the NPC wears whatever it finds.
 */
void Note(uint32_t aFormId, uint32_t aServerId, uint32_t aOwnershipEpoch, const Inventory& acSent, bool aUndone) noexcept;

/**
 * Run the checks that are due, and send an equipment snapshot for each NPC that wears other items than were sent or had a change undone.
 * A check is dropped when its NPC is no longer this game's at the same epoch, and all are dropped while not connected.
 * Called every frame.
 *
 * @param aWorld The world, for the NPCs this game owns.
 * @param aTransport The connection the snapshots are sent through.
 */
void Run(World& aWorld, TransportService& aTransport) noexcept;

/**
 * Make an NPC's copy wear the shield its owner wears, or none, from an equipment snapshot.
 * A shield the copy lacks is added to its inventory first; when that fails, the copy keeps its own.
 *
 * @param apActor The copy.
 * @param acOwnerEquipment The equipment its owner's game sent.
 */
void ApplyShield(Actor* apActor, const Inventory& acOwnerEquipment) noexcept;
} // namespace NpcEquipmentCheck
