#pragma once

#include <cstddef>
#include <cstdint>

//! When a remote copy we have been told to remove may actually be freed.
//!
//! It is a pure function of numbers with no game types, so it can be tested (see CopyRemovalPolicy tests).
//!
//!   - Deleting immediately is a use-after-free: the game still holds handles and makes a virtual call on the copy later.
//!   - Waiting for zero handles never frees anything: the game's own lists keep their handles while the cell is loaded.
//!
//! So the wait is bounded: at most kMaxWaiting copies are held back.
//! When one more arrives at that limit, the oldest is freed regardless of its handles.
//! The worst case is an immediate delete, and the usual case frees a copy once the game lets go of it.
//! Held-back copies are disabled while they wait, so they cost a list slot rather than sound, AI and draw calls.
namespace CopyRemovalPolicy
{
//! Never hold back more than this many copies.
//! A player leaving can remove over a hundred copies at once, and each one forced out at the limit is freed while held.
//! A waiting copy is disabled and costs a list slot; kMaxWaitMs is what keeps the list from growing without end.
constexpr size_t kMaxWaiting = 256;

//! Give up waiting after this long and free the copy anyway.
//! A holder that has not let go in a minute will not let go while the cell is loaded.
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
//! Only HandlesGone is what the wait is for; the other two free a copy that is still held, and the log must say so.
enum class Released
{
    NotYet,      //!< still held, keep waiting
    HandlesGone, //!< what the wait is for: nothing claims it any more, so freeing it is safe
    WaitedOut,   //!< gave up after kMaxWaitMs with handles still outstanding -- freed while held
    ListFull,    //!< the bound forced it out early, also freed while held
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
        return Released::WaitedOut;

    return Released::NotYet;
}

inline bool ShouldRelease(const uint32_t aHandles, const uint32_t aWaitedMs, const bool aOverBound) noexcept
{
    return ReleaseReason(aHandles, aWaitedMs, aOverBound) != Released::NotYet;
}

//! Whether freeing on these grounds leaves something holding a dangling pointer.
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
        return "it waited out the limit and was freed while still held";
    case Released::ListFull:
        return "the waiting list was full";
    default:
        return "still waiting";
    }
}
} // namespace CopyRemovalPolicy
