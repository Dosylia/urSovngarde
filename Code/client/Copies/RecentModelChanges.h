#pragma once

#include <cstddef>
#include <cstdint>

struct Actor;

//! TEMPORARY: the last changes this client made to an actor's 3D, so a crash in the game's code on an actor's freed 3D can be matched against them by address.
//! Nothing is logged in play: the crash block prints the ring, checked against the crashing registers and the crashing thread's stack.
//!
//! A fixed array and one atomic counter, with no allocation and no lock, as RecentDeletes.
//! It is written from game threads and read from a crashing one, where a torn entry is harmless and a lock is not.
namespace RecentModelChanges
{
enum class Kind : uint8_t
{
    ItemsChanged, //!< its items set again, reset, or one added, removed or dropped
    Equip,        //!< an item, spell or shout equipped (EquipManager)
    Unequip,      //!< one unequipped (EquipManager)
    WeaponDrawn,  //!< its weapon drawn or sheathed
    Disable,
    Enable,
    Moved, //!< MoveTo with its 3D loaded, which drops the 3D for a frame
    Killed,
    Revived,
    Rebuilt, //!< its 3D queued for a rebuild, or its base swapped for a leveled pick
    Deleted,
};

constexpr size_t kCount = 64;

//! Notes a change this client made to an actor's 3D.
//! A repeat of the newest entry, same actor, form id and kind, only refreshes its time, so one item reset does not fill the ring.
void Note(const Actor* apActor, Kind aKind) noexcept;

//! Writes the newest entries to the log, and any older one found, each checked against the crashing registers and stack words.
void Report(const uint64_t* apRegisters, size_t aRegisterCount, const uint64_t* apStack, size_t aStackWords) noexcept;
} // namespace RecentModelChanges
