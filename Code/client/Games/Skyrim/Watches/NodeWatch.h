#pragma once

#ifdef SKYRIMVR
#include <cstddef>
#include <cstdint>

/**
 * What the node watches (BoneHold, EyeBoneWatch) share: a guarded read of a node's children, and which of their repeated lines are written.
 * VR only, like the watches.
 */
namespace NodeWatch
{
/**
 * The most child slots a watch looks at in one node.
 */
constexpr uint16_t kMaxChildren = 512;

/**
 * Read where a node keeps its children, without crashing when the node was freed.
 *
 * @param apNode The node.
 * @param aSlots Receives the slot array.
 * @param aCount Receives how many slots the array has, empty ones included.
 * @return True when both were read and the node has an array.
 */
bool ReadChildSlots(void* apNode, void**& aSlots, uint16_t& aCount) noexcept;

/**
 * Whether a watch writes its line for the n-th time something was seen: each of the first 20, then one in every 100.
 *
 * @param aNumber How many times it has been seen, counting this one.
 * @return True when this one is written.
 */
bool IsTold(size_t aNumber) noexcept;
} // namespace NodeWatch
#endif
