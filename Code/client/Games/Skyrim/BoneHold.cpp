#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <CrashHandler.h>
#include <Games/Skyrim/BoneHold.h>
#include <Games/Skyrim/GameStack.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Games/Memory.h>
#include <Games/TES.h>
#include <Games/References.h>
#include <PlayerCharacter.h>
#include <BSAnimationGraphManager.h>

#include <intrin.h>
#include <thread>
#include <unordered_set>
#include <TlHelp32.h>

// A skeleton rebuilt for character creation or a race change leaves bones the game still writes to: the player's animation kept WEAPON and SHIELD on the freed nodes and wrote their pose there each frame.
// The destructor of the plain node class is hooked, and every freed named node is held a while before its memory goes back, so such a write lands in memory nothing else uses.
// Each frame the animation graphs' bone tables are checked, and an entry on a held node is moved to the node of the same name (RepairGraphsOf).
// A held node's transforms are compared as it is held, and a held node gets a class table that only records its callers, so whatever still reaches it is told.
// The first held node found written also gets a hardware write watch, to name the code that writes it.
namespace
{
constexpr uint32_t kRefCountOffset = 0x8;   // NiRefObject::refCount
constexpr uint32_t kNameOffset = 0x10;      // NiObjectNET::name
constexpr uint32_t kParentOffset = 0x30;    // NiAVObject::parent
constexpr uint32_t kLocalOffset = 0x48;     // NiAVObject::local, then world at 0x7C
constexpr uint32_t kTransformsBytes = 0x68; // local and world, 0x34 each
constexpr uint32_t kChildrenOffset = 0x138; // NiNode::children, on VR
constexpr uint16_t kMaxChildren = 512;
constexpr size_t kToldInFull = 20;          // then one in every kToldEvery
constexpr size_t kToldEvery = 100;
constexpr size_t kHeldMax = 8192;
constexpr auto kHoldFor = std::chrono::milliseconds(1500);
// The player's own bones are held far longer: a late write came 35 s after a race change.
constexpr size_t kPlayerHeldMax = 4096;
constexpr auto kPlayerHoldFor = std::chrono::seconds(120);
constexpr uint32_t kFirstPersonHandOffset = 0x598; // PlayerCharacter's first-person right hand, as VRBodySync reads it
constexpr size_t kMaxPlayerNodes = 4096;

using TDestructor = void*(void*, uint32_t);
std::atomic<TDestructor*> s_pReal{nullptr};
std::atomic<void**> s_pVTable{nullptr};
std::atomic<BoneHold::DestroyObserver> s_observer{nullptr};

// A freed node kept until its hold has passed or its list is full.
struct Held
{
    uint8_t* pMemory;
    char Name[48];
    std::chrono::steady_clock::time_point FreedAt;
    uint64_t Frame;
    uint32_t Writes;
    uint8_t Transforms[kTransformsBytes];
};

// Oldest first; the oldest goes when its hold has passed, or at once when the list is full.
struct HeldList
{
    Held* pItems;
    size_t Max;
    std::chrono::milliseconds HoldFor;
    size_t First = 0;
    size_t Count = 0;

