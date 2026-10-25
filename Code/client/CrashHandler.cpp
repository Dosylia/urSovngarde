#include <RecentDeletes.h>
#include <BranchInfo.h>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <strsafe.h>
#include <Games/Memory.h>
#include <SmallDump.h>

using time_point = std::chrono::system_clock::time_point;

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

// Names a code address as "module+offset", or "<alloc base>+offset" for memory without a module.
// The game image is mapped by our own PE loader into private memory, so it has no module name.
static void DescribeCodeAddress(void* apAddress, char* apOut, size_t aOutSize)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(apAddress, &mbi, sizeof(mbi)))
    {
        _snprintf_s(apOut, aOutSize, _TRUNCATE, "<unqueryable>");
        return;
    }

    char name[MAX_PATH];
    name[0] = '\0';

    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(apAddress), &hModule) &&
        hModule && GetModuleFileNameA(hModule, name, sizeof(name)))
    {
        const char* pBase = strrchr(name, '\\');
        _snprintf_s(apOut, aOutSize, _TRUNCATE, "%s+0x%llx", pBase ? pBase + 1 : name,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(apAddress) -
                                                    reinterpret_cast<uintptr_t>(hModule)));
        return;
    }

    // The custom-loaded game image, or generated code (trampolines, hook thunks).
    _snprintf_s(apOut, aOutSize, _TRUNCATE, "<alloc 0x%llx>+0x%llx",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mbi.AllocationBase)),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(apAddress) -
                                                reinterpret_cast<uintptr_t>(mbi.AllocationBase)));
}

