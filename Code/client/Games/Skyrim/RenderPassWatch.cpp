#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>
#include <Interface/Menus/MapMenu.h>
#include <PatchCheck.h>

#include <intrin.h>

// Every pass the game draws is put on a list by BSBatchRenderer's two register functions (its vtable slots 1 and 2).
// Put on its list twice, a pass links to itself: the head insert makes its next itself, and the sorted insert stops on
// it and does the same. The draw loop (VR 0x141349500) then follows it forever, a frozen game.
// This watches for that. After each insert, a pass that now links to itself is logged with where both of its inserts
// came from, and its link is put back as it was, so the list stays whole and the frame ends.
namespace
{
constexpr uintptr_t kBatchRendererVtable = 0x1906E88;
// The RTTI locator before the vtable, BSBatchRenderer's in SkyrimVR 1.4.15.
constexpr uintptr_t kBatchRendererLocator = 0x1A31D48;
constexpr size_t kPassNext = 0x30;
constexpr size_t kPassGeometry = 0x10;
constexpr size_t kObjectName = 0x10;

using TRegisterPass = uint64_t(void*, uint8_t*, uint64_t, uint64_t);
TRegisterPass* s_realRegister[2]{};

// The last insert of each pass, one slot per address and overwritten, so a pass's previous insert is there only if
// nothing else landed in its slot meanwhile. Written from every thread that registers passes; only read to report.
struct LastInsert
{
    const void* Pass;
    const void* Renderer;
    const void* Caller;
    uint32_t Thread;
    uint64_t Frame;
};
LastInsert s_lastInserts[4096];

std::atomic<uint32_t> s_selfLinks{0};

size_t SlotOf(const void* apPass) noexcept
{
    return (reinterpret_cast<uintptr_t>(apPass) >> 4) & (std::size(s_lastInserts) - 1);
}

bool Readable(const void* apAddress, const size_t aBytes) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!apAddress || !VirtualQuery(apAddress, &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return reinterpret_cast<uintptr_t>(apAddress) + aBytes <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}

std::string Where(const void* apAddress) noexcept
{
    HMODULE module = nullptr;
    if (apAddress && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(apAddress), &module))
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        const std::wstring cPath(path);
        const std::wstring cName = cPath.substr(cPath.find_last_of(L"\\/") + 1);
        return fmt::format("{}+{:#x}", std::string(cName.begin(), cName.end()), reinterpret_cast<uintptr_t>(apAddress) - reinterpret_cast<uintptr_t>(module));
    }
    return fmt::format("{}", apAddress);
}

// The game's functions are not in our image's unwind table, so RtlCaptureStackBackTrace stops at the first one.
// The game's own table (SkyrimVR 1.4.15's exception directory) is in our image at its own RVA and is searched here.
constexpr uintptr_t kGameUnwindTable = 0x36F4000;
constexpr size_t kGameUnwindEntries = 0x15B058 / sizeof(RUNTIME_FUNCTION);
constexpr DWORD kGameCodeEnd = 0x1580000;
uintptr_t s_exe = 0;
bool s_gameUnwindTable = false;

const RUNTIME_FUNCTION* GameFunctionEntry(const DWORD64 aPc) noexcept
{
    const DWORD64 cRva = aPc - s_exe;
    if (aPc < s_exe || cRva >= kGameCodeEnd)
        return nullptr;
    const auto* pEntries = reinterpret_cast<const RUNTIME_FUNCTION*>(s_exe + kGameUnwindTable);
    size_t low = 0;
    size_t high = kGameUnwindEntries;
    while (low < high)
    {
        const size_t cMiddle = (low + high) / 2;
        if (cRva < pEntries[cMiddle].BeginAddress)
            high = cMiddle;
        else if (cRva >= pEntries[cMiddle].EndAddress)
            low = cMiddle + 1;
        else
            return &pEntries[cMiddle];
    }
    return nullptr;
}

// The table is used only if it reads as one: entries in order, each inside the game's code.
void CheckGameUnwindTable() noexcept
{
    s_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* pEntries = reinterpret_cast<const RUNTIME_FUNCTION*>(s_exe + kGameUnwindTable);
    for (size_t i = 0; i < 64; ++i)
    {
        if (pEntries[i].BeginAddress >= pEntries[i].EndAddress || pEntries[i].EndAddress > kGameCodeEnd ||
            (i && pEntries[i].BeginAddress < pEntries[i - 1].EndAddress))
        {
            spdlog::error("RenderPassWatch: the game's unwind table does not read as one (entry {}); stacks stop at the first game frame", i);
            return;
        }
    }
    s_gameUnwindTable = true;
}

