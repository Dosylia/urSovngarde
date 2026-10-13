#include "AdminKeys.h"

namespace AdminKeys
{
namespace
{
int HexValue(const char aChar) noexcept
{
    if (aChar >= '0' && aChar <= '9')
        return aChar - '0';
    if (aChar >= 'a' && aChar <= 'f')
        return aChar - 'a' + 10;
    return -1;
}

bool ParseDigest(const std::string_view acHex, Digest& aOut) noexcept
{
    if (acHex.size() != 64)
        return false;
    for (size_t i = 0; i < 32; ++i)
    {
        const int high = HexValue(acHex[2 * i]);
        const int low = HexValue(acHex[2 * i + 1]);
        if (high < 0 || low < 0)
            return false;
        aOut[i] = static_cast<uint8_t>(high << 4 | low);
    }
    return true;
}

bool IsName(const std::string_view acName) noexcept
{
    if (acName.empty() || acName.size() > 32)
        return false;
    for (const char c : acName)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}

std::string_view Trimmed(std::string_view aText) noexcept
{
    while (!aText.empty() && (aText.front() == ' ' || aText.front() == '\t'))
        aText.remove_prefix(1);
    while (!aText.empty() && (aText.back() == ' ' || aText.back() == '\t' || aText.back() == '\r' || aText.back() == '\n'))
        aText.remove_suffix(1);
    return aText;
}
} // namespace

std::vector<Entry> Parse(std::string_view acList, std::vector<size_t>& aSkipped)
{
    std::vector<Entry> entries;
    acList = Trimmed(acList);
    if (acList.empty())
        return entries;
    size_t position = 0;
    while (true)
    {
        ++position;
        const size_t comma = acList.find(',');
        const std::string_view item = Trimmed(acList.substr(0, comma));
        const size_t colon = item.find(':');
        Entry entry{};
        if (colon != std::string_view::npos && IsName(item.substr(0, colon)) && ParseDigest(item.substr(colon + 1), entry.Id))
        {
            entry.Name = std::string(item.substr(0, colon));
            entries.push_back(std::move(entry));
        }
        else
            aSkipped.push_back(position);
        if (comma == std::string_view::npos)
            break;
        acList.remove_prefix(comma + 1);
    }
    return entries;
}

bool IsKeyToken(const std::string_view acToken) noexcept
{
    return acToken.substr(0, kPrefix.size()) == kPrefix;
}

const Entry* Match(const std::string_view acToken, const std::vector<Entry>& acEntries, const Hash aHash) noexcept
{
    if (!IsKeyToken(acToken) || !aHash)
        return nullptr;
    const std::string_view key = acToken.substr(kPrefix.size());
    Digest unused{};
    if (!ParseDigest(key, unused))
        return nullptr;
    const Digest id = aHash(key);
    // Every entry compared to the end: no early exit on a byte, nor on the entry that matches.
    const Entry* pFound = nullptr;
    for (const Entry& entry : acEntries)
    {
        uint8_t difference = 0;
        for (size_t i = 0; i < id.size(); ++i)
            difference |= static_cast<uint8_t>(id[i] ^ entry.Id[i]);
        const bool cSame = difference == 0;
        pFound = cSame && !pFound ? &entry : pFound;
    }
    return pFound;
}
} // namespace AdminKeys
