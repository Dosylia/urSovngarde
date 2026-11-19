#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

/**
 * Checks that memory can be read before the code touches it.
 * Used for game objects reached through raw offsets, which may already be freed.
 * Each answer is a snapshot: another thread can still free the memory right after.
 */
namespace ReadableMemory
{
/**
 * A block of memory the game can read.
 * Begin is its first address; End is the first address after it.
 */
struct Region
{
    uintptr_t Begin = 0;
    uintptr_t End = 0;

    /**
     * Check if a piece of memory lies entirely inside this block.
     *
     * @param aStart Address where the piece begins.
     * @param aSize Length of the piece, in bytes.
     * @return True when every byte of the piece is inside the block.
     */
    bool Holds(uintptr_t aStart, size_t aSize) const noexcept;
};

/**
 * Find the block of readable memory that an address belongs to.
 * Slow, as it asks Windows: not for every frame.
 *
 * @param apPointer The address to look up.
 * @return The block, or nothing when the address is not in readable memory.
 */
std::optional<Region> RegionAt(const void* apPointer) noexcept;

/**
 * Check if a piece of memory can be read without crashing the game.
 * A piece that runs past the end of its block is refused, even when the next block is readable too.
 * While a CacheScope is open on this thread, blocks already found are reused instead of asking Windows again.
 *
 * @param apPointer Address where the piece begins.
 * @param aSize Length of the piece, in bytes.
 * @return True when the whole piece can be read.
 */
bool IsReadable(const void* apPointer, size_t aSize) noexcept;

/**
 * Speed up a burst of IsReadable checks on this thread, such as reading a whole skeleton.
 * While it is open, IsReadable remembers the blocks it finds and answers from them.
 * Everything remembered is forgotten when it closes, since memory can be freed at any time.
 * Opening one inside another changes nothing: the outer one keeps remembering.
 */
class CacheScope
{
public:
    /**
     * Start remembering the blocks IsReadable finds on this thread.
     */
    CacheScope() noexcept;

    /**
     * Stop remembering, and forget every block, once the outermost scope closes.
     */
    ~CacheScope();

    CacheScope(const CacheScope&) = delete;
    CacheScope& operator=(const CacheScope&) = delete;
};
} // namespace ReadableMemory
