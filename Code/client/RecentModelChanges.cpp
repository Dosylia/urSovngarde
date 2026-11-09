#include <TiltedOnlinePCH.h>

#include <RecentModelChanges.h>

#include <Actor.h>

#include <atomic>
#include <chrono>

namespace RecentModelChanges
{
namespace
{
struct Entry
{
    uint64_t Address{};
    uint32_t FormId{};
    Kind What{};
    uint64_t Stamp{}; // steady_clock ticks
};

Entry s_ring[kCount]{};
std::atomic<uint32_t> s_next{0};

const char* KindName(const Kind aKind) noexcept
{
    switch (aKind)
    {
    case Kind::ItemsChanged: return "items changed";
    case Kind::Equip: return "equipped";
    case Kind::Unequip: return "unequipped";
    case Kind::WeaponDrawn: return "weapon drawn or sheathed";
    case Kind::Disable: return "switched off";
    case Kind::Enable: return "switched on";
    case Kind::Moved: return "moved";
    case Kind::Killed: return "killed";
    case Kind::Revived: return "revived";
    case Kind::Rebuilt: return "rebuilt";
    case Kind::Deleted: return "deleted";
    }
    return "?";
}
} // namespace

void Note(const Actor* apActor, const Kind aKind) noexcept
{
    if (!apActor)
        return;

    const uint64_t cAddress = reinterpret_cast<uint64_t>(apActor);
    const uint64_t cStamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (const uint32_t cWritten = s_next.load(std::memory_order_relaxed); cWritten != 0)
    {
        Entry& newest = s_ring[(cWritten - 1) % kCount];
        if (newest.Address == cAddress && newest.FormId == apActor->formID && newest.What == aKind)
        {
            newest.Stamp = cStamp;
            return;
        }
    }

    const uint32_t cSlot = s_next.fetch_add(1, std::memory_order_relaxed) % kCount;
    s_ring[cSlot] = Entry{cAddress, apActor->formID, aKind, cStamp};
}

void Report(const uint64_t* apRegisters, const size_t aRegisterCount, const uint64_t* apStack, const size_t aStackWords) noexcept
{
    constexpr uint32_t cNewestShown = 16;
    static const char* const kRegisterNames[] = {"Rax", "Rcx", "Rdx", "Rbx", "Rsi", "Rdi", "Rbp", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"};

    const uint32_t cWritten = s_next.load(std::memory_order_relaxed);
    if (cWritten == 0)
    {
        spdlog::error("RecentModelChanges: this client changed no actor's 3D this session.");
        return;
    }

    const uint64_t cNow = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const uint32_t cHeld = cWritten < kCount ? cWritten : static_cast<uint32_t>(kCount);
    spdlog::error(
        "RecentModelChanges: the last changes this client made to an actor's 3D, newest first, checked against the crashing registers and the crashing thread's stack ({} words).",
        aStackWords);

    bool matched = false;
    for (uint32_t i = 1; i <= cHeld; ++i)
    {
        const Entry& entry = s_ring[(cWritten - i) % kCount];
        if (!entry.Address)
            continue;

        int inRegister = -1;
        for (size_t r = 0; r < aRegisterCount && inRegister < 0; ++r)
            if (apRegisters[r] == entry.Address)
                inRegister = static_cast<int>(r);

        int64_t onStack = -1;
        for (size_t w = 0; w < aStackWords && onStack < 0; ++w)
            if (apStack[w] == entry.Address)
                onStack = static_cast<int64_t>(w);

        const bool cFound = inRegister >= 0 || onStack >= 0;
        matched = matched || cFound;
        if (i > cNewestShown && !cFound)
            continue;

        // steady_clock ticks are nanoseconds on MSVC; this only has to be readable, not exact.
        const uint64_t cAgo = cNow > entry.Stamp ? (cNow - entry.Stamp) / 1000000ull : 0ull;
        const char* pRegister = inRegister >= 0 && static_cast<size_t>(inRegister) < std::size(kRegisterNames) ? kRegisterNames[inRegister] : "a register";
        spdlog::error(
            "    {:#018x}  form {:X}  {}  {} ms ago{}", entry.Address, entry.FormId, KindName(entry.What), cAgo,
            inRegister >= 0 ? fmt::format("   <<<< IN {}", pRegister)
            : onStack >= 0  ? fmt::format("   <<<< ON THE CRASHING THREAD'S STACK at rsp+0x{:x}", static_cast<uint64_t>(onStack) * sizeof(uint64_t))
                            : std::string());
    }

    if (!matched)
        spdlog::error("    none of these actors is in a crashing register or on the crashing thread's stack.");
}
} // namespace RecentModelChanges
