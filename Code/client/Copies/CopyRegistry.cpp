#include <TiltedOnlinePCH.h>

#include <Copies/CopyRegistry.h>

namespace
{
std::unordered_set<uint32_t> s_ownCopies;
TiltedPhoques::Map<uint32_t, uint32_t> s_ghosts;
} // namespace

namespace CopyRegistry
{
bool IsOwnCopy(const uint32_t aFormId) noexcept
{
    return s_ownCopies.find(aFormId) != s_ownCopies.end();
}

void AddOwnCopy(const uint32_t aFormId) noexcept
{
    s_ownCopies.insert(aFormId);
}

void ForgetOwnCopy(const uint32_t aFormId) noexcept
{
    s_ownCopies.erase(aFormId);
}

const std::unordered_set<uint32_t>& OwnCopies() noexcept
{
    return s_ownCopies;
}

void ForgetAllOwnCopies() noexcept
{
    s_ownCopies.clear();
}

void SetStandIn(const uint32_t aGhost, const uint32_t aCopy) noexcept
{
    s_ghosts[aGhost] = aCopy;
}

bool IsGhost(const uint32_t aFormId) noexcept
{
    return s_ghosts.find(aFormId) != s_ghosts.end();
}

std::optional<uint32_t> StandInFor(const uint32_t aGhost) noexcept
{
    const auto it = s_ghosts.find(aGhost);
    if (it == s_ghosts.end())
        return std::nullopt;
    return it->second;
}

bool ForgetGhost(const uint32_t aGhost) noexcept
{
    return s_ghosts.erase(aGhost) != 0;
}

std::optional<uint32_t> TakeGhostOfCopy(const uint32_t aCopy) noexcept
{
    for (auto it = s_ghosts.begin(); it != s_ghosts.end(); ++it)
    {
        if (it->second != aCopy)
            continue;
        const uint32_t cGhost = it->first;
        s_ghosts.erase(it);
        return cGhost;
    }
    return std::nullopt;
}

std::optional<std::pair<uint32_t, uint32_t>> StandInEntryOf(const uint32_t aGhostOrCopy) noexcept
{
    const auto it = std::find_if(s_ghosts.begin(), s_ghosts.end(), [aGhostOrCopy](const auto& acEntry) { return acEntry.first == aGhostOrCopy || acEntry.second == aGhostOrCopy; });
    if (it == s_ghosts.end())
        return std::nullopt;
    return std::make_pair(it->first, it->second);
}

const TiltedPhoques::Map<uint32_t, uint32_t>& Ghosts() noexcept
{
    return s_ghosts;
}

TiltedPhoques::Map<uint32_t, uint32_t> TakeAllGhosts() noexcept
{
    TiltedPhoques::Map<uint32_t, uint32_t> ghosts;
    ghosts.swap(s_ghosts);
    return ghosts;
}

TiltedPhoques::Set<uint32_t> StandInCopies() noexcept
{
    TiltedPhoques::Set<uint32_t> copies;
    for (const auto& [ghost, copy] : s_ghosts)
        if (copy)
            copies.insert(copy);
    return copies;
}
} // namespace CopyRegistry