    Held& At(size_t aIndex) noexcept { return pItems[(First + aIndex) % Max]; }
};

// Something that reached a held node: a write to its transforms, or a call through its class table.
struct Late
{
    char Name[48];
    void* pNode;
    bool Player;
    bool Write;
    bool LocalChanged;
    bool WorldChanged;
    float Local[12];
    uint32_t AfterMs;
    uint32_t Writes;
    void* pCaller;
    uint32_t Thread;
    uint64_t Frame;
};

std::mutex s_heldLock;
Held s_heldItems[kHeldMax]{};
Held s_playerHeldItems[kPlayerHeldMax]{};
HeldList s_held{s_heldItems, kHeldMax, kHoldFor};
HeldList s_playerHeld{s_playerHeldItems, kPlayerHeldMax, std::chrono::duration_cast<std::chrono::milliseconds>(kPlayerHoldFor)};
std::unordered_set<const void*> s_playerNodes;
uint64_t s_heldTotal = 0;
uint64_t s_playerHeldTotal = 0;
Late s_late[32]{};
size_t s_lateCount = 0;
void* s_deadVTable[256]{};

// The first freed bone of the player's found written gets a hardware write breakpoint on every thread, to name the code that writes it.
// It stays a few seconds or three hits, well within the bone's hold, and is never set again in the session.
constexpr auto kWatchFor = std::chrono::seconds(20);
constexpr uint32_t kWatchHitsMax = 3;
struct WatchHit
{
    uint64_t Rip;
    uint64_t R14; // the bone table being walked, when the writer is the animation's pose-to-node loop (SkyrimVR.exe+0xB3C260)
    uint32_t Thread;
    uint64_t Stack[24];
};

// An animation graph's bone table: the node each animated bone writes its pose to (BShkbAnimationGraph, CommonLibVR).
constexpr uint32_t kGraphBoneNodesOffset = 0x160; // BSTArray of {NiNode*, int32 index}: data, then capacity, then size at +0x10
constexpr uint32_t kGraphHolderOffset = 0x210;
constexpr uint32_t kGraphRootOffset = 0x218;
constexpr uint32_t kBoneNodeEntrySize = 0x10;
constexpr uint32_t kFormIdOffset = 0x14;
size_t s_graphRepairs = 0;
std::atomic<uint64_t> s_watchAddress{0};
char s_watchName[48]{};
std::atomic<bool> s_watchWanted{false};
std::atomic<bool> s_watchUsed{false};
std::atomic<uint32_t> s_watchHitCount{0};
WatchHit s_watchHits[kWatchHitsMax]{};
std::chrono::steady_clock::time_point s_watchUntil{};
bool s_watchArmed = false;

// A read that answers false instead of faulting.
// The crash handler is told the fault is expected, or it would report a crash first.
template <class T> bool SafeRead(const void* apAddress, T& aOut) noexcept
{
    bool read = false;
    CrashGuard::Enter();
    __try
    {
        aOut = *static_cast<const T*>(apAddress);
        read = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    CrashGuard::Leave();
    return read;
}

std::string TextAt(const char* apText) noexcept
{
    char text[64]{};
    for (size_t i = 0; apText && i + 1 < sizeof(text) && GameStack::Readable(apText + i, 1) && apText[i]; ++i)
        text[i] = apText[i];
    return text;
}

void CopyName(const char* apText, char (&aOut)[48]) noexcept
{
    size_t i = 0;
    for (; apText && i + 1 < sizeof(aOut) && apText[i]; ++i)
        aOut[i] = apText[i];
    aOut[i] = 0;
}

// Called with s_heldLock held.
void NoteLate(const Late& acLate) noexcept
{
    s_late[s_lateCount++ % std::size(s_late)] = acLate;
}

// Called with s_heldLock held: tells a write to the held node since it was last looked at, and takes its transforms as they are now.
void LookAt(Held& aHeld, bool aPlayer) noexcept
{
    const uint8_t* pNow = aHeld.pMemory + kLocalOffset;
    const bool cLocal = std::memcmp(pNow, aHeld.Transforms, 0x34) != 0;
    const bool cWorld = std::memcmp(pNow + 0x34, aHeld.Transforms + 0x34, 0x34) != 0;
    if (!cLocal && !cWorld)
        return;
    ++aHeld.Writes;
    std::memcpy(aHeld.Transforms, pNow, kTransformsBytes);
    // The first write to a node is told; later ones only count, so a writer at every frame cannot flood the list.
    if (aHeld.Writes > 1)
        return;
    // The first freed node found written gets the hardware watch, set from the main loop; it is then held long (PopOldest).
    if (!s_watchUsed.exchange(true))
    {
        std::memcpy(s_watchName, aHeld.Name, sizeof(s_watchName));
        s_watchAddress.store(reinterpret_cast<uint64_t>(aHeld.pMemory + kLocalOffset));
        s_watchWanted.store(true);
    }
    Late late{};
    std::memcpy(late.Name, aHeld.Name, sizeof(late.Name));
    late.pNode = aHeld.pMemory;
    late.Player = aPlayer;
    late.Write = true;
    late.LocalChanged = cLocal;
    late.WorldChanged = cWorld;
    std::memcpy(late.Local, pNow, sizeof(late.Local));
    late.AfterMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - aHeld.FreedAt).count());
    late.Frame = MainLoopFrame();
    NoteLate(late);
}