static bool IsExecutableAddress(void* apAddress)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(apAddress, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;

    const DWORD prot = mbi.Protect;
    if (prot & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    return (prot & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// The game image is mapped by our own loader, so Windows does not know it as a module.
// Same range as UI.cpp's DescribeCaller.
constexpr uintptr_t kGameImageBase = 0x140000000ull;
constexpr uintptr_t kGameImageSpan = 0x8000000ull;
#ifdef SKYRIMVR
constexpr char kGameImageName[] = "SkyrimVR.exe";
#else
constexpr char kGameImageName[] = "SkyrimSE.exe";
#endif

// Formats an address as "<module>+0x<offset>", like Crash Logger, for the launcher's crash report.
// Only the file name: a path would show the Windows user name.
// Returns false outside every module; apModule stays null for the game image.
static bool NameModuleOffset(uintptr_t aAddress, char* apOut, size_t aOutSize, HMODULE* apModule) noexcept
{
    if (aAddress >= kGameImageBase && aAddress < kGameImageBase + kGameImageSpan)
    {
        _snprintf_s(apOut, aOutSize, _TRUNCATE, "%s+0x%llx", kGameImageName, static_cast<unsigned long long>(aAddress - kGameImageBase));
        return true;
    }

    HMODULE hModule = nullptr;
    wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(aAddress), &hModule) || !hModule)
        return false;
    const DWORD length = GetModuleFileNameW(hModule, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return false;
    const wchar_t* pFile = wcsrchr(path, L'\\');
    char file[MAX_PATH * 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, pFile ? pFile + 1 : path, -1, file, sizeof(file), nullptr, nullptr))
        return false;

    _snprintf_s(apOut, aOutSize, _TRUNCATE, "%s+0x%llx", file, static_cast<unsigned long long>(aAddress - reinterpret_cast<uintptr_t>(hModule)));
    if (apModule)
        *apModule = hModule;
    return true;
}

// Reads a loaded module's file version from its resource in memory, without opening a file or allocating.
// Returns false when it has none.
static bool ModuleFileVersion(HMODULE aModule, char* apOut, size_t aOutSize) noexcept
{
    const HRSRC hResource = FindResourceW(aModule, MAKEINTRESOURCEW(1) /* VS_VERSION_INFO */, MAKEINTRESOURCEW(16) /* RT_VERSION */);
    if (!hResource)
        return false;
    const DWORD size = SizeofResource(aModule, hResource);
    const HGLOBAL hData = LoadResource(aModule, hResource);
    const auto* pData = hData ? static_cast<const uint8_t*>(LockResource(hData)) : nullptr;
    if (!pData)
        return false;

    // VS_FIXEDFILEINFO follows the "VS_VERSION_INFO" key on a 4-byte boundary, and starts with this signature.
    struct FixedFileInfo
    {
        DWORD Signature, StructVersion, FileVersionMS, FileVersionLS;
    };
    constexpr DWORD cSignature = 0xFEEF04BD;
    for (DWORD offset = 0; offset + sizeof(FixedFileInfo) <= size; offset += 4)
    {
        FixedFileInfo info;
        std::memcpy(&info, pData + offset, sizeof(info));
        if (info.Signature != cSignature)
            continue;
        _snprintf_s(apOut, aOutSize, _TRUNCATE, "%u.%u.%u.%u", HIWORD(info.FileVersionMS), LOWORD(info.FileVersionMS), HIWORD(info.FileVersionLS), LOWORD(info.FileVersionLS));
        return true;
    }
    return false;
}

// Captures the call stack inside a CrashGuard, so a broken stack faults into this __except instead of the crash report.
// Kept in its own function: MSVC allows no __try where C++ objects need unwinding.
static USHORT CaptureFramesGuarded(void** apFrames, ULONG aCount) noexcept
{
    CrashGuard::Enter();
    USHORT captured = 0;
    __try
    {
        captured = RtlCaptureStackBackTrace(0, aCount, apFrames, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        captured = 0;
    }
    CrashGuard::Leave();
    return captured;
}

namespace CrashPlace
{
namespace
{
// Two buffers: a note fills the hidden one, then shows it, so a crash never reads a half-written line.
char s_lines[2][384]{};
std::atomic<int> s_current{-1};
} // namespace

void Note(const char* acpLine) noexcept
{
    if (!acpLine)
        return;
    const int next = s_current.load(std::memory_order_relaxed) == 0 ? 1 : 0;
    strncpy_s(s_lines[next], acpLine, _TRUNCATE);
    s_current.store(next, std::memory_order_release);
}

const char* Last() noexcept
{
    const int current = s_current.load(std::memory_order_acquire);
    return current < 0 ? nullptr : s_lines[current];
}
} // namespace CrashPlace

// Logs which module crashed and the frames that led there, as "<module>+<offset>", for the launcher's crash report.
// Flushed at once, so the lines are on disk even if the process dies before the rest of the report.
static void LogCrashModule(PEXCEPTION_POINTERS apInfo) noexcept
{
    void* const pFault = apInfo->ExceptionRecord->ExceptionAddress;
    char where[MAX_PATH * 3 + 32];
    char version[64];
    HMODULE hModule = nullptr;

    if (!NameModuleOffset(reinterpret_cast<uintptr_t>(pFault), where, sizeof(where), &hModule))
        spdlog::error("VectoredExceptionHandler: in no module");
    else if (hModule && ModuleFileVersion(hModule, version, sizeof(version)))
        spdlog::error("VectoredExceptionHandler: in {}, version {}", where, version);
    else
        spdlog::error("VectoredExceptionHandler: in {}", where);

    // The walk starts in this handler; only the frames from the faulting address outwards are logged.
    // No line when the walk never reaches it.
    void* frames[62];
    const USHORT captured = CaptureFramesGuarded(frames, static_cast<ULONG>(std::size(frames)));
    USHORT first = 0;
    while (first < captured && frames[first] != pFault)
        ++first;
    if (first < captured)
    {
        char stack[1024];
        size_t used = 0;
        stack[0] = '\0';
        for (USHORT i = first; i < captured && i < first + 8; ++i)
        {
            char frame[MAX_PATH * 3 + 32];
            if (!NameModuleOffset(reinterpret_cast<uintptr_t>(frames[i]), frame, sizeof(frame), nullptr))
                _snprintf_s(frame, sizeof(frame), _TRUNCATE, "?");
            const int written = _snprintf_s(stack + used, sizeof(stack) - used, _TRUNCATE, "%s%s", i == first ? "" : " < ", frame);
            if (written < 0)
                break;
            used += static_cast<size_t>(written);
        }
        spdlog::error("VectoredExceptionHandler: stack {}", stack);
    }

    spdlog::default_logger()->flush();
}

// Logs registers and a raw stack scan, because minidumps of this process are mostly unusable.
// A real stack walk needs unwind info, which the custom-loaded game image does not have.
// Scanning the stack for pointers into executable memory recovers the return addresses, with some false positives.
static void LogCrashContext(PEXCEPTION_POINTERS pExceptionInfo)
{
    const auto* pRecord = pExceptionInfo->ExceptionRecord;
    const auto* pContext = pExceptionInfo->ContextRecord;
    char desc[MAX_PATH + 64];

    if (pRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRecord->NumberParameters >= 2)
    {
        const ULONG_PTR kind = pRecord->ExceptionInformation[0];
        spdlog::error("  faulting access: {} at 0x{:x}",
                      kind == 0 ? "read from" : (kind == 1 ? "write to" : "execute at"),
                      static_cast<uint64_t>(pRecord->ExceptionInformation[1]));
    }

    LogCrashModule(pExceptionInfo);
    if (const char* pPlace = CrashPlace::Last())
        spdlog::error("CrashPlace: {}", pPlace);

    DescribeCodeAddress(pRecord->ExceptionAddress, desc, sizeof(desc));
    spdlog::error("  rip 0x{:x}  ({})", static_cast<uint64_t>(pContext->Rip), desc);

    spdlog::error("  rax 0x{:016x}  rcx 0x{:016x}  rdx 0x{:016x}  rbx 0x{:016x}",
                  static_cast<uint64_t>(pContext->Rax), static_cast<uint64_t>(pContext->Rcx),
                  static_cast<uint64_t>(pContext->Rdx), static_cast<uint64_t>(pContext->Rbx));
    spdlog::error("  rsp 0x{:016x}  rbp 0x{:016x}  rsi 0x{:016x}  rdi 0x{:016x}",
                  static_cast<uint64_t>(pContext->Rsp), static_cast<uint64_t>(pContext->Rbp),
                  static_cast<uint64_t>(pContext->Rsi), static_cast<uint64_t>(pContext->Rdi));
    spdlog::error("  r8  0x{:016x}  r9  0x{:016x}  r10 0x{:016x}  r11 0x{:016x}",
                  static_cast<uint64_t>(pContext->R8), static_cast<uint64_t>(pContext->R9),
                  static_cast<uint64_t>(pContext->R10), static_cast<uint64_t>(pContext->R11));
    spdlog::error("  r12 0x{:016x}  r13 0x{:016x}  r14 0x{:016x}  r15 0x{:016x}",
                  static_cast<uint64_t>(pContext->R12), static_cast<uint64_t>(pContext->R13),
                  static_cast<uint64_t>(pContext->R14), static_cast<uint64_t>(pContext->R15));

    // Stay inside the thread's stack: the guard page below StackLimit would fault.
    const auto* pTib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    auto* pCursor = reinterpret_cast<uintptr_t*>(pContext->Rsp & ~static_cast<uintptr_t>(7));
    const auto* pStackTop = reinterpret_cast<const uintptr_t*>(pTib->StackBase);
    const auto* pStackLimit = reinterpret_cast<const uintptr_t*>(pTib->StackLimit);

    if (pCursor < pStackLimit || pCursor >= pStackTop)
    {
        spdlog::error("  stack scan skipped: rsp 0x{:x} outside thread stack [0x{:x}, 0x{:x})",
                      static_cast<uint64_t>(pContext->Rsp), reinterpret_cast<uint64_t>(pStackLimit),
                      reinterpret_cast<uint64_t>(pStackTop));
        return;
    }

    spdlog::error("  stack scan (rsp 0x{:x} .. 0x{:x}), executable addresses only:",
                  reinterpret_cast<uint64_t>(pCursor), reinterpret_cast<uint64_t>(pStackTop));

    constexpr size_t kMaxSlots = 4096; // 32KB of stack
    constexpr size_t kMaxHits = 48;

    size_t hits = 0;
    for (size_t slot = 0; slot < kMaxSlots && pCursor < pStackTop && hits < kMaxHits; ++slot, ++pCursor)
    {
        auto* pCandidate = reinterpret_cast<void*>(*pCursor);
        if (!IsExecutableAddress(pCandidate))
            continue;

        DescribeCodeAddress(pCandidate, desc, sizeof(desc));
        spdlog::error("    [rsp+0x{:04x}] 0x{:012x}  {}", slot * sizeof(uintptr_t),
                      reinterpret_cast<uint64_t>(pCandidate), desc);
        ++hits;
    }

    if (!hits)
        spdlog::error("    (no executable addresses found - stack likely corrupted)");
}

namespace
{
//! A stand-in for the missing actor the crime alarm walks into: every field reads zero and every virtual call returns zero.
//! Built once, on first use.
void* NullActorDecoy() noexcept
{
    static void* s_pDecoy = []() -> void*
    {
        auto* pStub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!pStub)
            return nullptr;
        pStub[0] = 0x31; // xor eax, eax
        pStub[1] = 0xC0;
        pStub[2] = 0xC3; // ret            (the caller cleans up, so this is safe whatever the arguments were)
        DWORD previous = 0;
        if (!VirtualProtect(pStub, 0x1000, PAGE_EXECUTE_READ, &previous))
            return nullptr;

        static void* s_vtable[512];
        for (auto& entry : s_vtable)
            entry = pStub;

        static uint8_t s_object[0x1000]{};
        *reinterpret_cast<void**>(s_object) = s_vtable;
        return s_object;
    }();
    return s_pDecoy;
}
} // namespace

namespace CrashGuard
{
static thread_local int s_depth = 0;
void Enter() noexcept
{
    ++s_depth;
}
void Leave() noexcept
{
    if (s_depth > 0)
        --s_depth;
}
bool Inside() noexcept
{
    return s_depth > 0;
}
} // namespace CrashGuard

// Adds to the dump a few kilobytes around each register that looks like a pointer, plus the vtable each points to.
// A normal minidump has no heap, so the object a faulting call was made on would be missing.
// A full-memory dump is about 7 GB, and indirect memory gives truncated dumps on this process.
namespace
{
struct ExtraRange
{
    ULONG64 Base;
    ULONG Size;
};

ExtraRange g_extraRanges[24]{};
size_t g_extraCount = 0;
size_t g_extraNext = 0;

bool RangeIsReadable(ULONG64 aAddress, SIZE_T aSize) noexcept
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(aAddress), &info, sizeof(info)))
        return false;
    if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) || (info.Protect & PAGE_NOACCESS))
        return false;
    return aAddress + aSize <= reinterpret_cast<ULONG64>(info.BaseAddress) + info.RegionSize;
}

