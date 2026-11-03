#include <catch2/catch.hpp>

#include <Games/Skyrim/BannerPicture.h>

#include <chrono>
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

BannerPicture::Picture Meter(const std::wstring& acLabel, const float aFill, const uint32_t aAttackRgb, const uint32_t aDefendRgb)
{
    auto meter = BannerPicture::MeterFace(acLabel);
    BannerPicture::FillMeter(meter, aFill, aAttackRgb, aDefendRgb);
    return meter;
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

TEST_CASE("a title too long for the card is written smaller, never cut off at its ends", "[banner]")
{
    const auto cLong = BannerPicture::DeathCard(L"THE TOURNAMENT OF THE NORTH HAS BEGUN", L"", 0xD4AF37);
    REQUIRE(Drawn(cLong, 0, cLong.Height - 1) > 10000);
    REQUIRE(DrawnColumns(cLong, 0, 40) == 0);
    REQUIRE(DrawnColumns(cLong, cLong.Width - 41, cLong.Width - 1) == 0);
}

TEST_CASE("the meter fills from the left in the attackers' colour and the rest in the defenders'", "[banner]")
{
    // Pixels along the middle of the bar, which runs from 107 to 1045 inside its frame.
    const auto cRed = [](const BannerPicture::Picture& acPicture, const int aX) { return acPicture.Rgba[(124 * static_cast<size_t>(acPicture.Width) + aX) * 4]; };
    const auto cBlue = [](const BannerPicture::Picture& acPicture, const int aX) { return acPicture.Rgba[(124 * static_cast<size_t>(acPicture.Width) + aX) * 4 + 2]; };

    const auto cQuarter = Meter(L"TAKE THE COURTYARD", 0.25f, 0xFF0000, 0x0000FF);
    REQUIRE(cQuarter.Width == 1152);
    REQUIRE(cQuarter.Height == 200);
    REQUIRE(cRed(cQuarter, 200) == 255);
    REQUIRE(cBlue(cQuarter, 200) == 0);
    REQUIRE(cRed(cQuarter, 600) == 0);
    REQUIRE(cBlue(cQuarter, 600) == 255);

    const auto cEmpty = Meter(L"", 0.f, 0xFF0000, 0x0000FF);
    auto refilled = Meter(L"", 1.f, 0xFF0000, 0x0000FF);
    REQUIRE(cBlue(cEmpty, 110) == 255);
    REQUIRE(cRed(refilled, 1040) == 255);

    // A fill drawn over another leaves nothing of it.
    BannerPicture::FillMeter(refilled, 0.f, 0xFF0000, 0x0000FF);
    REQUIRE(refilled.Rgba == cEmpty.Rgba);

    // The label is drawn above the bar, and nothing is drawn there without one.
    REQUIRE(Drawn(cQuarter, 30, 85) > 500);
    REQUIRE(Drawn(cEmpty, 30, 85) == 0);

    // The fill is drawn on the main thread whenever it moves, the face only for a new label.
    auto meter = BannerPicture::MeterFace(L"TAKE THE COURTYARD");
    const auto cStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i)
        BannerPicture::FillMeter(meter, i / 10.f, 0xFF0000, 0x0000FF);
    const double cMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cStart).count() / 10.0;
    WARN("a meter's fill takes " << cMilliseconds << " ms to draw");
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
    REQUIRE(BannerPicture::WritePng("banner-card-long.png", BannerPicture::DeathCard(L"THE TOURNEY HAS BEGUN", L"YOU RIDE FOR THE NORTH", 0xD4AF37)));
    REQUIRE(BannerPicture::WritePng("banner-place.png", BannerPicture::Line(L"Throat of the World", 0xD4AF37, 84)));
    REQUIRE(BannerPicture::WritePng("banner-revive.png", BannerPicture::Line(L"Seen the strong is reviving you", 0xD4AF37, 72)));
    REQUIRE(BannerPicture::WritePng("banner-meter.png", Meter(L"TAKE THE COURTYARD", 0.4f, 0xA31919, 0x2E5FA8)));
}