// The return addresses from the caller's caller up, through game and plugin frames alike.
__declspec(noinline) USHORT CaptureStack(void** apFrames, const USHORT aMax) noexcept
{
    if (!s_gameUnwindTable)
        return RtlCaptureStackBackTrace(2, aMax, apFrames, nullptr);

    CONTEXT context{};
    RtlCaptureContext(&context);
    USHORT depth = 0;
    int skip = 1;
    __try
    {
        while (depth < aMax && context.Rip)
        {
            DWORD64 imageBase = 0;
            auto* pEntry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (!pEntry)
            {
                pEntry = const_cast<RUNTIME_FUNCTION*>(GameFunctionEntry(context.Rip));
                imageBase = s_exe;
            }
            if (pEntry)
            {
                void* pHandlerData = nullptr;
                DWORD64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, pEntry, &context, &pHandlerData, &establisherFrame, nullptr);
            }
            else
            {
                // A leaf function: its return address is on top of the stack.
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            }
            if (!context.Rip)
                break;
            if (skip-- > 0)
                continue;
            apFrames[depth++] = reinterpret_cast<void*>(context.Rip);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return depth;
}

std::string GeometryName(const uint8_t* apPass) noexcept
{
    if (!Readable(apPass + kPassGeometry, 8))
        return "?";
    const auto* pGeometry = *reinterpret_cast<const uint8_t* const*>(apPass + kPassGeometry);
    if (!Readable(pGeometry + kObjectName, 8))
        return "?";
    const char* pName = *reinterpret_cast<const char* const*>(pGeometry + kObjectName);
    if (!Readable(pName, 1))
        return "?";
    char name[64]{};
    for (size_t i = 0; i + 1 < sizeof(name) && Readable(pName + i, 1) && pName[i]; ++i)
        name[i] = pName[i];
    return name;
}

void ReportSelfLink(const void* apRenderer, const uint8_t* apPass, const void* apCaller, const size_t aSlot) noexcept
{
    const uint32_t cCount = ++s_selfLinks;
    // Every one counts; the first twenty are told in full, then one in five hundred.
    if (cCount > 20 && cCount % 500 != 0)
        return;

    const LastInsert cPrevious = s_lastInserts[SlotOf(apPass)];
    const bool cKnown = cPrevious.Pass == apPass;
    const uint64_t cFrame = MainLoopFrame();
    void* frames[16]{};
    const USHORT cDepth = RtlCaptureStackBackTrace(2, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    std::string stack;
    for (USHORT i = 0; i < cDepth; ++i)
        stack += (i ? " < " : "") + Where(frames[i]);

    spdlog::error("RenderPassWatch: pass {} ('{}') put on renderer {}'s list a second time (slot {}, number {}); it linked to itself, the draw "
                  "would have followed it forever; its link put back",
                  fmt::ptr(apPass), GeometryName(apPass), fmt::ptr(apRenderer), aSlot, cCount);
    spdlog::error("RenderPassWatch:   this insert from {} on thread {} (main thread {}), frame {}; the map {}", Where(apCaller), GetCurrentThreadId(),
                  GameMainThreadId(), cFrame, MapMenuShown() ? "shown" : "not shown");
    if (cKnown)
        spdlog::error("RenderPassWatch:   the insert before from {} on thread {} into renderer {}, frame {}", Where(cPrevious.Caller), cPrevious.Thread,
                      fmt::ptr(cPrevious.Renderer), cPrevious.Frame);
    else
        spdlog::error("RenderPassWatch:   the insert before was not kept (another pass took its slot)");
    spdlog::error("RenderPassWatch:   stack: {}", stack);
}

// The VR laser's geometry is the one found linked to itself after a map close.
// Names are interned, so a geometry is the laser when its name is this very string.
const char* PointerName() noexcept
{
    static const BSFixedString s_name("UI Pointer Geometry");
    return s_name.data;
}

bool IsPointerPass(const uint8_t* apPass) noexcept
{
    const auto* pGeometry = *reinterpret_cast<const uint8_t* const*>(apPass + kPassGeometry);
    return pGeometry && *reinterpret_cast<const char* const*>(pGeometry + kObjectName) == PointerName();
}

struct PointerInsert
{
    const uint8_t* Pass;
    uint32_t Count;
    const void* Renderer;
    uint32_t Thread;
    size_t Slot;
    void* Frames[24];
    USHORT Depth;
};

std::string StackOf(void* const* apFrames, const USHORT aDepth) noexcept
{
    std::string stack;
    for (USHORT i = 0; i < aDepth; ++i)
        stack += (i ? " < " : "") + Where(apFrames[i]);
    return stack;
}

std::string StackOf(const PointerInsert& acInsert) noexcept
{
    return StackOf(acInsert.Frames, acInsert.Depth);
}

// A full stack costs a few microseconds, so the laser's are taken only in the frames right after a map close, where
// every double insert came, and for the lines told once.
bool NearMapClose() noexcept
{
    const uint64_t cHide = MapMenuLastHideFrame();
    const uint64_t cFrame = MainLoopFrame();
    return cHide && cFrame >= cHide && cFrame - cHide <= 3;
}

// What fed the laser to each accumulator in the current frame: the culler's appends, with where the laser hung then,
// and the walks that turn an accumulator's array into passes on its renderer.
// Together they tell an object culled twice from an array walked twice.
// Taken by the render threads that append, walk and insert the laser.
std::mutex s_pointerLock;

constexpr size_t kAccumulatorRenderer = 0x158;
constexpr size_t kAccumulatorCount = 0xD0;
constexpr size_t kAccumulatorStart = 0xD8;
constexpr size_t kObjectParent = 0x30;

struct FeedRecord
{
    uint32_t Thread;
    uint32_t Start;
    uint32_t Count;
    const char* Parents[6];
    void* Frames[24];
    USHORT Depth;
};

struct AccumulatorFeed
{
    const void* Accumulator;
    const void* Renderer;
    bool Culler;
    uint32_t Appends;
    FeedRecord AppendRecords[3];
    uint32_t Walks;
    FeedRecord WalkRecords[3];
};

AccumulatorFeed s_feeds[8]{};
size_t s_feedCount = 0;
uint64_t s_feedFrame = 0;
std::atomic<uint64_t> s_pointerAppendFrame{0};

// Called under s_pointerLock.
AccumulatorFeed* FeedOf(const void* apAccumulator, const bool acCreate, const bool acCuller = false) noexcept
{
    const uint64_t cFrame = MainLoopFrame();
    if (cFrame != s_feedFrame)
    {
        s_feedFrame = cFrame;
        s_feedCount = 0;
    }
    for (size_t i = 0; i < s_feedCount; ++i)
        if (s_feeds[i].Accumulator == apAccumulator)
            return &s_feeds[i];
    if (!acCreate || s_feedCount == std::size(s_feeds))
        return nullptr;
    AccumulatorFeed& feed = s_feeds[s_feedCount++];
    feed = AccumulatorFeed{apAccumulator, acCuller ? nullptr : *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(apAccumulator) + kAccumulatorRenderer), acCuller};
    return &feed;
}

std::string ParentsOf(const FeedRecord& acRecord) noexcept
{
    std::string parents;
    for (const char* pName : acRecord.Parents)
        if (pName)
            parents += fmt::format("{}'{}'", parents.empty() ? "" : " < ", pName);
    return parents;
}

void LogFeed(const AccumulatorFeed& acFeed) noexcept
{
    if (acFeed.Culler)
    {
        spdlog::warn("RenderPassWatch:   visible list {} this frame: the laser put on it {} time(s)", acFeed.Accumulator, acFeed.Appends);
        for (uint32_t i = 0; i < std::min<uint32_t>(acFeed.Appends, 3); ++i)
        {
            const FeedRecord& record = acFeed.AppendRecords[i];
            spdlog::warn("RenderPassWatch:     visit {} on thread {}, under {}: {}", i + 1, record.Thread, ParentsOf(record), StackOf(record.Frames, record.Depth));
        }
        return;
    }
    spdlog::warn("RenderPassWatch:   accumulator {} (renderer {}) this frame: the laser appended {} time(s), its array walked {} time(s)", acFeed.Accumulator,
                 acFeed.Renderer, acFeed.Appends, acFeed.Walks);
    for (uint32_t i = 0; i < std::min<uint32_t>(acFeed.Appends, 3); ++i)
    {
        const FeedRecord& record = acFeed.AppendRecords[i];
        spdlog::warn("RenderPassWatch:     append {} on thread {}, under {}: {}", i + 1, record.Thread, ParentsOf(record), StackOf(record.Frames, record.Depth));
    }
    for (uint32_t i = 0; i < std::min<uint32_t>(acFeed.Walks, 3); ++i)
    {
        const FeedRecord& record = acFeed.WalkRecords[i];
        spdlog::warn("RenderPassWatch:     walk {} on thread {}, from entry {} of {}: {}", i + 1, record.Thread, record.Start, record.Count,
                     StackOf(record.Frames, record.Depth));
    }
}

void TracePointerAppend(const void* apAccumulator, const uint8_t* apGeometry, const bool acCuller = false) noexcept
{
    FeedRecord record{GetCurrentThreadId()};
    if (NearMapClose())
        record.Depth = CaptureStack(record.Frames, static_cast<USHORT>(std::size(record.Frames)));
    const auto* pParent = *reinterpret_cast<const uint8_t* const*>(apGeometry + kObjectParent);
    for (size_t i = 0; pParent && i < std::size(record.Parents); ++i)
    {
        record.Parents[i] = *reinterpret_cast<const char* const*>(pParent + kObjectName);
        pParent = *reinterpret_cast<const uint8_t* const*>(pParent + kObjectParent);
    }

    std::scoped_lock lock(s_pointerLock);
    s_pointerAppendFrame = MainLoopFrame();

    // Each culler and accumulator the laser goes through is told once, the normal paths to compare a double insert with.
    static const void* s_seen[16]{};
    static size_t s_seenCount = 0;
    if (std::find(s_seen, s_seen + s_seenCount, apAccumulator) == s_seen + s_seenCount && s_seenCount < std::size(s_seen))
    {
        s_seen[s_seenCount++] = apAccumulator;
        if (!record.Depth)
            record.Depth = CaptureStack(record.Frames, static_cast<USHORT>(std::size(record.Frames)));
        spdlog::info("RenderPassWatch: the VR pointer first {} {} on thread {} (main thread {}), under {}: {}", acCuller ? "put on visible list" : "appended to accumulator",
                     apAccumulator, record.Thread, GameMainThreadId(), ParentsOf(record), StackOf(record.Frames, record.Depth));
    }

    AccumulatorFeed* pFeed = FeedOf(apAccumulator, true, acCuller);
    if (!pFeed)
        return;
    if (pFeed->Appends < std::size(pFeed->AppendRecords))
        pFeed->AppendRecords[pFeed->Appends] = record;
    ++pFeed->Appends;
}

void TracePointerWalk(const void* apAccumulator) noexcept
{
    // Only the accumulators the laser went to this frame.
    if (s_pointerAppendFrame.load(std::memory_order_relaxed) != MainLoopFrame())
        return;

    const auto* pAccumulator = static_cast<const uint8_t*>(apAccumulator);
    FeedRecord record{GetCurrentThreadId(), *reinterpret_cast<const uint32_t*>(pAccumulator + kAccumulatorStart),
                      *reinterpret_cast<const uint32_t*>(pAccumulator + kAccumulatorCount)};

    std::scoped_lock lock(s_pointerLock);
    AccumulatorFeed* pFeed = FeedOf(apAccumulator, false);
    if (!pFeed)
        return;
    if (pFeed->Walks < std::size(pFeed->WalkRecords))
    {
        if (NearMapClose())
            record.Depth = CaptureStack(record.Frames, static_cast<USHORT>(std::size(record.Frames)));
        pFeed->WalkRecords[pFeed->Walks] = record;
    }
    ++pFeed->Walks;
}

// Each laser pass's first insert in one main loop frame, to name what puts it on a list a second time before the
// list is drawn, which a list it is already on turns into a loop. Inserts come from the render threads.
uint64_t s_pointerFrame = 0;
PointerInsert s_pointerInserts[4]{};
size_t s_pointerPasses = 0;
uint64_t s_framesByInserts[3]{};
std::chrono::steady_clock::time_point s_lastPointerSummary{};
uint32_t s_doubleInserts = 0;

// The threads the laser's pass was put on a list from, each told once with its stack.
uint32_t s_pointerThreads[16]{};
size_t s_pointerThreadCount = 0;

void TracePointerInsert(const void* apRenderer, const uint8_t* apPass, const size_t aSlot, const void* apNextBefore) noexcept
{
    PointerInsert insert{apPass, 1, apRenderer, GetCurrentThreadId(), aSlot, {}, 0};
    if (NearMapClose())
        insert.Depth = CaptureStack(insert.Frames, static_cast<USHORT>(std::size(insert.Frames)));
    const uint64_t cFrame = MainLoopFrame();

    std::scoped_lock lock(s_pointerLock);

    if (std::find(s_pointerThreads, s_pointerThreads + s_pointerThreadCount, insert.Thread) == s_pointerThreads + s_pointerThreadCount &&
        s_pointerThreadCount < std::size(s_pointerThreads))
    {
        s_pointerThreads[s_pointerThreadCount++] = insert.Thread;
        if (!insert.Depth)
            insert.Depth = CaptureStack(insert.Frames, static_cast<USHORT>(std::size(insert.Frames)));
        spdlog::info("RenderPassWatch: the VR pointer's pass {} put on a list from thread {} (main thread {}), renderer {}, slot {}, frame {}, its link before {}; "
                     "the map {}: {}",
                     fmt::ptr(apPass), insert.Thread, GameMainThreadId(), fmt::ptr(apRenderer), aSlot, cFrame, apNextBefore, MapMenuShown() ? "shown" : "not shown",
                     StackOf(insert));
    }
    if (cFrame != s_pointerFrame)
    {
        uint32_t most = 0;
        for (size_t i = 0; i < s_pointerPasses; ++i)
            most = std::max(most, s_pointerInserts[i].Count);
        if (most)
            ++s_framesByInserts[std::min<uint32_t>(most, 3) - 1];
        s_pointerFrame = cFrame;
        s_pointerPasses = 0;
    }

    PointerInsert* pFirst = nullptr;
    for (size_t i = 0; i < s_pointerPasses; ++i)
        if (s_pointerInserts[i].Pass == apPass)
            pFirst = &s_pointerInserts[i];

    if (!pFirst)
    {
        if (s_pointerPasses < std::size(s_pointerInserts))
            s_pointerInserts[s_pointerPasses++] = insert;
    }
    else
    {
        ++pFirst->Count;
        // Every one counts; the first twenty are told in full, then one in five hundred.
        const uint32_t cCount = ++s_doubleInserts;
        if (cCount <= 20 || cCount % 500 == 0)
        {
            spdlog::warn("RenderPassWatch: the VR pointer's pass {} put on a list again in frame {} (insert {} this frame, number {}); the map {}", fmt::ptr(apPass), cFrame,
                         pFirst->Count, cCount, MapMenuShown() ? "shown" : "not shown");
            spdlog::warn("RenderPassWatch:   first: renderer {}, slot {}, thread {}: {}", fmt::ptr(pFirst->Renderer), pFirst->Slot, pFirst->Thread, StackOf(*pFirst));
            spdlog::warn("RenderPassWatch:   this: renderer {}, slot {}, thread {}: {}", fmt::ptr(insert.Renderer), insert.Slot, insert.Thread, StackOf(insert));
            // Every accumulator and culler the laser went through this frame, that renderer's among them.
            for (size_t i = 0; s_feedFrame == cFrame && i < s_feedCount; ++i)
                LogFeed(s_feeds[i]);
            if (s_feedFrame != cFrame || !s_feedCount)
                spdlog::warn("RenderPassWatch:   no append of the laser was seen this frame");
        }
    }

    const auto cNow = std::chrono::steady_clock::now();
    if (cNow - s_lastPointerSummary >= std::chrono::seconds(30))
    {
        s_lastPointerSummary = cNow;
        spdlog::info("RenderPassWatch: frames with the VR pointer drawn, by its inserts in the frame: once {}, twice {}, three times or more {}", s_framesByInserts[0],
                     s_framesByInserts[1], s_framesByInserts[2]);
    }
}

// The game takes passes from a pool each frame and puts each one on a list that the frame draws and empties.
// A list left from an earlier frame still holds passes the pool has handed out again, and one handed back to its
// own list links to itself. So a pass inserted behind one put on the same list two or more frames ago is told.
std::atomic<uint32_t> s_staleLists{0};

void CheckStaleList(const void* apRenderer, const uint8_t* apPass, const void* apNext, const size_t aSlot) noexcept
{
    const LastInsert cNext = s_lastInserts[SlotOf(apNext)];
    const uint64_t cFrame = MainLoopFrame();
    if (cNext.Pass != apNext || cNext.Renderer != apRenderer || cNext.Frame + 2 > cFrame)
        return;

    // Every one counts; the first twenty are told in full, then one in five hundred.
    const uint32_t cCount = ++s_staleLists;
    if (cCount > 20 && cCount % 500 != 0)
        return;

    void* frames[14]{};
    const USHORT cDepth = RtlCaptureStackBackTrace(2, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    std::string stack;
    for (USHORT i = 0; i < cDepth; ++i)
        stack += (i ? " < " : "") + Where(frames[i]);

    spdlog::error("RenderPassWatch: renderer {}'s list still holds pass {} from frame {} (inserted from {} on thread {}) in frame {}; pass {} ('{}') put "
                  "before it from thread {} (main thread {}), slot {}, number {}; the map {}: {}",
                  fmt::ptr(apRenderer), apNext, cNext.Frame, Where(cNext.Caller), cNext.Thread, cFrame, fmt::ptr(apPass), GeometryName(apPass),
                  GetCurrentThreadId(), GameMainThreadId(), aSlot, cCount, MapMenuShown() ? "shown" : "not shown", stack);
}

template <size_t Slot>
uint64_t HookRegister(void* apRenderer, uint8_t* apPass, const uint64_t a3, const uint64_t a4)
{
    void* const cBefore = apPass ? *reinterpret_cast<void**>(apPass + kPassNext) : nullptr;
    const uint64_t cResult = s_realRegister[Slot - 1](apRenderer, apPass, a3, a4);
    if (!apPass)
        return cResult;

    if (IsPointerPass(apPass))
        TracePointerInsert(apRenderer, apPass, Slot, cBefore);

    const void* cCaller = _ReturnAddress();
    auto** ppNext = reinterpret_cast<void**>(apPass + kPassNext);
    if (*ppNext == apPass)
    {
        // Its next before this insert is its place in the list it was already on.
        *ppNext = cBefore == apPass ? nullptr : cBefore;
        ReportSelfLink(apRenderer, apPass, cCaller, Slot);
    }
    else if (*ppNext)
        CheckStaleList(apRenderer, apPass, *ppNext, Slot);
    s_lastInserts[SlotOf(apPass)] = LastInsert{apPass, apRenderer, cCaller, GetCurrentThreadId(), MainLoopFrame()};
    return cResult;
}

// The draw of one list (VR 0x141349270), on the main thread: the renderer, its technique, the list's index in the technique's group.
// It finds the group in the renderer's hash map, then follows the list from that group's head until a pass with no next.
// Passes put on twice can also close a loop of several (the two controller models after a map close), which the insert check above misses.
// So each list is checked as the draw finds it, and the link that closes a loop is cut before the draw follows it.
constexpr uintptr_t kDrawList = 0x1349270;
constexpr size_t kRendererGroups = 0x8;
constexpr size_t kRendererMapCapacity = 0x2C;
constexpr size_t kRendererMapEnd = 0x38;
constexpr size_t kRendererMap = 0x48;
// A group is five list heads and a mask, 0x30 bytes.
constexpr size_t kGroupHeads = 6;

using TDrawList = uint64_t(void*, uint32_t*, int32_t*, uint64_t, uint32_t);
TDrawList* s_realDrawList = nullptr;

std::atomic<uint32_t> s_cutLoops{0};

uint8_t* NextOf(const uint8_t* apPass) noexcept
{
    return *reinterpret_cast<uint8_t* const*>(apPass + kPassNext);
}

// The list head the draw starts from, found the way the draw finds it (technique not in the map: group 0).
uint8_t** DrawnListHead(uint8_t* apRenderer, const uint32_t aTechnique, const int32_t aList) noexcept
{
    uint32_t group = 0;
    if (auto* pEntry = *reinterpret_cast<uint8_t**>(apRenderer + kRendererMap))
    {
        const uint32_t cCapacity = *reinterpret_cast<const uint32_t*>(apRenderer + kRendererMapCapacity);
        pEntry += static_cast<size_t>(aTechnique & (cCapacity - 1)) << 4;
        if (*reinterpret_cast<uint8_t**>(pEntry + 8))
        {
            const auto* pEnd = *reinterpret_cast<uint8_t**>(apRenderer + kRendererMapEnd);
            do
            {
                if (*reinterpret_cast<const uint32_t*>(pEntry) == aTechnique)
                {
                    group = *reinterpret_cast<const uint32_t*>(pEntry + 4);
                    break;
                }
                pEntry = *reinterpret_cast<uint8_t**>(pEntry + 8);
            } while (pEntry != pEnd);
        }
    }
    return *reinterpret_cast<uint8_t***>(apRenderer + kRendererGroups) + group * kGroupHeads + aList;
}

// Brent's loop finding, no allocation: an ordinary list is walked about twice, a looping one cut where its last pass leads back.
void CutLoop(void* apRenderer, uint8_t** appHead, const uint32_t aTechnique, const int32_t aList) noexcept
{
    uint8_t* pSlow = *appHead;
    if (!pSlow)
        return;
    uint8_t* pFast = NextOf(pSlow);
    size_t power = 1;
    size_t length = 1;
    while (pFast && pFast != pSlow)
    {
        if (power == length)
        {
            pSlow = pFast;
            power *= 2;
            length = 0;
        }
        pFast = NextOf(pFast);
        ++length;
    }
    if (!pFast)
        return;

    // The loop is length passes long: its first pass is where two walks that far apart meet.
    pSlow = pFast = *appHead;
    for (size_t i = 0; i < length; ++i)
        pFast = NextOf(pFast);
    size_t before = 0;
    while (pSlow != pFast)
    {
        pSlow = NextOf(pSlow);
        pFast = NextOf(pFast);
        ++before;
    }
    uint8_t* pLast = pSlow;
    for (size_t i = 1; i < length; ++i)
        pLast = NextOf(pLast);
    *reinterpret_cast<uint8_t**>(pLast + kPassNext) = nullptr;

    // Every one counts; the first twenty are told in full, then one in five hundred.
    const uint32_t cCount = ++s_cutLoops;
    if (cCount > 20 && cCount % 500 != 0)
        return;
    spdlog::error("RenderPassWatch: pass {} ('{}') led back to pass {} ('{}') in renderer {}'s list (technique {:#x}, list {}): a loop of {} passes, {} after the head; "
                  "cut before the draw, number {}; frame {}, the map {}",
                  fmt::ptr(pLast), GeometryName(pLast), fmt::ptr(pSlow), GeometryName(pSlow), fmt::ptr(apRenderer), aTechnique, aList, length, before, cCount, MainLoopFrame(),
                  MapMenuShown() ? "shown" : "not shown");
}

uint64_t HookDrawList(void* apRenderer, uint32_t* apTechnique, int32_t* apList, const uint64_t a4, const uint32_t aFlags)
{
    CutLoop(apRenderer, DrawnListHead(static_cast<uint8_t*>(apRenderer), *apTechnique, *apList), *apTechnique, *apList);
    return s_realDrawList(apRenderer, apTechnique, apList, a4, aFlags);
}

// The add to a culler's visible list (VR 0x140D9A990, the list at culler+0x128, through which every culler's append goes),
// the append of an object to an accumulator (VR 0x141308570, which in some accumulators registers its passes at once),
// and the walk of an accumulator's array that registers each object's passes on its renderer (VR 0x1413087D0).
constexpr uintptr_t kCullerAppend = 0xD9A990;
constexpr uintptr_t kAccumulatorAppend = 0x1308570;
constexpr uintptr_t kAccumulatorWalk = 0x13087D0;

using TCullerAppend = uint64_t(void*, uint8_t*);
using TAccumulatorAppend = uint64_t(void*, uint8_t*, uint64_t);
using TAccumulatorWalk = uint64_t(void*);
TCullerAppend* s_realCullerAppend = nullptr;
TAccumulatorAppend* s_realAppend = nullptr;
TAccumulatorWalk* s_realWalk = nullptr;

uint64_t HookCullerAppend(void* apVisibleList, uint8_t* apGeometry)
{
    if (apGeometry && *reinterpret_cast<const char* const*>(apGeometry + kObjectName) == PointerName())
        TracePointerAppend(apVisibleList, apGeometry, true);
    return s_realCullerAppend(apVisibleList, apGeometry);
}

uint64_t HookAccumulatorAppend(void* apAccumulator, uint8_t* apGeometry, const uint64_t aExtra)
{
    if (apGeometry && *reinterpret_cast<const char* const*>(apGeometry + kObjectName) == PointerName())
        TracePointerAppend(apAccumulator, apGeometry);
    return s_realAppend(apAccumulator, apGeometry, aExtra);
}

uint64_t HookAccumulatorWalk(void* apAccumulator)
{
    TracePointerWalk(apAccumulator);
    return s_realWalk(apAccumulator);
}
} // namespace

static TiltedPhoques::Initializer s_renderPassWatch(
    []()
    {
        const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto** pVtable = reinterpret_cast<void**>(exe + kBatchRendererVtable);
        // The vtable is BSBatchRenderer's only if its RTTI locator, right before it, is.
        if (reinterpret_cast<uintptr_t>(pVtable[-1]) != exe + kBatchRendererLocator)
        {
            spdlog::error("RenderPassWatch: BSBatchRenderer's vtable is not at {} ({} before it); passes are not watched", fmt::ptr(pVtable), pVtable[-1]);
            return;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(&pVtable[1], 2 * sizeof(void*), PAGE_READWRITE, &oldProtect))
        {
            spdlog::error("RenderPassWatch: BSBatchRenderer's vtable could not be made writable ({}); passes are not watched", GetLastError());
            return;
        }
        s_realRegister[0] = reinterpret_cast<TRegisterPass*>(pVtable[1]);
        s_realRegister[1] = reinterpret_cast<TRegisterPass*>(pVtable[2]);
        pVtable[1] = reinterpret_cast<void*>(&HookRegister<1>);
        pVtable[2] = reinterpret_cast<void*>(&HookRegister<2>);
        VirtualProtect(&pVtable[1], 2 * sizeof(void*), oldProtect, &oldProtect);
        spdlog::info("RenderPassWatch: BSBatchRenderer's register functions watched (they were {} and {})", Where(s_realRegister[0]), Where(s_realRegister[1]));
    });

static TiltedPhoques::Initializer s_drawListCheck(
    []()
    {
        const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pDraw = reinterpret_cast<const uint8_t*>(exe + kDrawList);
        if (!PatchCheck::Is(pDraw, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10}))
        {
            spdlog::error("RenderPassWatch: the list draw at {} is not the expected code ({}); lists are not checked for loops", fmt::ptr(pDraw), PatchCheck::Bytes(pDraw, 10));
            return;
        }
        s_realDrawList = reinterpret_cast<TDrawList*>(exe + kDrawList);
        TP_HOOK(&s_realDrawList, HookDrawList);
        spdlog::info("RenderPassWatch: each list is checked for a loop before it is drawn");
    });