void AddExtraRange(ULONG64 aAddress, ULONG aSize) noexcept
{
    if (g_extraCount >= std::size(g_extraRanges) || aAddress < 0x10000)
        return;
    // Page-align downwards so the whole object is caught whatever the pointer pointed into.
    const ULONG64 base = aAddress & ~static_cast<ULONG64>(0xFFF);
    for (size_t i = 0; i < g_extraCount; ++i)
        if (g_extraRanges[i].Base == base)
            return;
    if (!RangeIsReadable(base, aSize))
        return;
    g_extraRanges[g_extraCount++] = {base, aSize};
}

void CollectCrashObjects(PEXCEPTION_POINTERS apInfo) noexcept
{
    g_extraCount = 0;
    g_extraNext = 0;
    if (!apInfo || !apInfo->ContextRecord)
        return;

    const CONTEXT& c = *apInfo->ContextRecord;
    const ULONG64 candidates[] = {c.Rcx, c.Rdx, c.Rbx, c.Rsi, c.Rdi, c.Rbp, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15};

    for (const ULONG64 value : candidates)
    {
        AddExtraRange(value, 0x1000);

        // Also what its first qword points at: for an object that is its vtable, null or foreign once freed.
        if (!RangeIsReadable(value, sizeof(ULONG64)))
            continue;
        ULONG64 first = 0;
        std::memcpy(&first, reinterpret_cast<const void*>(value), sizeof(first));
        AddExtraRange(first, 0x1000);
    }
}

