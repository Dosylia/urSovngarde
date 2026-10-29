#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Interface/UI.h>
#include <Interface/Menus/MapMenu.h>

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

template <size_t Slot>
uint64_t HookRegister(void* apRenderer, uint8_t* apPass, const uint64_t a3, const uint64_t a4)
{
    void* const cBefore = apPass ? *reinterpret_cast<void**>(apPass + kPassNext) : nullptr;
    const uint64_t cResult = s_realRegister[Slot - 1](apRenderer, apPass, a3, a4);
    if (!apPass)
        return cResult;

    const void* cCaller = _ReturnAddress();
    auto** ppNext = reinterpret_cast<void**>(apPass + kPassNext);
    if (*ppNext == apPass)
    {
        // Its next before this insert is its place in the list it was already on.
        *ppNext = cBefore == apPass ? nullptr : cBefore;
        ReportSelfLink(apRenderer, apPass, cCaller, Slot);
    }
    s_lastInserts[SlotOf(apPass)] = LastInsert{apPass, apRenderer, cCaller, GetCurrentThreadId(), MainLoopFrame()};
    return cResult;
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
#endif