// Called with s_heldLock held: the oldest node goes, its memory given back to be freed, or null when it is kept.
// A node written after its free moves to the long hold, so the watch on it stays on memory nothing else uses.
uint8_t* PopOldest(HeldList& aList, bool aPlayer) noexcept
{
    Held& oldest = aList.At(0);
    LookAt(oldest, aPlayer);
    if (!aPlayer && oldest.Writes > 0 && s_playerHeld.Count < s_playerHeld.Max)
    {
        s_playerHeld.At(s_playerHeld.Count) = oldest;
        ++s_playerHeld.Count;
        aList.First = (aList.First + 1) % aList.Max;
        --aList.Count;
        return nullptr;
    }
    if (oldest.Writes > 1)
    {
        Late late{};
        std::memcpy(late.Name, oldest.Name, sizeof(late.Name));
        late.pNode = oldest.pMemory;
        late.Player = aPlayer;
        late.Write = true;
        late.Writes = oldest.Writes;
        late.AfterMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - oldest.FreedAt).count());
        late.Frame = MainLoopFrame();
        NoteLate(late);
    }
    aList.First = (aList.First + 1) % aList.Max;
    --aList.Count;
    return oldest.pMemory;
}

// Every slot of a held node's class table: records who called, does nothing, answers null.
void* DeadNodeCall(void* apThis)
{
    void* pCaller = _ReturnAddress();
    std::scoped_lock lock(s_heldLock);
    Late late{};
    late.pNode = apThis;
    late.pCaller = pCaller;
    late.Thread = GetCurrentThreadId();
    late.Frame = MainLoopFrame();
    for (HeldList* pList : {&s_playerHeld, &s_held})
    {
        for (size_t i = 0; i < pList->Count; ++i)
        {
            const Held& cHeld = pList->At(i);
            if (cHeld.pMemory != apThis)
                continue;
            std::memcpy(late.Name, cHeld.Name, sizeof(late.Name));
            late.Player = pList == &s_playerHeld;
            late.AfterMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cHeld.FreedAt).count());
            break;
        }
    }
    NoteLate(late);
    return nullptr;
}

void* HookDestructor(void* apThis, uint32_t aFlags)
{
    TDestructor* pReal = s_pReal.load(std::memory_order_acquire);
    // Only set before the hook goes in, so this does not happen; the object is then left alive rather than called through nothing.
    if (!pReal)
        return apThis;

    auto* pBytes = static_cast<uint8_t*>(apThis);
    if (const auto observer = s_observer.load(std::memory_order_acquire))
        observer(apThis);

    // Only a named node that is being freed is held; an unnamed one is no bone.
    const char* pName = *reinterpret_cast<const char* const*>(pBytes + kNameOffset);
    if (!(aFlags & 1) || !pName || !pName[0])
        return pReal(apThis, aFlags);

    Held held{};
    held.pMemory = pBytes;
    CopyName(pName, held.Name);
    // Destroyed, its memory kept.
    pReal(apThis, aFlags & ~1u);
    *reinterpret_cast<void**>(pBytes) = s_deadVTable;
    held.FreedAt = std::chrono::steady_clock::now();
    held.Frame = MainLoopFrame();
    std::memcpy(held.Transforms, pBytes + kLocalOffset, kTransformsBytes);

    uint8_t* pToFree = nullptr;
    {
        std::scoped_lock lock(s_heldLock);
        const bool cPlayer = s_playerNodes.contains(apThis);
        HeldList& list = cPlayer ? s_playerHeld : s_held;
        if (list.Count == list.Max)
            pToFree = PopOldest(list, cPlayer);
        list.At(list.Count) = held;
        ++list.Count;
        ++(cPlayer ? s_playerHeldTotal : s_heldTotal);
    }
    if (pToFree)
        Memory::Free(pToFree);
    return apThis;
}

// Gives back the memory of the nodes held long enough, and looks at the player's held bones for a write.
void ReleaseOldHeld() noexcept
{
    uint8_t* toFree[256];
    size_t count = 0;
    const auto cNow = std::chrono::steady_clock::now();
    static auto s_nextLook = cNow;
    {
        std::scoped_lock lock(s_heldLock);
        for (HeldList* pList : {&s_held, &s_playerHeld})
            while (pList->Count && count < std::size(toFree) && cNow - pList->At(0).FreedAt >= pList->HoldFor)
                if (uint8_t* pMemory = PopOldest(*pList, pList == &s_playerHeld))
                    toFree[count++] = pMemory;
        // Once a second, so a write to one of the player's bones is told within a second rather than when it goes.
        if (cNow >= s_nextLook)
        {
            s_nextLook = cNow + std::chrono::seconds(1);
            for (size_t i = 0; i < s_playerHeld.Count; ++i)
                LookAt(s_playerHeld.At(i), true);
        }
    }
    for (size_t i = 0; i < count; ++i)
        Memory::Free(toFree[i]);
}