BOOL CALLBACK CrashDumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT apInput, PMINIDUMP_CALLBACK_OUTPUT apOutput) noexcept
{
    if (!apInput || !apOutput)
        return FALSE;

    if (apInput->CallbackType == MemoryCallback)
    {
        if (g_extraNext >= g_extraCount)
        {
            apOutput->MemoryBase = 0;
            apOutput->MemorySize = 0;
            return FALSE; // done adding
        }
        apOutput->MemoryBase = g_extraRanges[g_extraNext].Base;
        apOutput->MemorySize = g_extraRanges[g_extraNext].Size;
        ++g_extraNext;
        return TRUE;
    }

    return TRUE;
}
} // namespace

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    // A fault inside a guarded section is recovered further up this thread's stack, so it is not reported.
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && CrashGuard::Inside())
        return EXCEPTION_CONTINUE_SEARCH;

    // The crime alarm (Actor::StealAlarm) can meet an empty actor slot and read its flags at +0xE8 through a null RCX.
    // That code was relocated by a hook and has no unwind data, so no __try can catch it.
    // The read gets a zeroed stand-in and the walk finishes; what leaves the slot empty is unknown.
    {
        const auto& record = *pExceptionInfo->ExceptionRecord;
        static std::atomic<uint32_t> s_recovered{0};
        constexpr uint32_t cMaxRecoveries = 64; // a genuine loop still surfaces instead of being papered over
        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2 &&
            record.ExceptionInformation[0] == 0 /* a read */ && record.ExceptionInformation[1] == 0xE8 /* of null + 0xE8 */ &&
            pExceptionInfo->ContextRecord->Rcx == 0)
        {
            const uint32_t count = ++s_recovered;
            void* pDecoy = count <= cMaxRecoveries ? NullActorDecoy() : nullptr;
            if (pDecoy)
            {
                if (count <= 5)
                    spdlog::warn("Recovered from the crime alarm's missing actor (read of null+0xE8 at {:#x}, occurrence {}). The session continues; the crime may not have been reported.",
                                 static_cast<uint64_t>(pExceptionInfo->ContextRecord->Rip), count);
                pExceptionInfo->ContextRecord->Rcx = reinterpret_cast<DWORD64>(pDecoy);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    // One report per exception type, so a recovered access violation does not use up the one for a /GS failure.
    static int alreadyCrashedAV = 0;
    static int alreadyCrashedGS = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize
    static std::mutex singleThreaded;
    const std::lock_guard lock{singleThreaded};

    const auto exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
    const bool isNewAV = exceptionCode == EXCEPTION_ACCESS_VIOLATION && alreadyCrashedAV++ == 0;
    const bool isNewGS = exceptionCode == STATUS_STACK_BUFFER_OVERRUN && alreadyCrashedGS++ == 0;

    // Access violations, and /GS stack cookie failures (STATUS_STACK_BUFFER_OVERRUN).
    if (isNewAV || isNewGS)
    {
        spdlog::critical (__FUNCTION__ ": crash occurred!"); 
        
        spdlog::error(__FUNCTION__ ": exception code is {:x}, at address {}, flags {:x} ",
                      pExceptionInfo->ExceptionRecord->ExceptionCode,
                      pExceptionInfo->ExceptionRecord->ExceptionAddress,
                      pExceptionInfo->ExceptionRecord->ExceptionFlags);

        LogCrashContext(pExceptionInfo);

        // Checks the registers against the objects we deleted; Rcx holds `this` for a virtual call on x64.
        if (pExceptionInfo && pExceptionInfo->ContextRecord)
        {
            const CONTEXT& cr = *pExceptionInfo->ContextRecord;
            const uint64_t regs[] = {cr.Rax, cr.Rcx, cr.Rdx, cr.Rbx, cr.Rsi, cr.Rdi, cr.Rbp,
                                     cr.R8,  cr.R9,  cr.R10, cr.R11, cr.R12, cr.R13, cr.R14, cr.R15};
            RecentDeletes::Report(regs, std::size(regs));
        }

        // The small dump first, in every build: it is the one a player can send with a report.
        // It holds stacks, registers, modules and the objects the crashing registers point at, not the game's data.
        try
        {
            CollectCrashObjects(pExceptionInfo);
            MINIDUMP_CALLBACK_INFORMATION smallCallback{};
            smallCallback.CallbackRoutine = &CrashDumpCallback;
            const auto logs = TiltedPhoques::GetPath() / "logs";
            const auto name = "crash_" + SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S") + ".small.dmp";
            DWORD smallError = 0;
            // A second, smaller pass (when the first is too big to send) needs the crash objects handed out again.
            const uint64_t smallSize = SmallDump::Write(logs / name, pExceptionInfo, &smallCallback, smallError, []() noexcept { g_extraNext = 0; });
            if (smallSize)
            {
                spdlog::critical(__FUNCTION__ ": small crash dump {} ({} KB)", name, smallSize / 1024);
                SmallDump::Prune(logs);
            }
            else
                spdlog::critical(__FUNCTION__ ": small crash dump could not be written (error {:#x})", smallError);
        }
        catch (...) // best effort, like the full dump
        {
        }

#if (IS_MASTER)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = INVALID_HANDLE_VALUE;
            BOOL dumpWritten = FALSE;
            DWORD dumpError = 0;
            try
            {
                MINIDUMP_EXCEPTION_INFORMATION M;
                char dumpPath[MAX_PATH];

                M.ThreadId = GetCurrentThreadId();
                M.ExceptionPointers = pExceptionInfo;
                M.ClientPointers = 0;

                std::ostringstream oss;
                oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S")
                    << ".dmp";

                GetModuleFileNameA(NULL, dumpPath, sizeof(dumpPath));
                std::filesystem::path modulePath(dumpPath);
                auto subPath = modulePath.parent_path();

                CrashHandler::RemovePreviousDump(subPath);

                subPath /= oss.str();

                hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, NULL);
                if (hDumpFile == INVALID_HANDLE_VALUE)
                    dumpError = GetLastError();

                // baseline settings from https://stackoverflow.com/a/63123214/5273909
                // Larger dump types fail on this process because of the game image's 1 GB+ buffer.
                // DataSegs is about 1 GB but holds the decrypted game code, the only copy that can be disassembled.
                auto dumpSettings = MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithThreadInfo;

                if (hDumpFile != INVALID_HANDLE_VALUE)
                {
                    CollectCrashObjects(pExceptionInfo);
                    MINIDUMP_CALLBACK_INFORMATION callback{};
                    callback.CallbackRoutine = &CrashDumpCallback;
                    callback.CallbackParam = nullptr;

                    dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile,
                                                    (MINIDUMP_TYPE)dumpSettings, (pExceptionInfo) ? &M : NULL, NULL,
                                                    &callback);
                    if (!dumpWritten)
                        dumpError = GetLastError();
                }
            }
            catch (...) // Mini-dump is best effort only.
            {
            }

            // CreateFileA fails with INVALID_HANDLE_VALUE, not NULL.
            if (hDumpFile == INVALID_HANDLE_VALUE)
                spdlog::critical(__FUNCTION__ ": coredump file could not be created (error {}).", dumpError);
            else
            {
                CloseHandle(hDumpFile);
                if (dumpWritten)
                    spdlog::critical(__FUNCTION__ ": coredump created -> flush logs.");
                else
                    spdlog::critical(__FUNCTION__ ": MiniDumpWriteDump failed (error {:#x}) -> rely on the logged "
                                     "crash context above.",
                                     dumpError);
            }
        }

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable), 
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical(__FUNCTION__ ": UnhandledExceptionFilter() workaround triggered.");

            singleThreaded.unlock();        // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }

        // A flush, not spdlog::shutdown(): shutdown drops the logger, and every later log line would then fault.
        spdlog::default_logger()->flush();
    }
    return retval;
}

