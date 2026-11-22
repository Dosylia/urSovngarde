// Where the banner and the meter are shown relative to the headset (client/UI/Banner/BannerPlacement.h).

#include <catch2/catch.hpp>

#include <UI/Banner/BannerPlacement.h>

#include <string>

using BannerPlacement::Layer;

namespace
{
// How close two lengths in metres must be.
constexpr float kMeters = 1e-4f;

// The meter's tilt, atan(0.45 / 1.5), about 16.7 degrees: its cosine and sine.
constexpr float kMeterCos = 0.957826f;
constexpr float kMeterSin = 0.287348f;
} // namespace

TEST_CASE("The death card is 1.6 m wide, as tall as its picture's proportions, 1.5 m ahead and 5 cm below the eyes", "[bannerplacement]")
{
    // The card's picture is 2048 by 640: 1.6 m wide makes it 0.5 m tall, from 20 cm above its centre to 30 cm below the eyes.
    const auto cCorners = BannerPlacement::CornersOf(BannerPlacement::PlacementOf(Layer::kBanner), 2048, 640);
    // Top left, top right, bottom right, bottom left.
    CHECK(cCorners[0].x == Approx(-0.8f).margin(kMeters));
    CHECK(cCorners[0].y == Approx(0.2f).margin(kMeters));
    CHECK(cCorners[0].z == Approx(-1.5f).margin(kMeters));
    CHECK(cCorners[1].x == Approx(0.8f).margin(kMeters));
    CHECK(cCorners[1].y == Approx(0.2f).margin(kMeters));
    CHECK(cCorners[2].x == Approx(0.8f).margin(kMeters));
    CHECK(cCorners[2].y == Approx(-0.3f).margin(kMeters));
    CHECK(cCorners[3].x == Approx(-0.8f).margin(kMeters));
    CHECK(cCorners[3].y == Approx(-0.3f).margin(kMeters));
    CHECK(cCorners[3].z == Approx(-1.5f).margin(kMeters));
}

TEST_CASE("A banner line is as wide as the card and less tall, around the same centre", "[bannerplacement]")
{
    // A line's picture is 2048 by 384: 0.3 m tall, centred 5 cm below the eyes.
    const auto cCorners = BannerPlacement::CornersOf(BannerPlacement::PlacementOf(Layer::kBanner), 2048, 384);
    CHECK(cCorners[0].x == Approx(-0.8f).margin(kMeters));
    CHECK(cCorners[0].y == Approx(0.1f).margin(kMeters));
    CHECK(cCorners[3].y == Approx(-0.2f).margin(kMeters));
}

TEST_CASE("The meter is 0.9 m wide, its middle 0.45 m above the eyes and 1.5 m ahead, turned to face them", "[bannerplacement]")
{
    // The meter's picture is 1152 by 200: 0.9 m wide makes it 0.15625 m tall.
    const auto cCorners = BannerPlacement::CornersOf(BannerPlacement::PlacementOf(Layer::kMeter), 1152, 200);
    const glm::vec3 cMiddle = (cCorners[0] + cCorners[1] + cCorners[2] + cCorners[3]) * 0.25f;
    CHECK(cMiddle.x == Approx(0.f).margin(kMeters));
    CHECK(cMiddle.y == Approx(0.45f).margin(kMeters));
    CHECK(cMiddle.z == Approx(-1.5f).margin(kMeters));

    // Its top edge is 7.48 cm higher and 2.24 cm nearer than its middle.
    CHECK(cCorners[0].x == Approx(-0.45f).margin(kMeters));
    CHECK(cCorners[0].y == Approx(0.52483f).margin(kMeters));
    CHECK(cCorners[0].z == Approx(-1.47755f).margin(kMeters));
    CHECK(cCorners[3].y == Approx(0.37517f).margin(kMeters));
    CHECK(cCorners[3].z == Approx(-1.52245f).margin(kMeters));

    // Facing the eyes: the line from the eyes to its middle is square to it, across and down.
    CHECK(glm::dot(cCorners[1] - cCorners[0], cMiddle) == Approx(0.f).margin(kMeters));
    CHECK(glm::dot(cCorners[3] - cCorners[0], cMiddle) == Approx(0.f).margin(kMeters));
}

TEST_CASE("SteamVR gets the banner upright, 1.5 m ahead and 5 cm below the eyes, 1.6 m wide", "[bannerplacement]")
{
    const auto cPlacement = BannerPlacement::PlacementOf(Layer::kBanner);
    const float cExpected[3][4]{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, -0.05f}, {0.f, 0.f, 1.f, -1.5f}};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            CHECK(cPlacement.Rows[row][column] == cExpected[row][column]);
    CHECK(cPlacement.WidthMeters == 1.6f);
}

TEST_CASE("SteamVR gets the meter turned about the x axis, 0.45 m above the eyes and 1.5 m ahead, 0.9 m wide", "[bannerplacement]")
{
    const auto cPlacement = BannerPlacement::PlacementOf(Layer::kMeter);
    const float cExpected[3][4]{{1.f, 0.f, 0.f, 0.f}, {0.f, kMeterCos, -kMeterSin, 0.45f}, {0.f, kMeterSin, kMeterCos, -1.5f}};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            CHECK(cPlacement.Rows[row][column] == Approx(cExpected[row][column]).margin(1e-5f));
    CHECK(cPlacement.WidthMeters == 0.9f);
}

TEST_CASE("The banner is drawn over the meter, and each layer keeps its names", "[bannerplacement]")
{
    CHECK(BannerPlacement::IndexOf(Layer::kBanner) > BannerPlacement::IndexOf(Layer::kMeter));
    CHECK(std::string(BannerPlacement::NamesOf(Layer::kBanner).Log) == "banner");
    CHECK(std::string(BannerPlacement::NamesOf(Layer::kMeter).Log) == "meter");
    // SteamVR knows the overlays by these keys.
    CHECK(std::string(BannerPlacement::NamesOf(Layer::kBanner).OverlayKey) == "ursovngarde.banner");
    CHECK(std::string(BannerPlacement::NamesOf(Layer::kMeter).OverlayKey) == "ursovngarde.meter");
}

TEST_CASE("A picture with no width gets no height, rather than broken numbers", "[bannerplacement]")
{
    const auto cCorners = BannerPlacement::CornersOf(BannerPlacement::PlacementOf(Layer::kBanner), 0, 640);
    CHECK(cCorners[0].y == Approx(-0.05f).margin(kMeters));
    CHECK(cCorners[3].y == Approx(-0.05f).margin(kMeters));
}