void CopyStack(const uint64_t* apStack, uint64_t (&aOut)[24]) noexcept
{
    __try
    {
        for (size_t i = 0; i < std::size(aOut); ++i)
            aOut[i] = apStack[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// The write to the watched bone stops on the instruction after it: where it was and the stack above it are kept, and the game goes on.
LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* apInfo)
{
    if (apInfo->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* pContext = apInfo->ContextRecord;
    if (!(pContext->Dr6 & 1) || s_watchAddress.load() == 0)
        return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t cIndex = s_watchHitCount.fetch_add(1);
    if (cIndex < kWatchHitsMax)
    {
        WatchHit& hit = s_watchHits[cIndex];
        hit.Rip = pContext->Rip;
        hit.R14 = pContext->R14;
        hit.Thread = GetCurrentThreadId();
        CopyStack(reinterpret_cast<const uint64_t*>(pContext->Rsp), hit.Stack);
    }
    pContext->Dr6 = 0;
    // This thread stops watching once enough hits are kept; the main loop takes it off the others.
    if (cIndex + 1 >= kWatchHitsMax)
        pContext->Dr7 &= ~0xF0003ull;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets or clears the watch in every thread's debug registers, one thread suspended at a time.
// Called from a thread of its own, which can then reach the main thread as well.
void SetWatchOnAllThreads(uint64_t aAddress, bool aOn) noexcept
{
    std::vector<DWORD> ids;
    if (HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL found = Thread32First(snapshot, &entry); found; found = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == GetCurrentProcessId())
                ids.push_back(entry.th32ThreadID);
        CloseHandle(snapshot);
    }
    const DWORD cSelf = GetCurrentThreadId();
    for (const DWORD cId : ids)
    {
        if (cId == cSelf)
            continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, cId);
        if (!thread)
            continue;
        if (SuspendThread(thread) != static_cast<DWORD>(-1))
        {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context))
            {
                // Dr0, enabled locally, on writes, 8 bytes.
                context.Dr7 &= ~0xF0003ull;
                if (aOn)
                {
                    context.Dr0 = aAddress;
                    context.Dr7 |= 0x1ull | (0x1ull << 16) | (0x3ull << 18);
                }
                context.Dr6 = 0;
                SetThreadContext(thread, &context);
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
}

void CollectNodes(void* apRoot, std::unordered_set<const void*>& aOut) noexcept;

// The name a held node had, by its address; empty when it is not held.
std::string HeldName(const void* apNode) noexcept
{
    std::scoped_lock lock(s_heldLock);
    for (HeldList* pList : {&s_playerHeld, &s_held})
        for (size_t i = 0; i < pList->Count; ++i)
            if (pList->At(i).pMemory == apNode)
                return pList->At(i).Name;
    return {};
}

void* FindByName(void* apRoot, const std::string& acName) noexcept
{
    using TGetObjectByName = void*(void*, const char* const*, char);
    POINTER_SKYRIMSE(TGetObjectByName, GetObjectByName, 76207, 76207);
    if (!apRoot || acName.empty())
        return nullptr;
    const BSFixedString cName(acName.c_str());
    return GetObjectByName(apRoot, &cName.data, 1);
}

// Tells whose graph a bone table belongs to, and each entry that points at a freed node, with what its 3D has under that name.
void DescribeGraph(uint8_t* apGraph) noexcept
{
    void* pHolder = nullptr;
    void* pRoot = nullptr;
    uint8_t* pData = nullptr;
    uint32_t size = 0;
    uint32_t formId = 0;
    if (!SafeRead(apGraph + kGraphHolderOffset, pHolder) || !SafeRead(apGraph + kGraphRootOffset, pRoot) || !SafeRead(apGraph + kGraphBoneNodesOffset, pData) ||
        !SafeRead(apGraph + kGraphBoneNodesOffset + 0x10, size))
    {
        spdlog::error("NodeQuarantine: the writer's bone table at {} could not be read", fmt::ptr(apGraph + kGraphBoneNodesOffset));
        return;
    }
    if (pHolder)
        SafeRead(static_cast<uint8_t*>(pHolder) + kFormIdOffset, formId);
    const char* pRootName = nullptr;
    if (pRoot)
        SafeRead(static_cast<uint8_t*>(pRoot) + kNameOffset, pRootName);
    std::unordered_set<const void*> tree;
    CollectNodes(pRoot, tree);
    size_t stale = 0;
    spdlog::error("NodeQuarantine: the writer walks the bone table of the animation graph {} of actor {:X}, root {} ('{}', {} nodes below it), {} entries", fmt::ptr(apGraph),
                  formId, fmt::ptr(pRoot), TextAt(pRootName), tree.size(), size);
    for (uint32_t i = 0; i < size && i < 512 && pData; ++i)
    {
        void* pNode = nullptr;
        int32_t index = 0;
        SafeRead(pData + i * kBoneNodeEntrySize, pNode);
        SafeRead(pData + i * kBoneNodeEntrySize + 8, index);
        void* pVTable = nullptr;
        if (!pNode || (SafeRead(pNode, pVTable) && pVTable != s_deadVTable && tree.contains(pNode)))
            continue;
        ++stale;
        const std::string cName = HeldName(pNode);
        void* pNow = FindByName(pRoot, cName);
        spdlog::error("    entry {} (bone index {}) points at {} '{}', {}; its 3D has {}", i, index, fmt::ptr(pNode), cName.empty() ? "?" : cName,
                      pVTable == s_deadVTable ? "a freed node" : "a node outside its 3D", pNow ? fmt::format("'{}' at {}", cName, fmt::ptr(pNow)) : std::string("no node of that name"));
    }
    spdlog::error("NodeQuarantine: {} of the {} entries point at a freed node or one outside the graph's 3D", stale, size);
}

// An actor's animation graphs keep, for each animated bone, the node its pose is written to.
// A skeleton rebuilt for a race change or character creation can leave such an entry on the old, freed node: the pose is then written into freed memory each frame.
// Each entry on a held freed node is moved to the node of the same name in the graph's 3D, or emptied when there is none; the pose loop skips an empty one.
// From the main loop, between frames, under the graph manager's lock.
void RepairGraphsOf(TESObjectREFR* apRefr) noexcept
{
    BSAnimationGraphManager* pManager = nullptr;
    if (!apRefr || !apRefr->animationGraphHolder.GetBSAnimationGraph(&pManager) || !pManager)
        return;
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t g = 0; g < pManager->animationGraphs.size; ++g)
        {
            auto* pGraph = reinterpret_cast<uint8_t*>(const_cast<BShkbAnimationGraph*>(pManager->animationGraphs.Get(g)));
            if (!pGraph)
                continue;
            auto* pData = *reinterpret_cast<uint8_t**>(pGraph + kGraphBoneNodesOffset);
            const uint32_t cSize = *reinterpret_cast<uint32_t*>(pGraph + kGraphBoneNodesOffset + 0x10);
            void* pRoot = *reinterpret_cast<void**>(pGraph + kGraphRootOffset);
            for (uint32_t i = 0; pData && i < cSize && i < 1024; ++i)
            {
                void*& node = *reinterpret_cast<void**>(pData + i * kBoneNodeEntrySize);
                void* pVTable = nullptr;
                if (!node || !SafeRead(node, pVTable) || pVTable != s_deadVTable)
                    continue;
                const std::string cName = HeldName(node);
                void* pNow = FindByName(pRoot, cName);
                if (pNow && *static_cast<void**>(pNow) == s_deadVTable)
                    pNow = nullptr;
                ++s_graphRepairs;
                if (s_graphRepairs <= kToldInFull || s_graphRepairs % kToldEvery == 0)
                    spdlog::warn("GraphRebind: actor {:X}'s animation graph {} entry {} ('{}') pointed at a freed node; {} ({} so far)", apRefr->formID, g, i,
                                 cName.empty() ? "?" : cName, pNow ? fmt::format("moved to the node of that name in its 3D ({})", fmt::ptr(pNow)) : std::string("emptied, its 3D has no node of that name"),
                                 s_graphRepairs);
                node = pNow;
            }
        }
    }
    pManager->Release();
}

// The player at every frame, the loaded actors every tenth.
void RepairGraphs(uint64_t aFrame) noexcept
{
    if (PlayerCharacter* pPlayer = PlayerCharacter::Get())
        RepairGraphsOf(pPlayer);
    if (aFrame % 10 != 0)
        return;
    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return;
    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
        RepairGraphsOf(TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]));
}

