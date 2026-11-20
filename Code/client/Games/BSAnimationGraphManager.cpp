#include <TiltedOnlinePCH.h>

#include <BSAnimationGraphManager.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Havok/BShkbHkxDB.h>
#include <Havok/hkbBehaviorGraph.h>

#include <TiltedCore/Hash.hpp>

/**
 * List one graph's animation variables by number, and print them for the code's tables when asked.
 *
 * @param aPrintVariables True to print each variable as "kName = number," to the console.
 * @param aForceIndex Which graph, as for GetDescriptorKey; -1 to ask the manager.
 * @return Each variable's name by its number in the graph; empty when the graph cannot be read.
 */
SortedMap<uint32_t, String> BSAnimationGraphManager::DumpAnimationVariables(bool aPrintVariables, int aForceIndex)
{
    SortedMap<uint32_t, String> variables;
    for (const auto& [id, pName] : VariableNamesByNumber(aForceIndex))
        variables[id] = pName;

    if (aPrintVariables)
    {
        for (auto& [id, name] : variables)
        {
            std::cout << "k" << name << " = " << id << "," << std::endl;
        }
    }

    return variables;
}

/**
 * List one graph's animation variables by their number in the graph, as its database names them.
 *
 * @param aForceIndex Which graph, as for GetDescriptorKey; -1 to ask the manager.
 * @return Each variable's name by its number, in number order; empty when the graph or its tables cannot be read.
 */
std::map<uint32_t, const char*> BSAnimationGraphManager::VariableNamesByNumber(int aForceIndex)
{
    std::map<uint32_t, const char*> variables;
    const uint32_t cIndex = ResolveGraphIndex(aForceIndex);
    if (cIndex >= animationGraphs.size)
        return variables;
    const auto pGraph = animationGraphs.Get(cIndex);
    if (!pGraph)
        return variables;

    const auto pDb = pGraph->hkxDB;
    const auto pBuckets = pDb->animationVariables.buckets;
    const auto pVariableSet = pGraph->behaviorGraph->animationVariables;
    if (!pBuckets || !pVariableSet)
        return variables;

    // The names sit in a hash table: each filled bucket starts a chain that ends at the table's end marker.
    for (decltype(pDb->animationVariables.bucketCount) i = 0; i < pDb->animationVariables.bucketCount; ++i)
    {
        auto pBucket = &pBuckets[i];
        if (!pBucket->next)
            continue;

        while (pBucket != pDb->animationVariables.end)
        {
            // Only the variables the behaviour graph really has.
            const auto variableIndex = pBucket->value;
            if (pVariableSet->size > static_cast<uint32_t>(variableIndex))
                variables[variableIndex] = pBucket->key.AsAscii();

            pBucket = pBucket->next;
        }
    }
    return variables;
}

uint32_t BSAnimationGraphManager::ResolveGraphIndex(const int aForceIndex) const noexcept
{
    if (aForceIndex >= 0)
        return static_cast<uint32_t>(aForceIndex);

    if (animationGraphIndex < animationGraphs.size)
        return animationGraphIndex;

    // Out of range. With one graph in the list, that graph is the only possible answer.
    // With more than one, the value is handed back unchanged and the caller's own bounds check rejects it.
    if (animationGraphs.size == 1)
        return 0;

    return animationGraphIndex;
}

/**
 * Fingerprint one graph by its animation variables' names, joined in number order and lower-cased.
 * Copies use it to tell which graph a set of animation variables belongs to, so it must not change.
 *
 * @param aForceIndex Which graph; -1 to ask the manager. DumpAnimationVariables must be given the same one.
 * @return The names' CRC-64; the CRC of nothing when the graph cannot be read.
 */
uint64_t BSAnimationGraphManager::GetDescriptorKey(int aForceIndex)
{
    using TiltedPhoques::FHash::Crc64;

    String variableNames{};
    variableNames.reserve(8192);
    for (const auto& [id, pName] : VariableNamesByNumber(aForceIndex))
        variableNames += pName;

    std::transform(variableNames.begin(), variableNames.end(), variableNames.begin(), [](unsigned char c) { return std::tolower(c); });

    return Crc64(reinterpret_cast<const unsigned char*>(variableNames.c_str()), variableNames.size());
}

uint64_t BSAnimationGraphManager::GetProjectKey(int aForceIndex)
{
    using TiltedPhoques::FHash::Crc64;

    if (const uint32_t index = ResolveGraphIndex(aForceIndex); index < animationGraphs.size)
    {
        const auto pGraph = animationGraphs.Get(index);
        if (const char* pName = pGraph ? pGraph->projectName.AsAscii() : nullptr)
            return Crc64(reinterpret_cast<const unsigned char*>(pName), std::strlen(pName));
    }

    return 0;
}