namespace
{
// Cleared by an exit handler registered after spdlog's registry exists, so it runs before the logger is destroyed.
std::atomic<bool> s_loggerAlive{false};

struct SevereCode
{
    DWORD Code;
    const char* Name;
};

// Exceptions that usually end the process but get no crash report.
// The crash report covers access violations and /GS failures.
// Code 0 marks the CRT's last stops, which end the process without an exception.
constexpr SevereCode cSevereCodes[] = {
    {0xC0000374, "heap corruption"},
    {0xC00000FD, "stack overflow"},
    {0xC0000006, "in-page error"},
    {0xC000001D, "illegal instruction"},
    {0xC0000094, "integer divide by zero"},
    {0xC0000096, "privileged instruction"},
    {0, "std::terminate was called (an uncaught C++ exception or a noexcept violation)"},
    {0, "a CRT function was given an invalid parameter"},
    {0, "a pure virtual function was called"},
};
constexpr size_t cTerminateIndex = 6;
constexpr size_t cInvalidParameterIndex = 7;
constexpr size_t cPurecallIndex = 8;
std::atomic<bool> s_severeReported[std::size(cSevereCodes)]{};

// The watchdog thread writes the line, so the faulting thread needs little stack and takes no lock.
// A stack overflow leaves too little stack to format it, and a heap corruption may come from inside the logger.
// A slot is pending while its thread id is set.
std::atomic<uintptr_t> s_severeAddress[std::size(cSevereCodes)]{};
std::atomic<DWORD> s_severeThread[std::size(cSevereCodes)]{};
std::atomic<HANDLE> s_watchdogWake{nullptr};
std::atomic<DWORD> s_watchdogThreadId{0};

void LogSevereException(const SevereCode& acCode, const uintptr_t aAddress, const DWORD aThreadId) noexcept
{
    char line[MAX_PATH * 3 + 160];
    if (acCode.Code == 0)
    {
        _snprintf_s(line, sizeof(line), _TRUNCATE, "SevereException: %s, thread %lu", acCode.Name, aThreadId);
        CrashLog::Line(line);
        return;
    }
    char where[MAX_PATH * 3 + 32];
    if (!NameModuleOffset(aAddress, where, sizeof(where), nullptr))
        _snprintf_s(where, sizeof(where), _TRUNCATE, "0x%llx (no module)", static_cast<unsigned long long>(aAddress));
    _snprintf_s(line, sizeof(line), _TRUNCATE, "SevereException: 0x%08lX %s at %s, thread %lu (once per kind; it may still be handled)", acCode.Code, acCode.Name, where,
                aThreadId);
    CrashLog::Line(line);
}

// Called by the watchdog thread to write the lines the handler left.
void LogPendingSevereExceptions() noexcept
{
    for (size_t i = 0; i < std::size(cSevereCodes); ++i)
    {
        if (const DWORD cThread = s_severeThread[i].load())
        {
            LogSevereException(cSevereCodes[i], s_severeAddress[i].load(), cThread);
            s_severeThread[i].store(0);
        }
    }
}

// Leaves the line for the watchdog, once per kind and session.
// Waits up to a second for it to reach the disk, since the process may end right after.
void ReportSevere(const size_t aIndex, const uintptr_t aAddress) noexcept
{
    HANDLE hWake = s_watchdogWake.load();
    if (!hWake || !s_loggerAlive.load() || s_severeReported[aIndex].exchange(true))
        return;

    s_severeAddress[aIndex].store(aAddress);
    s_severeThread[aIndex].store(GetCurrentThreadId());
    SetEvent(hWake);
    if (GetCurrentThreadId() != s_watchdogThreadId.load())
        for (int i = 0; i < 100 && s_severeThread[aIndex].load(); ++i)
            Sleep(10);
}

// One line per kind and session for the exceptions above, then the search goes on as if this handler did not exist.
// A first-chance exception the program handles itself is also logged, once.
// C++ exceptions and breakpoints are not in the list.
LONG WINAPI SevereExceptionHandler(PEXCEPTION_POINTERS apInfo) noexcept
{
    const DWORD cCode = apInfo->ExceptionRecord->ExceptionCode;
    for (size_t i = 0; i < std::size(cSevereCodes); ++i)
    {
        if (cCode != 0 && cSevereCodes[i].Code == cCode)
        {
            ReportSevere(i, reinterpret_cast<uintptr_t>(apInfo->ExceptionRecord->ExceptionAddress));
            break;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// The CRT's last stops each leave one line, then do what would have happened without them.
std::terminate_handler s_previousTerminate = nullptr;
_invalid_parameter_handler s_previousInvalidParameter = nullptr;
_purecall_handler s_previousPurecall = nullptr;

void OnTerminate()
{
    ReportSevere(cTerminateIndex, 0);
    if (s_previousTerminate)
        s_previousTerminate();
    std::abort();
}

void OnInvalidParameter(const wchar_t* apExpression, const wchar_t* apFunction, const wchar_t* apFile, const unsigned int aLine, const uintptr_t aReserved)
{
    ReportSevere(cInvalidParameterIndex, 0);
    if (s_previousInvalidParameter)
        s_previousInvalidParameter(apExpression, apFunction, apFile, aLine, aReserved);
    else
        _invoke_watson(apExpression, apFunction, apFile, aLine, aReserved);
}

void OnPurecall()
{
    ReportSevere(cPurecallIndex, 0);
    if (s_previousPurecall)
        s_previousPurecall();
}
} // namespace

void CrashLog::Line(const char* acpLine, const spdlog::level::level_enum aLevel) noexcept
{
    if (!acpLine || !s_loggerAlive.load(std::memory_order_acquire))
        return;
    if (spdlog::logger* pLogger = spdlog::default_logger_raw())
    {
        pLogger->log(aLevel, "{}", acpLine);
        pLogger->flush();
    }
}

namespace FreezeWatchdog
{
namespace
{
std::atomic<int64_t> s_lastBeatMs{0};
std::atomic<DWORD> s_updateThreadId{0};

int64_t NowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The game's main thread: the oldest thread of the process (the launcher's, which runs the game's WinMain).
DWORD FindMainThreadId() noexcept
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;
    DWORD oldestId = 0;
    ULONGLONG oldestTime = ~0ull;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(hSnapshot, &entry); ok; ok = Thread32Next(hSnapshot, &entry))
    {
        if (entry.th32OwnerProcessID != GetCurrentProcessId())
            continue;
        HANDLE hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!hThread)
            continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetThreadTimes(hThread, &created, &exited, &kernel, &user))
        {
            const ULONGLONG cTime = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (cTime < oldestTime)
            {
                oldestTime = cTime;
                oldestId = entry.th32ThreadID;
            }
        }
        CloseHandle(hThread);
    }
    CloseHandle(hSnapshot);
    return oldestId;
}

uint32_t CountThreads() noexcept
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return 0;
    uint32_t count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(hSnapshot, &entry); ok; ok = Thread32Next(hSnapshot, &entry))
        if (entry.th32OwnerProcessID == GetCurrentProcessId())
            ++count;
    CloseHandle(hSnapshot);
    return count;
}