// From the main loop: sets the watch once wanted, and takes it off after its time or its hits, telling what it caught.
void UpdateWatch() noexcept
{
    const uint64_t cAddress = s_watchAddress.load();
    if (s_watchWanted.exchange(false) && cAddress)
    {
        static bool s_handlerAdded = false;
        if (!s_handlerAdded)
            s_handlerAdded = AddVectoredExceptionHandler(1, &WatchHandler) != nullptr;
        std::thread([cAddress] { SetWatchOnAllThreads(cAddress, true); }).join();
        s_watchArmed = true;
        s_watchUntil = std::chrono::steady_clock::now() + kWatchFor;
        spdlog::error("NodeQuarantine: a hardware write watch is set on the player's freed bone '{}' ({:#x}) for {} s", s_watchName, cAddress, kWatchFor.count());
        return;
    }
    if (!s_watchArmed || (std::chrono::steady_clock::now() < s_watchUntil && s_watchHitCount.load() < kWatchHitsMax))
        return;

    std::thread([cAddress] { SetWatchOnAllThreads(cAddress, false); }).join();
    s_watchArmed = false;
    s_watchAddress.store(0);
    const uint32_t cHits = std::min(s_watchHitCount.load(), kWatchHitsMax);
    spdlog::error("NodeQuarantine: the watch on the player's freed bone '{}' caught {} write(s)", s_watchName, s_watchHitCount.load());
    for (uint32_t i = 0; i < cHits; ++i)
    {
        const WatchHit& cHit = s_watchHits[i];
        std::string stack;
        size_t shown = 0;
        for (const uint64_t cValue : cHit.Stack)
        {
            const std::string cWhere = cValue > 0x10000 ? GameStack::Where(reinterpret_cast<void*>(cValue)) : std::string();
            if (cWhere.find(".dll+") == std::string::npos && cWhere.find(".exe+") == std::string::npos)
                continue;
            stack += (shown++ ? ", " : "") + cWhere;
            if (shown == 8)
                break;
        }
        spdlog::error("    write {} came from {} on thread {} (main thread {}); code addresses on its stack: {}", i + 1, GameStack::Where(reinterpret_cast<void*>(cHit.Rip)),
                      cHit.Thread, GameMainThreadId(), stack.empty() ? "none" : stack);
    }
    // The animation's pose-to-node loop holds the bone table it walks in r14.
    if (cHits > 0 && s_watchHits[0].R14 > kGraphBoneNodesOffset)
        DescribeGraph(reinterpret_cast<uint8_t*>(s_watchHits[0].R14 - kGraphBoneNodesOffset));
}

