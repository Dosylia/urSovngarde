#include <TiltedCore/Stl.hpp>

#include <catch2/catch.hpp>

#include <PluginRules.h>

using namespace TiltedPhoques;

TEST_CASE("A game's own plugins between a flat and a VR player")
{
    SECTION("VR's own master is VR's, not flat's")
    {
        REQUIRE(PluginRules::IsGameOwn("SkyrimVR.esm", false));
        REQUIRE_FALSE(PluginRules::IsGameOwn("SkyrimVR.esm", true));
    }

    SECTION("The free Creation Club files are flat's, not VR's")
    {
        REQUIRE(PluginRules::IsGameOwn("ccBGSSSE001-Fish.esm", true));
        REQUIRE(PluginRules::IsGameOwn("ccBGSSSE025-AdvDSGS.esm", true));
        REQUIRE(PluginRules::IsGameOwn("ccQDRSSE001-SurvivalMode.esl", true));
        REQUIRE_FALSE(PluginRules::IsGameOwn("ccBGSSSE001-Fish.esm", false));
    }

    SECTION("Names compare without case, as the game loads them")
    {
        REQUIRE(PluginRules::IsGameOwn("skyrimvr.esm", false));
        REQUIRE(PluginRules::IsGameOwn("CCBGSSSE001-FISH.ESM", true));
        REQUIRE_FALSE(PluginRules::IsGameOwn("SkyrimVR.esp", false));
        REQUIRE_FALSE(PluginRules::IsGameOwn("ccBGSSSE001-Fish", true));
    }

    SECTION("The lists measured on 2026-11-18: only real mod differences stay")
    {
        // What only the VR player and only the flat player had in the server's comparison, cut to a few names.
        Vector<String> onlyVr{"Inigo.esp", "SkyrimVR.esm", "Unofficial Skyrim Special Edition Patch.esp"};
        Vector<String> onlyFlat{"ccBGSSSE001-Fish.esm", "ccBGSSSE025-AdvDSGS.esm"};

        const Vector<String> vrOwn = PluginRules::RemoveGameOwn(onlyVr, false);
        const Vector<String> flatOwn = PluginRules::RemoveGameOwn(onlyFlat, true);

        REQUIRE(onlyVr == Vector<String>{"Inigo.esp", "Unofficial Skyrim Special Edition Patch.esp"});
        REQUIRE(vrOwn == Vector<String>{"SkyrimVR.esm"});
        REQUIRE(onlyFlat.empty());
        REQUIRE(flatOwn == Vector<String>{"ccBGSSSE001-Fish.esm", "ccBGSSSE025-AdvDSGS.esm"});
    }

    SECTION("A mod with a Creation Club name on the VR side stays a difference")
    {
        Vector<String> onlyVr{"ccBGSSSE001-Fish.esm"};
        REQUIRE(PluginRules::RemoveGameOwn(onlyVr, false).empty());
        REQUIRE(onlyVr == Vector<String>{"ccBGSSSE001-Fish.esm"});
    }
}

TEST_CASE("The plugin rule for a pair of players")
{
    SECTION("A flat player and a VR player: each list loses its own game's plugins, whichever side is which")
    {
        Vector<String> onlyFlat{"ccBGSSSE001-Fish.esm", "Flat Mod.esp"};
        Vector<String> onlyVr{"SkyrimVR.esm", "Inigo.esp"};
        const PluginRules::LeftOut leftOut = PluginRules::FilterForPair(onlyFlat, true, onlyVr, false);
        REQUIRE(onlyFlat == Vector<String>{"Flat Mod.esp"});
        REQUIRE(onlyVr == Vector<String>{"Inigo.esp"});
        REQUIRE(leftOut.Here == Vector<String>{"ccBGSSSE001-Fish.esm"});
        REQUIRE(leftOut.There == Vector<String>{"SkyrimVR.esm"});

        Vector<String> vrFirst{"SkyrimVR.esm"};
        Vector<String> flatSecond{"ccBGSSSE025-AdvDSGS.esm"};
        const PluginRules::LeftOut swapped = PluginRules::FilterForPair(vrFirst, false, flatSecond, true);
        REQUIRE(vrFirst.empty());
        REQUIRE(flatSecond.empty());
        REQUIRE(swapped.Here == Vector<String>{"SkyrimVR.esm"});
        REQUIRE(swapped.There == Vector<String>{"ccBGSSSE025-AdvDSGS.esm"});
    }

    SECTION("Two VR players keep every difference, SkyrimVR.esm and a ported Creation Club file included")
    {
        Vector<String> onlyHere{"SkyrimVR.esm", "ccBGSSSE001-Fish.esm"};
        Vector<String> onlyThere{"Inigo.esp"};
        const PluginRules::LeftOut leftOut = PluginRules::FilterForPair(onlyHere, false, onlyThere, false);
        REQUIRE(onlyHere == Vector<String>{"SkyrimVR.esm", "ccBGSSSE001-Fish.esm"});
        REQUIRE(onlyThere == Vector<String>{"Inigo.esp"});
        REQUIRE(leftOut.Here.empty());
        REQUIRE(leftOut.There.empty());
    }

    SECTION("Two flat players keep every difference, the Creation Club files included")
    {
        Vector<String> onlyHere{"ccQDRSSE001-SurvivalMode.esl"};
        Vector<String> onlyThere{"ccBGSSSE037-Curios.esl"};
        const PluginRules::LeftOut leftOut = PluginRules::FilterForPair(onlyHere, true, onlyThere, true);
        REQUIRE(onlyHere == Vector<String>{"ccQDRSSE001-SurvivalMode.esl"});
        REQUIRE(onlyThere == Vector<String>{"ccBGSSSE037-Curios.esl"});
        REQUIRE(leftOut.Here.empty());
        REQUIRE(leftOut.There.empty());
    }
}
