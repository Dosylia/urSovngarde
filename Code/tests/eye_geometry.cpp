// The maths that puts the panel's marks into the eye images (client/UI/Menu/EyeGeometry.h).

#include <catch2/catch.hpp>

#include <UI/Menu/EyeGeometry.h>

namespace
{
// An eye at the head's centre: no turn, no offset.
constexpr float kEyeAtHead[3][4]{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}};
// The same eye 3 cm to the right of the head's centre.
constexpr float kEyeToTheRight[3][4]{{1.f, 0.f, 0.f, 0.03f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}};
// A projection that keeps x and y and puts the distance ahead in w: a point 1 m ahead lands where its x and y say.
constexpr float kSimpleProjection[4][4]{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}, {0.f, 0.f, -1.f, 0.f}};

// An eye's part of the image, 1000 by 500 pixels.
D3D11_VIEWPORT Viewport()
{
    D3D11_VIEWPORT viewport{};
    viewport.Width = 1000.f;
    viewport.Height = 500.f;
    viewport.MaxDepth = 1.f;
    return viewport;
}

// The pixel a point seen from the head lands on, through the simple projection.
std::optional<std::pair<UINT, UINT>> PixelSeenAt(const float (&acEyeToHead)[3][4], const glm::vec3& acHead)
{
    return EyeGeometry::PixelOf(EyeGeometry::ToClip(kSimpleProjection, acEyeToHead, acHead), Viewport());
}
} // namespace

TEST_CASE("A point straight ahead lands in the middle of the eye image", "[eyegeometry]")
{
    const auto cPixel = PixelSeenAt(kEyeAtHead, {0.f, 0.f, -1.f});
    REQUIRE(cPixel);
    CHECK(cPixel->first == 500);
    CHECK(cPixel->second == 250);
}

TEST_CASE("Up in the head is up in the image, right is right", "[eyegeometry]")
{
    const auto cPixel = PixelSeenAt(kEyeAtHead, {0.5f, 0.5f, -1.f});
    REQUIRE(cPixel);
    CHECK(cPixel->first == 750);
    CHECK(cPixel->second == 125);
}

TEST_CASE("An eye beside the head's centre sees from its own place", "[eyegeometry]")
{
    // 3 cm to the right of the head is straight ahead of the right eye.
    const auto cPixel = PixelSeenAt(kEyeToTheRight, {0.03f, 0.f, -1.f});
    REQUIRE(cPixel);
    CHECK(cPixel->first == 500);
    CHECK(cPixel->second == 250);
}

TEST_CASE("A point behind the eye or outside its image lands on no pixel", "[eyegeometry]")
{
    CHECK_FALSE(PixelSeenAt(kEyeAtHead, {0.f, 0.f, 1.f}));
    CHECK_FALSE(PixelSeenAt(kEyeAtHead, {2.f, 0.f, -1.f}));
    CHECK_FALSE(PixelSeenAt(kEyeAtHead, {0.f, -2.f, -1.f}));
}

TEST_CASE("A dot is a square around its point, facing the eyes", "[eyegeometry]")
{
    const glm::vec3 cCentre(0.f, 0.f, -2.f);
    const auto cCorners = EyeGeometry::Facing(cCentre, 0.1f);

    // Top left first: left is -x, up is +y; every corner as far ahead as the point.
    CHECK(cCorners[0].x == Approx(-0.1f));
    CHECK(cCorners[0].y == Approx(0.1f));
    CHECK(glm::distance(cCorners[0], cCorners[1]) == Approx(0.2f));
    CHECK(glm::distance(cCorners[1], cCorners[2]) == Approx(0.2f));
    for (const auto& cCorner : cCorners)
        CHECK(cCorner.z == Approx(-2.f));

    // Straight above the head the usual "up" fails, and the square is still whole.
    const auto cAbove = EyeGeometry::Facing({0.f, 2.f, 0.f}, 0.1f);
    CHECK(glm::distance(cAbove[0], cAbove[1]) == Approx(0.2f));
    CHECK(glm::distance(cAbove[1], cAbove[2]) == Approx(0.2f));
}

TEST_CASE("A beam is a thin strip, and none when it points straight at the eyes", "[eyegeometry]")
{
    const auto cBeam = EyeGeometry::Strip({-1.f, 0.f, -2.f}, {1.f, 0.f, -2.f}, 0.002f);
    REQUIRE(cBeam);
    CHECK(glm::distance((*cBeam)[0], (*cBeam)[1]) == Approx(2.f));
    CHECK(glm::distance((*cBeam)[0], (*cBeam)[3]) == Approx(0.004f));

    // Seen end on, or of no length, it has no width to draw.
    CHECK_FALSE(EyeGeometry::Strip({0.f, 0.f, -1.f}, {0.f, 0.f, -2.f}, 0.002f));
    CHECK_FALSE(EyeGeometry::Strip({0.f, 0.f, -1.f}, {0.f, 0.f, -1.f}, 0.002f));
}
