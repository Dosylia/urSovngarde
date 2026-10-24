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
} // namespace

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
}
