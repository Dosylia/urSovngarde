#include "PluginRules.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>

namespace PluginRules
{
namespace
{
// VR's own master, loaded by every VR game.
constexpr const char* cVrOwn[] = {"SkyrimVR.esm"};

// The free Creation Club files every flat Skyrim has had since 1.6.640 (the Anniversary update); none exists for VR.
constexpr const char* cFlatOwn[] = {
    "_ResourcePack.esl", "ccBGSSSE001-Fish.esm", "ccBGSSSE025-AdvDSGS.esm", "ccBGSSSE037-Curios.esl", "ccQDRSSE001-SurvivalMode.esl",
};

bool SameName(const TiltedPhoques::String& acPlugin, const char* acpName) noexcept
{
    return acPlugin.size() == std::strlen(acpName) &&
           std::equal(acPlugin.begin(), acPlugin.end(), acpName, [](const char aLeft, const char aRight) { return std::tolower(static_cast<unsigned char>(aLeft)) == std::tolower(static_cast<unsigned char>(aRight)); });
}

template <size_t N> bool InList(const TiltedPhoques::String& acPlugin, const char* const (&acNames)[N]) noexcept
{
    return std::any_of(std::begin(acNames), std::end(acNames), [&acPlugin](const char* acpName) { return SameName(acPlugin, acpName); });
}
} // namespace

bool IsGameOwn(const TiltedPhoques::String& acPlugin, const bool aFlat) noexcept
{
    return aFlat ? InList(acPlugin, cFlatOwn) : InList(acPlugin, cVrOwn);
}

TiltedPhoques::Vector<TiltedPhoques::String> RemoveGameOwn(TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyThisPlayer, const bool aFlat) noexcept
{
    TiltedPhoques::Vector<TiltedPhoques::String> removed;
    TiltedPhoques::Vector<TiltedPhoques::String> kept;
    for (TiltedPhoques::String& plugin : aOnlyThisPlayer)
    {
        if (IsGameOwn(plugin, aFlat))
            removed.push_back(std::move(plugin));
        else
            kept.push_back(std::move(plugin));
    }
    aOnlyThisPlayer = std::move(kept);
    return removed;
}

LeftOut FilterForPair(TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyHere, const bool aHereFlat, TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyThere, const bool aThereFlat) noexcept
{
    LeftOut leftOut;
    if (aHereFlat == aThereFlat)
        return leftOut;
    leftOut.Here = RemoveGameOwn(aOnlyHere, aHereFlat);
    leftOut.There = RemoveGameOwn(aOnlyThere, aThereFlat);
    return leftOut;
}
} // namespace PluginRules
