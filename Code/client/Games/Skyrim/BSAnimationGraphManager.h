#pragma once

#include <Games/Primitives.h>

#include <Havok/BShkbAnimationGraph.h>

#include <map>

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

    /**
     * List one graph's animation variables by number, and print them for the code's tables when asked.
     * The dump and the fingerprint must describe the same graph, or a signature is looked for in one graph and keyed to another.
     *
     * @param aPrintVariables True to print each variable as "kName = number," to the console.
     * @param aForceIndex Which graph, as for GetDescriptorKey, and the same one; -1 to ask the manager.
     * @return Each variable's name by its number in the graph; empty when the graph cannot be read.
     */
    SortedMap<uint32_t, String> DumpAnimationVariables(bool aPrintVariables, int aForceIndex = -1);

    /**
     * Fingerprint one graph by its animation variables' names, joined in number order and lower-cased.
     * Copies use it to tell which graph a set of animation variables belongs to, so it must not change.
     *
     * @param aForceIndex Which graph; -1 to ask the manager. DumpAnimationVariables must be given the same one.
     * @return The names' CRC-64; the CRC of nothing when the graph cannot be read.
     */
    uint64_t GetDescriptorKey(int aForceIndex = -1);
    /**
     * List one graph's animation variables by their number in the graph, as its database names them.
     *
     * @param aForceIndex Which graph, as for GetDescriptorKey; -1 to ask the manager.
     * @return Each variable's name by its number, in number order; empty when the graph or its tables cannot be read.
     */
    std::map<uint32_t, const char*> VariableNamesByNumber(int aForceIndex = -1);
    //! A hash of the behavior project's name ("DefaultMale"), 0 when unknown; the same graph choice as GetDescriptorKey.
    uint64_t GetProjectKey(int aForceIndex = -1);
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