// Every node below a root, for the set of the player's bones.
void CollectNodes(void* apRoot, std::unordered_set<const void*>& aOut) noexcept
{
    std::vector<void*> stack{apRoot};
    while (!stack.empty() && aOut.size() < kMaxPlayerNodes)
    {
        void* pNode = stack.back();
        stack.pop_back();
        if (!pNode || !aOut.insert(pNode).second)
            continue;
        void** pChildren = nullptr;
        uint16_t capacity = 0;
        if (!SafeRead(static_cast<uint8_t*>(pNode) + kChildrenOffset + 0x8, pChildren) || !SafeRead(static_cast<uint8_t*>(pNode) + kChildrenOffset + 0x10, capacity) ||
            !pChildren || capacity > kMaxChildren)
            continue;
        for (uint16_t i = 0; i < capacity; ++i)
        {
            void* pChild = nullptr;
            if (SafeRead(&pChildren[i], pChild) && pChild)
                stack.push_back(pChild);
        }
    }
}

// The player's third-person skeleton, and the first-person one found from its right hand up to its top.
void UpdatePlayerNodes() noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
    std::unordered_set<const void*> nodes;
    nodes.reserve(1024);
    if (void* pRoot = pPlayer->GetNiNode())
        CollectNodes(pRoot, nodes);
    void* pTop = nullptr;
    if (SafeRead(reinterpret_cast<uint8_t*>(pPlayer) + kFirstPersonHandOffset, pTop) && pTop)
    {
        // Up to the skeleton's own root, the first node named after its file: above it is the world.
        void* pParent = nullptr;
        for (int depth = 0; depth < 32 && TextAt(*reinterpret_cast<const char* const*>(static_cast<uint8_t*>(pTop) + kNameOffset)).find(".nif") == std::string::npos &&
                            SafeRead(static_cast<uint8_t*>(pTop) + kParentOffset, pParent) && pParent;
             ++depth)
            pTop = pParent;
        CollectNodes(pTop, nodes);
    }
    std::scoped_lock lock(s_heldLock);
    s_playerNodes.swap(nodes);
}

