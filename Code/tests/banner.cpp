#include <catch2/catch.hpp>

#include <Games/Skyrim/BannerPicture.h>

#include <cstdlib>

namespace
{
// How many pixels of a band of rows show anything at all.
size_t Drawn(const BannerPicture::Picture& acPicture, const int aFirstRow, const int aLastRow)
{
    size_t count = 0;
    for (int y = aFirstRow; y <= aLastRow; ++y)
        for (int x = 0; x < acPicture.Width; ++x)
            if (acPicture.Rgba[(static_cast<size_t>(y) * acPicture.Width + x) * 4] > 100)
                ++count;
    return count;
}

// How many pixels of a band of columns show anything at all.
size_t DrawnColumns(const BannerPicture::Picture& acPicture, const int aFirstColumn, const int aLastColumn)
{
    size_t count = 0;
    for (int y = 0; y < acPicture.Height; ++y)
        for (int x = aFirstColumn; x <= aLastColumn; ++x)
            if (acPicture.Rgba[(static_cast<size_t>(y) * acPicture.Width + x) * 4] > 100)
                ++count;
    return count;
}
} // namespace

TEST_CASE("a line too long for the banner is written smaller, never cut off at its ends", "[banner]")
{
    // The revive line with a long player name ran past both edges of the picture.
    const auto cLong = BannerPicture::Line(L"Somebody with a rather long name is reviving you", 0xD4AF37, 72);
    REQUIRE(cLong.Width == 2048);
    REQUIRE(Drawn(cLong, 0, cLong.Height - 1) > 3000);
    REQUIRE(DrawnColumns(cLong, 0, 40) == 0);
    REQUIRE(DrawnColumns(cLong, cLong.Width - 41, cLong.Width - 1) == 0);

    // A short line keeps its size.
    const auto cShort = BannerPicture::Line(L"Whiterun", 0xD4AF37, 72);
    const auto cShortSmaller = BannerPicture::Line(L"Whiterun", 0xD4AF37, 48);
    REQUIRE(Drawn(cShort, 0, cShort.Height - 1) > Drawn(cShortSmaller, 0, cShortSmaller.Height - 1));
}

TEST_CASE("the death card draws its title, and its subtitle only when there is one", "[banner]")
{
    // No game data here: Georgia stands in for the game's fonts.
    const auto cWith = BannerPicture::DeathCard(L"YOU DIED", L"A PLAYER WAS SLAIN BY A BANDIT", 0xA31919);
    REQUIRE(cWith.Width == 2048);
    REQUIRE(cWith.Height == 640);
    REQUIRE(Drawn(cWith, 200, 340) > 10000);

    // Without a subtitle the rest moves down to stay centred, so the whole card is compared: the subtitle's letters are what is missing.
    const auto cWithout = BannerPicture::DeathCard(L"YOU DIED", L"", 0xA31919);
    REQUIRE(Drawn(cWith, 0, 639) > Drawn(cWithout, 0, 639) + 1000);
}

// Hidden: draws the pictures in the game's own fonts, to look at them outside the headset.
//   set STR_GAME_DATA=F:\SteamLibrary\steamapps\common\SkyrimVR\Data
//   TPTests.exe "[.banner-preview]"
// The pictures are written next to where it runs.
TEST_CASE("the banners, drawn to pictures", "[.banner-preview]")
{
    if (const char* pData = std::getenv("STR_GAME_DATA"))
        BannerPicture::SetGameData(pData);
    WARN(BannerPicture::FontReport());

    REQUIRE(BannerPicture::WritePng("banner-death.png", BannerPicture::DeathCard(L"YOU DIED", L"LYDIA WAS SLAIN BY BANDIT CHIEF USING IRON WAR AXE", 0xA31919)));
    REQUIRE(BannerPicture::WritePng("banner-death-short.png", BannerPicture::DeathCard(L"YOU DIED", L"LYDIA FELL FROM A HIGH PLACE", 0xA31919)));
    REQUIRE(BannerPicture::WritePng("banner-death-none.png", BannerPicture::DeathCard(L"YOU DIED", L"", 0xA31919)));
    REQUIRE(BannerPicture::WritePng("banner-place.png", BannerPicture::Line(L"Throat of the World", 0xD4AF37, 84)));
    REQUIRE(BannerPicture::WritePng("banner-revive.png", BannerPicture::Line(L"Seen the strong is reviving you", 0xD4AF37, 72)));
}
