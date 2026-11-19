#include <TiltedOnlinePCH.h>

#include <Helpers/ReadableMemory.h>

#include <array>

namespace
{
/**
 * Check if Windows lets the game read memory with this protection.
 * Guard pages are refused: touching one raises an exception.
 *
 * @param aProtect The memory's protection flags, as Windows reports them.
 * @return True when the memory can be read.
 */
bool IsReadableProtection(const DWORD aProtect) noexcept
{
    if (aProtect & PAGE_GUARD)
        return false;

    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (aProtect & kReadable) != 0;
}

// The blocks found on this thread while a CacheScope is open, the oldest replaced first when it is full.
thread_local std::array<ReadableMemory::Region, 16> t_knownBlocks{};
thread_local size_t t_nextKnownBlock = 0;
// How many CacheScopes are open on this thread.
thread_local int t_openScopes = 0;
} // namespace

namespace ReadableMemory
{
/**
 * Check if a piece of memory lies entirely inside this block.
 *
 * @param aStart Address where the piece begins.
 * @param aSize Length of the piece, in bytes.
 * @return True when every byte of the piece is inside the block.
 */
bool Region::Holds(const uintptr_t aStart, const size_t aSize) const noexcept
{
    // Written as a subtraction, so a huge size cannot wrap around and pass.
    return aStart >= Begin && aStart < End && aSize <= End - aStart;
}

/**
 * Find the block of readable memory that an address belongs to.
 * Slow, as it asks Windows: not for every frame.
 *
 * @param apPointer The address to look up.
 * @return The block, or nothing when the address is not in readable memory.
 */
std::optional<Region> RegionAt(const void* apPointer) noexcept
{
    if (!apPointer)
        return std::nullopt;

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(apPointer, &info, sizeof(info)) == 0)
        return std::nullopt;

    if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect))
        return std::nullopt;

    const auto begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return Region{begin, begin + info.RegionSize};
}

/**
 * Check if a piece of memory can be read without crashing the game.
 * A piece that runs past the end of its block is refused, even when the next block is readable too.
 * While a CacheScope is open on this thread, blocks already found are reused instead of asking Windows again.
 *
 * @param apPointer Address where the piece begins.
 * @param aSize Length of the piece, in bytes.
 * @return True when the whole piece can be read.
 */
bool IsReadable(const void* apPointer, const size_t aSize) noexcept
{
    const auto start = reinterpret_cast<uintptr_t>(apPointer);
    const bool remembering = t_openScopes > 0;
    if (remembering)
    {
        for (const Region& known : t_knownBlocks)
            if (known.Holds(start, aSize))
                return true;
    }

    const auto region = RegionAt(apPointer);
    if (!region)
        return false;

    if (remembering)
        t_knownBlocks[t_nextKnownBlock++ % t_knownBlocks.size()] = *region;
    return region->Holds(start, aSize);
}

/**
 * Start remembering the blocks IsReadable finds on this thread.
 */
CacheScope::CacheScope() noexcept
{
    ++t_openScopes;
}

/**
 * Stop remembering, and forget every block, once the outermost scope closes.
 */
CacheScope::~CacheScope()
{
    if (--t_openScopes > 0)
        return;
    t_knownBlocks = {};
    t_nextKnownBlock = 0;
}
} // namespace ReadableMemory
