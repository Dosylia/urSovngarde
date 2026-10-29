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
    void* Frames[14];
    USHORT Depth;
};

std::string StackOf(const PointerInsert& acInsert) noexcept
{
    std::string stack;
    for (USHORT i = 0; i < acInsert.Depth; ++i)
        stack += (i ? " < " : "") + Where(acInsert.Frames[i]);
    return stack;
}

// Each laser pass's first insert in one main loop frame, to name what puts it on a list a second time before the
// list is drawn, which a list it is already on turns into a loop. Inserts come from the render threads.
std::mutex s_pointerLock;
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
    insert.Depth = RtlCaptureStackBackTrace(2, static_cast<DWORD>(std::size(insert.Frames)), insert.Frames, nullptr);
    const uint64_t cFrame = MainLoopFrame();

    std::scoped_lock lock(s_pointerLock);

    if (std::find(s_pointerThreads, s_pointerThreads + s_pointerThreadCount, insert.Thread) == s_pointerThreads + s_pointerThreadCount &&
        s_pointerThreadCount < std::size(s_pointerThreads))
    {
        s_pointerThreads[s_pointerThreadCount++] = insert.Thread;
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
#endif
