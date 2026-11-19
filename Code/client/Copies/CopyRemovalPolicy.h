#pragma once

#include <cstddef>
#include <cstdint>

//! When a remote copy we have been told to remove may actually be freed.
//!
//! It is a pure function of numbers with no game types, so it can be tested (see CopyRemovalPolicy tests).
//!
//!   - Delete() destroys a copy at once, and the game's jobs keep raw pointers to it for a moment after it goes.
//!     Deleting immediately is then a use-after-free, and no count shows those pointers, so a removed copy is given time.
//!   - The count read here is the object's strong references, called handles in the logs.
//!     Every reference in a cell has kOwnHolds of them, which Delete() drops itself, so it never reaches zero while the copy lives.
//!     A copy with more outlives Delete() until the others let go.
//!
//! So the wait is bounded: at most kMaxWaiting copies are held back.
//! A copy that arrives at that limit is freed at once, regardless of its handles.
//! The worst case is an immediate delete, and the usual case frees a copy after kMaxWaitMs.
//! Held-back copies are disabled while they wait, so they cost a list slot rather than sound, AI and draw calls.
namespace CopyRemovalPolicy
{
//! The strong references every reference in a cell has: the handle table's entry and the cell's list.
constexpr uint32_t kOwnHolds = 2;

//! Never hold back more than this many copies.
//! A player leaving can remove over a hundred copies at once, and each one forced out at the limit is freed while held.
//! A waiting copy is disabled and costs a list slot; kMaxWaitMs is what keeps the list from growing without end.
constexpr size_t kMaxWaiting = 256;

//! How long a disabled copy waits before it is deleted.
//! Long enough for the game's jobs to have finished with it: no crash has come from a copy this old.
constexpr uint32_t kMaxWaitMs = 60000;

enum class Action
{
    DeleteNow,    //!< free it here: either nothing holds it, or the waiting list is full
    WaitDisabled, //!< disable it and try again when its handles clear
};

//! What to do with a copy the moment we are told to remove it.
//! @param aHandles Outstanding BSPointerHandles: how many things still have a claim on the actor.
//! @param aWaitingCount How many copies are already being held back.
inline Action OnRemoved(const uint32_t aHandles, const size_t aWaitingCount) noexcept
{
    if (aHandles == 0)
        return Action::DeleteNow;

    if (aWaitingCount >= kMaxWaiting)
        return Action::DeleteNow;

    return Action::WaitDisabled;
}

//! Why a waiting copy was freed.
//! Only HandlesGone is what the wait is for; the other two delete a copy something else still holds, and the log must say so.
enum class Released
{
    NotYet,      //!< still waiting
    HandlesGone, //!< what the wait is for: the time has passed and only the game's own holds are left
    WaitedOut,   //!< the time has passed and something else holds it too; the object lives on until that lets go
    ListFull,    //!< the bound forced it out early
};

//! Whether a copy that is already waiting should be freed now, and on what grounds.
//! @param aHandles Its outstanding handles as of this check.
//! @param aWaitedMs How long it has been waiting.
//! @param aOverBound True when the list is over its limit and this is among the oldest that must go.
inline Released ReleaseReason(const uint32_t aHandles, const uint32_t aWaitedMs, const bool aOverBound) noexcept
{
    if (aHandles == 0)
        return Released::HandlesGone;

    if (aOverBound)
        return Released::ListFull;

    if (aWaitedMs >= kMaxWaitMs)
        return aHandles <= kOwnHolds ? Released::HandlesGone : Released::WaitedOut;

    return Released::NotYet;
}

inline bool ShouldRelease(const uint32_t aHandles, const uint32_t aWaitedMs, const bool aOverBound) noexcept
{
    return ReleaseReason(aHandles, aWaitedMs, aOverBound) != Released::NotYet;
}

//! Whether something besides the game's own lists still held the copy when it was deleted.
inline bool FreedWhileHeld(const Released aReason) noexcept
{
    return aReason == Released::WaitedOut || aReason == Released::ListFull;
}

//! What the log should say about it, in the words the session report counts.
inline const char* Describe(const Released aReason) noexcept
{
    switch (aReason)
    {
    case Released::HandlesGone:
        return "the game let go of it";
    case Released::WaitedOut:
        return "it waited out the limit and something else still holds it";
    case Released::ListFull:
        return "the waiting list was full";
    default:
        return "still waiting";
    }
}
} // namespace CopyRemovalPolicy