// Logs where a thread is, suspending it only while its registers and stack top are copied into an existing buffer.
// Nothing is allocated or logged while it is suspended: it may hold the heap lock or the logger's.
// ReadProcessMemory fails instead of faulting on a missing page.
void LogThread(const char* apWhat, const DWORD aThreadId) noexcept
{
    if (!aThreadId || aThreadId == GetCurrentThreadId())
        return;
    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, aThreadId);
    if (!hThread)
    {
        spdlog::error("FreezeWatchdog: could not open the {} (thread {})", apWhat, aThreadId);
        return;
    }

    static uintptr_t s_stack[4096]; // 32 KB, as much as the crash report scans
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    SIZE_T copied = 0;
    bool haveContext = false;
    if (SuspendThread(hThread) != static_cast<DWORD>(-1))
    {
        haveContext = GetThreadContext(hThread, &context) != FALSE;
        if (haveContext)
        {
            // Shrink the window until it reads: the stack ends somewhere above rsp.
            for (SIZE_T size = sizeof(s_stack); size >= 4096 && copied == 0; size /= 2)
                if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(context.Rsp & ~static_cast<DWORD64>(7)), s_stack, size, &copied))
                    copied = 0;
        }
        ResumeThread(hThread);
    }
    CloseHandle(hThread);

    if (!haveContext)
    {
        spdlog::error("FreezeWatchdog: could not read where the {} is (thread {})", apWhat, aThreadId);
        return;
    }

    char desc[MAX_PATH + 64];
    DescribeCodeAddress(reinterpret_cast<void*>(context.Rip), desc, sizeof(desc));
    spdlog::error("FreezeWatchdog: the {} (thread {}) is at rip 0x{:x} ({})", apWhat, aThreadId, static_cast<uint64_t>(context.Rip), desc);
    spdlog::error("  rax 0x{:016x}  rcx 0x{:016x}  rdx 0x{:016x}  rbx 0x{:016x}  rsp 0x{:016x}", static_cast<uint64_t>(context.Rax),
                  static_cast<uint64_t>(context.Rcx), static_cast<uint64_t>(context.Rdx), static_cast<uint64_t>(context.Rbx), static_cast<uint64_t>(context.Rsp));
    size_t hits = 0;
    for (size_t slot = 0; slot < copied / sizeof(uintptr_t) && hits < 40; ++slot)
    {
        auto* pCandidate = reinterpret_cast<void*>(s_stack[slot]);
        if (!IsExecutableAddress(pCandidate))
            continue;
        DescribeCodeAddress(pCandidate, desc, sizeof(desc));
        spdlog::error("    [rsp+0x{:04x}] 0x{:012x}  {}", slot * sizeof(uintptr_t), reinterpret_cast<uint64_t>(pCandidate), desc);
        ++hits;
    }
}