// The class of an object from MSVC's type information before its vtable.
std::string ClassOf(const void* apVTable) noexcept
{
    const void* pLocator = nullptr;
    if (!apVTable || !SafeRead(static_cast<const void* const*>(apVTable) - 1, pLocator) || !GameStack::Readable(pLocator, 0x18))
        return {};
    const auto* pFields = static_cast<const uint32_t*>(pLocator);
    if (pFields[0] != 1)
        return {};
    const uintptr_t cModule = reinterpret_cast<uintptr_t>(pLocator) - pFields[5];
    return TextAt(reinterpret_cast<const char*>(cModule + pFields[3] + 0x10));
}

// Hooks the destructor of the class of the first eye bone found on a loaded actor.
void HookFromALoadedActor(const BSFixedString& acName) noexcept
{
    using TGetObjectByName = void*(void*, const char* const*, char);
    POINTER_SKYRIMSE(TGetObjectByName, GetObjectByName, 76207, 76207);

    ProcessLists* pProcessLists = ProcessLists::Get();
    if (!pProcessLists)
        return;
    for (uint32_t i = 0; i < pProcessLists->highActorHandleArray.length; ++i)
    {
        TESObjectREFR* pRefr = TESObjectREFR::GetByHandle(pProcessLists->highActorHandleArray[i]);
        NiNode* pRoot = pRefr ? pRefr->GetNiNode() : nullptr;
        void* pBone = pRoot ? GetObjectByName(pRoot, &acName.data, 1) : nullptr;
        void** pVTable = nullptr;
        if (!pBone || !SafeRead(pBone, pVTable) || !pVTable)
            continue;

        for (void*& slot : s_deadVTable)
            slot = reinterpret_cast<void*>(&DeadNodeCall);

        DWORD oldProtect = 0;
        if (!VirtualProtect(&pVTable[0], sizeof(void*), PAGE_READWRITE, &oldProtect))
            return;
        s_pReal.store(reinterpret_cast<TDestructor*>(pVTable[0]), std::memory_order_release);
        s_pVTable.store(pVTable, std::memory_order_release);
        pVTable[0] = reinterpret_cast<void*>(&HookDestructor);
        VirtualProtect(&pVTable[0], sizeof(void*), oldProtect, &oldProtect);
        spdlog::info("EyeBoneWatch: the destruction of {} ({}) is watched for a node freed while its parent still holds it (found on {:X}'s eye bone); freed named nodes are held {} ms, the player's own {} s",
                     ClassOf(pVTable), GameStack::Where(pVTable), pRefr->formID, kHoldFor.count(), kPlayerHoldFor.count());
        return;
    }
}

std::string Describe(const Late& acLate) noexcept
{
    const char* cpWhose = acLate.Player ? "the player's freed bone" : "freed node";
    if (acLate.Write && acLate.Writes > 1)
        return fmt::format("{} '{}' ({}) was written in {} looks before its memory went back, {} ms after its free", cpWhose, acLate.Name, fmt::ptr(acLate.pNode), acLate.Writes,
                           acLate.AfterMs);
    if (acLate.Write)
        return fmt::format("{} '{}' ({}) was written {} ms or less after its free ({}{}{}); its local transform now: rotate [{:.3f} {:.3f} {:.3f} / {:.3f} {:.3f} {:.3f} / {:.3f} {:.3f} {:.3f}], position ({:.3f}, {:.3f}, {:.3f})",
                           cpWhose, acLate.Name, fmt::ptr(acLate.pNode), acLate.AfterMs, acLate.LocalChanged ? "local" : "", acLate.LocalChanged && acLate.WorldChanged ? " and " : "",
                           acLate.WorldChanged ? "world" : "", acLate.Local[0], acLate.Local[1], acLate.Local[2], acLate.Local[3], acLate.Local[4], acLate.Local[5],
                           acLate.Local[6], acLate.Local[7], acLate.Local[8], acLate.Local[9], acLate.Local[10], acLate.Local[11]);
    return fmt::format("{} '{}' ({}) was called through its class table {} ms after its free, from {} on thread {} (main thread {}) in frame {}", cpWhose, acLate.Name,
                       fmt::ptr(acLate.pNode), acLate.AfterMs, GameStack::Where(acLate.pCaller), acLate.Thread, GameMainThreadId(), acLate.Frame);
}
} // namespace

