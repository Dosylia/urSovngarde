#pragma once

#include <DecodeGuard.h>

#include <TiltedCore/Serialization.hpp>

//! A list of plugin file names, as the NPC plugin messages carry it.
namespace PluginList
{
//! Far above any load order.
constexpr uint64_t kMax = 4096;

inline void Write(TiltedPhoques::Buffer::Writer& aWriter, const TiltedPhoques::Vector<String>& acList) noexcept
{
    const uint64_t cCount = std::min<uint64_t>(acList.size(), kMax);
    Serialization::WriteVarInt(aWriter, cCount);
    for (uint64_t i = 0; i < cCount; ++i)
        Serialization::WriteString(aWriter, acList[i]);
}

//! A count over the limit leaves the list empty.
inline void Read(TiltedPhoques::Buffer::Reader& aReader, TiltedPhoques::Vector<String>& aList) noexcept
{
    const uint64_t cCount = Serialization::ReadVarInt(aReader);
    aList.clear();
    if (!DecodeGuard::Within(cCount, kMax))
        return;
    for (uint64_t i = 0; i < cCount; ++i)
        aList.push_back(Serialization::ReadString(aReader));
}

//! "a, b, c"; past aShown names, the rest is counted ("and 3 more").
inline String Joined(const TiltedPhoques::Vector<String>& acList, const size_t aShown = SIZE_MAX) noexcept
{
    String text;
    for (size_t i = 0; i < acList.size() && i < aShown; ++i)
        text += (text.empty() ? "" : ", ") + acList[i];
    if (acList.size() > aShown)
        text += String(" and ") + std::to_string(acList.size() - aShown).c_str() + " more";
    return text;
}
} // namespace PluginList