// Logs where this process's threads started, most common first: a start address names the module that owns a thread.
void LogThreadStarts(const uint32_t aCount) noexcept
{
    using NtQueryInformationThread_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto s_query = reinterpret_cast<NtQueryInformationThread_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    if (!s_query)
        return;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return;
    std::unordered_map<uintptr_t, uint32_t> starts;
    std::unordered_map<uintptr_t, std::vector<DWORD>> samples; // a few thread ids per start address
    uint32_t unopened = 0;
    LONG lastStatus = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(hSnapshot, &entry); ok; ok = Thread32Next(hSnapshot, &entry))
    {
        if (entry.th32OwnerProcessID != GetCurrentProcessId())
            continue;
        // The full query right: with THREAD_QUERY_LIMITED_INFORMATION the start address is refused.
        HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!hThread)
        {
            ++unopened;
            continue;
        }
        uintptr_t start = 0;
        constexpr ULONG cThreadQuerySetWin32StartAddress = 9;
        if (const LONG cStatus = s_query(hThread, cThreadQuerySetWin32StartAddress, &start, sizeof(start), nullptr); cStatus == 0)
        {
            ++starts[start];
            auto& ids = samples[start];
            if (ids.size() < 3)
                ids.push_back(entry.th32ThreadID);
            else
                ids[2] = entry.th32ThreadID; // the last one listed, likely among the newest
        }
        else
        {
            ++unopened;
            lastStatus = cStatus;
        }
        CloseHandle(hThread);
    }
    CloseHandle(hSnapshot);

    std::vector<std::pair<uintptr_t, uint32_t>> sorted(starts.begin(), starts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string line;
    char desc[MAX_PATH + 64];
    for (size_t i = 0; i < sorted.size() && i < 6; ++i)
    {
        DescribeCodeAddress(reinterpret_cast<void*>(sorted[i].first), desc, sizeof(desc));
        line += fmt::format("{}{} x{}", i ? ", " : "", desc, sorted[i].second);
    }
    spdlog::warn("ThreadStarts: {} threads from {} start addresses ({} not readable, last status 0x{:X}); most: {}", aCount, sorted.size(), unopened,
                 static_cast<uint32_t>(lastStatus), line);

    // The biggest group may start in ucrtbase's thread starter, which names no owner.
    // A sample thread's stack then shows the module that started it.
    if (!sorted.empty() && sorted[0].second >= 100)
    {
        const auto& ids = samples[sorted[0].first];
        for (size_t i = ids.size() > 2 ? ids.size() - 2 : 0; i < ids.size(); ++i)
            LogThread("sample thread of the biggest group", ids[i]);
    }
}