static TiltedPhoques::Initializer s_accumulatorTrace(
    []()
    {
        CheckGameUnwindTable();

        const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* pCullerAppend = reinterpret_cast<const uint8_t*>(exe + kCullerAppend);
        const auto* pAppend = reinterpret_cast<const uint8_t*>(exe + kAccumulatorAppend);
        const auto* pWalk = reinterpret_cast<const uint8_t*>(exe + kAccumulatorWalk);
        if (!PatchCheck::Is(pCullerAppend, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44}) ||
            !PatchCheck::Is(pAppend, {0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40}) ||
            !PatchCheck::Is(pWalk, {0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57}))
        {
            spdlog::error("RenderPassWatch: the culler's append ({}), the accumulator's append ({}) or its walk ({}) is not the expected code; what feeds the laser "
                          "is not traced",
                          PatchCheck::Bytes(pCullerAppend, 9), PatchCheck::Bytes(pAppend, 9), PatchCheck::Bytes(pWalk, 9));
            return;
        }
        s_realCullerAppend = reinterpret_cast<TCullerAppend*>(exe + kCullerAppend);
        TP_HOOK(&s_realCullerAppend, HookCullerAppend);
        s_realAppend = reinterpret_cast<TAccumulatorAppend*>(exe + kAccumulatorAppend);
        TP_HOOK(&s_realAppend, HookAccumulatorAppend);
        s_realWalk = reinterpret_cast<TAccumulatorWalk*>(exe + kAccumulatorWalk);
        TP_HOOK(&s_realWalk, HookAccumulatorWalk);
        spdlog::info("RenderPassWatch: what feeds the laser to each accumulator is traced; full stacks {}", s_gameUnwindTable ? "on" : "off");
    });
#endif