namespace BoneHold
{
void SetDestroyObserver(DestroyObserver aObserver) noexcept
{
    s_observer.store(aObserver, std::memory_order_release);
}

void Update() noexcept
{
    static const BSFixedString s_name("NPCEyeBone");
    static uint64_t s_nextTry = 0;
    const uint64_t cFrame = MainLoopFrame();
    if (!s_pVTable.load(std::memory_order_acquire) && cFrame >= s_nextTry)
    {
        s_nextTry = cFrame + 300;
        HookFromALoadedActor(s_name);
    }
    if (!s_pVTable.load(std::memory_order_acquire))
        return;

    // The player's skeleton is rebuilt at a race change, so it is read again every half second or so.
    if (cFrame % 30 == 0)
        UpdatePlayerNodes();
    // Before anything held goes back, so a repaired entry no longer reaches it.
    RepairGraphs(cFrame);
    ReleaseOldHeld();
    UpdateWatch();

    // The first ones in full, then one in every hundred, so a plugin that does this often cannot flood the log.
    static size_t s_toldLate = 0;
    Late lates[std::size(s_late)]{};
    size_t lateFirst = 0;
    size_t lateLast = 0;
    {
        std::scoped_lock lock(s_heldLock);
        lateFirst = std::max(s_toldLate, s_lateCount > std::size(s_late) ? s_lateCount - std::size(s_late) : size_t{0});
        lateLast = s_lateCount;
        for (size_t i = lateFirst; i < lateLast; ++i)
            lates[i - lateFirst] = s_late[i % std::size(s_late)];
        s_toldLate = lateLast;
    }
    for (size_t i = lateFirst; i < lateLast; ++i)
        if (i < kToldInFull || (i + 1) % kToldEvery == 0)
            spdlog::error("NodeQuarantine: {} ({} so far)", Describe(lates[i - lateFirst]), i + 1);

    static auto s_nextSummary = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    if (const auto cNow = std::chrono::steady_clock::now(); cNow >= s_nextSummary)
    {
        s_nextSummary = cNow + std::chrono::seconds(60);
        std::scoped_lock lock(s_heldLock);
        spdlog::info("NodeQuarantine: {} freed named nodes held so far ({} of the player's), {} and {} held now, {} reached after their free; {} player nodes known",
                     s_heldTotal + s_playerHeldTotal, s_playerHeldTotal, s_held.Count, s_playerHeld.Count, s_lateCount, s_playerNodes.size());
    }
}

void Report(const uint64_t* apRegisters, size_t aCount) noexcept
{
    if (!s_pVTable.load(std::memory_order_acquire))
        return;
    // The crashing thread may hold a lock: then nothing is told rather than waiting forever.
    {
        std::unique_lock lock(s_heldLock, std::try_to_lock);
        if (lock.owns_lock())
        {
            spdlog::error("NodeQuarantine: {} freed named nodes held this session ({} of the player's), {} reached after their free", s_heldTotal + s_playerHeldTotal,
                          s_playerHeldTotal, s_lateCount);
            for (size_t n = 0; n < std::min(s_lateCount, std::size(s_late)); ++n)
                spdlog::error("    {}", Describe(s_late[(s_lateCount - 1 - n) % std::size(s_late)]));
            // A crashing register pointing into a held node names it.
            for (HeldList* pList : {&s_playerHeld, &s_held})
                for (size_t i = 0; i < pList->Count; ++i)
                {
                    const Held& cHeld = pList->At(i);
                    for (size_t r = 0; r < aCount; ++r)
                        if (apRegisters[r] >= reinterpret_cast<uint64_t>(cHeld.pMemory) && apRegisters[r] < reinterpret_cast<uint64_t>(cHeld.pMemory) + 0x180)
                            spdlog::error("    register {} points into held node '{}' ({}), freed in frame {}", r, cHeld.Name, fmt::ptr(cHeld.pMemory), cHeld.Frame);
                }
        }
    }
}
} // namespace BoneHold
#endif