void LogHealth(const int64_t aSinceBeatMs) noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    const bool cHaveMemory = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE;
    spdlog::info("Health: private {} MB, working set {} MB, {} threads, last frame {} ms ago", cHaveMemory ? counters.PrivateUsage / (1024 * 1024) : 0,
                 cHaveMemory ? counters.WorkingSetSize / (1024 * 1024) : 0, CountThreads(), aSinceBeatMs);
}

DWORD WINAPI WatchdogMain(LPVOID) noexcept
{
    int64_t nextHealthMs = NowMs() + 30000;
    int64_t reportedForBeat = -1;
    int reportsForBeat = 0;
    for (;;)
    {
        if (WaitForSingleObject(s_watchdogWake.load(), 1000) == WAIT_FAILED)
            Sleep(1000);
        LogPendingSevereExceptions();
        // While the game exits only the lines above are written.
        if (IsProcessExiting())
            continue;
        const int64_t cBeat = s_lastBeatMs.load(std::memory_order_relaxed);
        if (!cBeat)
            continue; // the game has not run a frame of ours yet
        const int64_t cNow = NowMs();
        const int64_t cSince = cNow - cBeat;

        if (cNow >= nextHealthMs)
        {
            nextHealthMs = cNow + 30000;
            LogHealth(cSince);

            // Every 5 minutes, and only once the count is past what a normal game has (about 200).
            static int64_t s_nextThreadStartsMs = 0;
            if (cNow >= s_nextThreadStartsMs)
            {
                if (const uint32_t cThreads = CountThreads(); cThreads > 500)
                {
                    s_nextThreadStartsMs = cNow + 300000;
                    LogThreadStarts(cThreads);
                }
            }
        }

        // Once at 25 s and once more at 2 minutes for the same stall. A long loading screen also gets here.
        if (cBeat != reportedForBeat)
        {
            reportedForBeat = cBeat;
            reportsForBeat = 0;
        }
        const int64_t cThreshold = reportsForBeat == 0 ? 25000 : 120000;
        if (cSince < cThreshold || reportsForBeat >= 2)
            continue;
        ++reportsForBeat;
        spdlog::error("FreezeWatchdog: no frame of ours for {} s (a long loading screen does this too); where the game is:", cSince / 1000);
        LogHealth(cSince);
        const DWORD cMain = FindMainThreadId();
        LogThread("game's main thread", cMain);
        const DWORD cUpdate = s_updateThreadId.load(std::memory_order_relaxed);
        if (cUpdate != cMain)
            LogThread("thread that last ran our update", cUpdate);

        // Every thread's stack, once per stall: a main thread waiting says nothing about what it waits for.
        // One file, overwritten by the next freeze, so freezes never fill the folder.
        if (reportsForBeat == 1)
        {
            const auto cPath = TiltedPhoques::GetPath() / "logs" / "freeze.small.dmp";
            DWORD error = 0;
            // No size target: past it only the writing thread's stack is kept, and that is this watchdog's.
            if (const uint64_t cSize = SmallDump::Write(cPath, nullptr, nullptr, error, nullptr, UINT64_MAX))
                spdlog::error("FreezeWatchdog: every thread's stack written to logs\\freeze.small.dmp ({} KB)", cSize / 1024);
            else
                spdlog::error("FreezeWatchdog: the freeze dump could not be written (error {:#x})", error);
        }
        spdlog::default_logger()->flush();
    }
}
} // namespace

void Beat() noexcept
{
    s_lastBeatMs.store(NowMs(), std::memory_order_relaxed);
    s_updateThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

void Start() noexcept
{
    s_watchdogWake.store(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    DWORD threadId = 0;
    if (HANDLE hWatchdog = CreateThread(nullptr, 0, &WatchdogMain, nullptr, 0, &threadId))
    {
        s_watchdogThreadId.store(threadId);
        CloseHandle(hWatchdog);
    }
    else
        s_watchdogWake.store(nullptr);
}
} // namespace FreezeWatchdog

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);

    // Added last so it runs first, before the crash report's larger frame meets a stack overflow.
    AddVectoredExceptionHandler(1, &SevereExceptionHandler);
    s_previousTerminate = std::set_terminate(&OnTerminate);
    s_previousInvalidParameter = _set_invalid_parameter_handler(&OnInvalidParameter);
    s_previousPurecall = _set_purecall_handler(&OnPurecall);

    // Creating the registry first makes this exit handler run before the registry is destroyed.
    static_cast<void>(spdlog::default_logger_raw());
    s_loggerAlive.store(true);
    std::atexit([] { s_loggerAlive.store(false); });

    FreezeWatchdog::Start();
}

CrashHandler::~CrashHandler()
{
}

void CrashHandler::RemovePreviousDump(std::filesystem::path path)
{
    // This is the game folder (the launcher spoofs the module path), so only delete our own dumps.
    for (auto& entry : std::filesystem::directory_iterator(path))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.rfind("crash_UTC_", 0) == 0 && entry.path().extension() == ".dmp")
        {
            DeleteFileA(entry.path().string().c_str());
        }
    }
}
