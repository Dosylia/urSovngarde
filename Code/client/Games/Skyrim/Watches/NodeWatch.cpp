#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR
#include <Games/Skyrim/Watches/NodeWatch.h>
#include <Games/Skyrim/NetImmerse/NiLayout.h>
#include <Helpers/GuardedRead.h>

namespace NodeWatch
{
namespace
{
constexpr size_t kToldEach = 20; // then one in every kToldEvery
constexpr size_t kToldEvery = 100;
} // namespace

/**
 * Read where a node keeps its children, without crashing when the node was freed.
 *
 * @param apNode The node.
 * @param aSlots Receives the slot array.
 * @param aCount Receives how many slots the array has, empty ones included.
 * @return True when both were read and the node has an array.
 */
bool ReadChildSlots(void* apNode, void**& aSlots, uint16_t& aCount) noexcept
{
    const auto* pBytes = static_cast<uint8_t*>(apNode);
    return GuardedRead::Value(pBytes + NiLayout::kChildSlotsOffset, aSlots) && GuardedRead::Value(pBytes + NiLayout::kChildCountOffset, aCount) && aSlots;
}

/**
 * Whether a watch writes its line for the n-th time something was seen: each of the first 20, then one in every 100.
 *
 * @param aNumber How many times it has been seen, counting this one.
 * @return True when this one is written.
 */
bool IsTold(const size_t aNumber) noexcept
{
    return aNumber <= kToldEach || aNumber % kToldEvery == 0;
}
} // namespace NodeWatch
#endif
