#pragma once

#include <atomic>
#include <cstdint>

//! The last few actors this client deleted, so a crash dump can be matched against them by address.
//!
//! Every remote copy deletion records the actor's pointer here, and the crash handler prints the ring.
//! If a crashing register holds one of these addresses, the crash is a use-after-free of an actor we deleted.
//!
//! A fixed array and one atomic counter, with no allocation and no lock.
//! It is written from actor threads and read from a crashing one, where a torn entry is harmless and a lock is not.
namespace RecentDeletes
{
struct Entry
{
    uint64_t Address{};
    uint32_t FormId{};
    uint32_t Handles{};   // outstanding BSPointerHandles: who else still has a claim on this actor
    uint32_t RefCount{};  // the whole refCount word, handles are its low 10 bits
    uint32_t Flags{};     // see Claim below
    uint64_t Stamp{};     // steady_clock ticks
};

//! What still had a hold on an actor at the moment it was deleted.
//! Recorded at the moment of the delete, because afterwards the evidence is gone.
enum Claim : uint32_t
{
    kNone = 0,
    kPlayerCombatTarget = 1 << 0, //!< the player is fighting it
    kOtherCombatTarget = 1 << 1,  //!< some other actor is fighting it
    kHasProcess = 1 << 2,         //!< still in the AI process lists
    kHas3D = 1 << 3,              //!< still has a loaded model, so "unloaded" was wrong about it
    kDeadOrDying = 1 << 4,
    kIsPlayerTeammate = 1 << 5,

    // Which path deleted it, for every path other than the unload one.
    kOnServersWord = 1 << 6, //!< the server removed the character
    kOnConnect = 1 << 7,     //!< the clean-up of temporary actors when a connection starts
    kOnDisconnect = 1 << 8,  //!< another player's copy, when the connection ended
    kLeftToGame = 1 << 9,    //!< NOT deleted here: a temporary actor the game made itself, which this client let go of
};

constexpr size_t kCount = 32;

//! Note that an actor is about to be deleted, and what still had a claim on it.
void Record(const void* apActor, uint32_t aFormId, uint32_t aHandles, uint32_t aRefCount, uint32_t aFlags) noexcept;

//! Write the ring to the log, newest first, flagging any entry that appears in the crashing registers.
//! Every register is checked, because a dead pointer can sit in any of them, not only Rcx.
void Report(const uint64_t* apRegisters, size_t aRegisterCount) noexcept;
} // namespace RecentDeletes
