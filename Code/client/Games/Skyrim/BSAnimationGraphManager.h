#pragma once

#include <Games/Primitives.h>

#include <Havok/BShkbAnimationGraph.h>

struct BSFixedString;

struct BSAnimationGraphManager
{
    virtual ~BSAnimationGraphManager();
    virtual void sub_1(void* apUnk1);

    void Release()
    {
        if (InterlockedDecrement(&refCount) == 0)
            this->~BSAnimationGraphManager();
    }

    volatile LONG refCount;
    void* pad_ptrs[6];
    BSTSmallArray<BShkbAnimationGraph> animationGraphs; // 40 - 20
    void* pad_ptrs2[9];
    BSRecursiveLock lock;  // 98 - 4C
    void* unkPtrAfterLock; // A0 - 58

#if TP_PLATFORM_32
    void* unkPtrOldrim;
#endif

    uint32_t animationGraphIndex; // A8 - 5C

    //! Which entry of animationGraphs to actually read.
    //!
    //! On VR the SE offset (0xB0) of `animationGraphIndex` holds something else, often far out of range.
    //! With a single graph in the list, that graph is the answer.
    //! @param aForceIndex A caller that already knows which graph it wants (the player always wants 0); -1 to ask.
    uint32_t ResolveGraphIndex(int aForceIndex = -1) const noexcept;

    //! @param aForceIndex As for GetDescriptorKey, and it must match it.
    //! The dump and the hash must describe the same graph, or a signature is looked for in one graph and keyed to another.
    SortedMap<uint32_t, String> DumpAnimationVariables(bool aPrintVariables, int aForceIndex = -1);
    uint64_t GetDescriptorKey(int aForceIndex = -1);
    uint32_t ReSendEvent(BSFixedString* apEventName);
};

#if TP_PLATFORM_64
static_assert(offsetof(BSAnimationGraphManager, animationGraphs) == 0x40);
static_assert(offsetof(BSAnimationGraphManager, lock) == 0xA0);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0xB0);
#else
static_assert(offsetof(BSAnimationGraphManager, animationGraphs) == 0x20);
static_assert(offsetof(BSAnimationGraphManager, lock) == 0x4C);
static_assert(offsetof(BSAnimationGraphManager, animationGraphIndex) == 0x5C);
#endif
