#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/GameStack.h>

namespace
{
// The game's functions are not in our image's unwind table, so RtlCaptureStackBackTrace stops at the first one.
// The game's own table (SkyrimVR 1.4.15's exception directory) is in our image at its own RVA and is searched here.
constexpr uintptr_t kGameUnwindTable = 0x36F4000;
constexpr size_t kGameUnwindEntries = 0x15B058 / sizeof(RUNTIME_FUNCTION);
constexpr DWORD kGameCodeEnd = 0x1580000;
uintptr_t s_exe = 0;

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
bool CheckGameUnwindTable() noexcept
{
    s_exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* pEntries = reinterpret_cast<const RUNTIME_FUNCTION*>(s_exe + kGameUnwindTable);
    for (size_t i = 0; i < 64; ++i)
    {
        if (pEntries[i].BeginAddress >= pEntries[i].EndAddress || pEntries[i].EndAddress > kGameCodeEnd ||
            (i && pEntries[i].BeginAddress < pEntries[i - 1].EndAddress))
        {
            spdlog::error("GameStack: the game's unwind table does not read as one (entry {}); stacks stop at the first game frame", i);
            return false;
        }
    }
    return true;
}
} // namespace

namespace GameStack
{
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

bool Ready() noexcept
{
    static const bool s_ready = CheckGameUnwindTable();
    return s_ready;
}

__declspec(noinline) USHORT Capture(void** apFrames, const USHORT aMax) noexcept
{
    if (!Ready())
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

std::string Describe(void* const* apFrames, const USHORT aDepth) noexcept
{
    std::string stack;
    for (USHORT i = 0; i < aDepth; ++i)
        stack += (i ? " < " : "") + Where(apFrames[i]);
    return stack;
}
} // namespace GameStack
#endif
