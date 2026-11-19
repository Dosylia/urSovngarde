#pragma once

#include <TiltedCore/Stl.hpp>

//! Which plugin differences between two players are worth a warning.
//! Plain values in and out, so the unit tests check the rule without a server (PlayerService applies it).
namespace PluginRules
{
//! True when the plugin comes with the game itself: SkyrimVR.esm in VR, the free Creation Club files in flat Skyrim.
//! The names are compared without case, as the game loads them.
bool IsGameOwn(const TiltedPhoques::String& acPlugin, bool aFlat) noexcept;

//! Takes out of a list of plugins only one player has those that come with that player's game.
//! Used only between a flat and a VR player: the other game can never have them, so they are no mod difference.
//! Between two players of the same game every difference stays, a VR player's ported Creation Club file included.
//! Returns what it took out, for the server log.
TiltedPhoques::Vector<TiltedPhoques::String> RemoveGameOwn(TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyThisPlayer, bool aFlat) noexcept;

//! What a pair's comparison left out as each player's game's own plugins.
struct LeftOut
{
    TiltedPhoques::Vector<TiltedPhoques::String> Here;
    TiltedPhoques::Vector<TiltedPhoques::String> There;
};

//! The rule for one pair: what only this player has and what only the other has, each with that player's game.
//! When one plays flat and the other VR, each list loses its own game's plugins; between two players of the same game nothing is taken out.
LeftOut FilterForPair(TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyHere, bool aHereFlat, TiltedPhoques::Vector<TiltedPhoques::String>& aOnlyThere, bool aThereFlat) noexcept;
} // namespace PluginRules
