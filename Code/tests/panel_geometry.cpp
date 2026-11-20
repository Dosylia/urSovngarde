// The in-game panel's geometry: where it opens in front of the player, and where a laser lands on it (client/UI/Menu/PanelGeometry.h).

#include <catch2/catch.hpp>

#include <UI/Menu/PanelGeometry.h>

#include <limits>

namespace
{
// The panel as the game opens it: 1.4 m wide at the page's 16:9, 1.25 m ahead, 15 cm below the eyes, 70 game units to the metre.
constexpr PanelGeometry::Placement kPlacement{1.4f, 1.4f * 900.f / 1600.f, 1.25f, 0.15f};
constexpr float kUnitsPerMetre = 70.f;
constexpr int kPageWidth = 1600;
constexpr int kPageHeight = 900;

// The panel opened by a player at the origin, looking along +y.
PanelGeometry::Corners PanelAhead()
{
    return PanelGeometry::CornersInFront(glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f), kUnitsPerMetre, kPlacement);
}

// Two points within a thousandth of a game unit of each other.
bool Near(const glm::vec3& acA, const glm::vec3& acB)
{
    return glm::distance(acA, acB) < 1e-3f;
}
} // namespace

TEST_CASE("Ahead is where the head looks, level with the ground", "[panelgeometry]")
{
    // Looking ahead and a little up: only the level part counts.
    auto ahead = PanelGeometry::LevelAhead(glm::normalize(glm::vec3(0.f, 1.f, 0.5f)), glm::normalize(glm::vec3(0.f, -0.5f, 1.f)));
    REQUIRE(ahead);
    CHECK(Near(*ahead, {0.f, 1.f, 0.f}));

    // Looking straight down, the top of the head points ahead.
    ahead = PanelGeometry::LevelAhead({0.f, 0.f, -1.f}, {0.f, 1.f, 0.f});
    REQUIRE(ahead);
    CHECK(Near(*ahead, {0.f, 1.f, 0.f}));

    // Looking straight up, the top of the head points back.
    ahead = PanelGeometry::LevelAhead({0.f, 0.f, 1.f}, {0.f, -1.f, 0.f});
    REQUIRE(ahead);
    CHECK(Near(*ahead, {0.f, 1.f, 0.f}));

    // Broken numbers give no direction.
    const float cNaN = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(PanelGeometry::LevelAhead({cNaN, cNaN, cNaN}, {0.f, 0.f, 1.f}));
}

TEST_CASE("The panel opens upright in front of the player, a little below the eyes", "[panelgeometry]")
{
    const auto cCorners = PanelAhead();
    const glm::vec3 cCentre = PanelGeometry::CentreOf(cCorners);
    CHECK(Near(cCentre, {0.f, 1.25f * kUnitsPerMetre, -0.15f * kUnitsPerMetre}));

    // Top left first, clockwise as the player faces it: x is right, z is up.
    CHECK(cCorners[0].x < 0.f);
    CHECK(cCorners[1].x > 0.f);
    CHECK(cCorners[0].z > cCorners[3].z);
    CHECK(glm::distance(cCorners[0], cCorners[1]) == Approx(1.4f * kUnitsPerMetre));
    CHECK(glm::distance(cCorners[1], cCorners[2]) == Approx(kPlacement.Height * kUnitsPerMetre));

    // Upright: every corner is as far ahead as the centre.
    for (const auto& cCorner : cCorners)
        CHECK(cCorner.y == Approx(cCentre.y));
}

TEST_CASE("A laser at the panel's centre lands on the page's centre", "[panelgeometry]")
{
    const auto cCorners = PanelAhead();
    const glm::vec3 cCentre = PanelGeometry::CentreOf(cCorners);

    const auto cHit = PanelGeometry::AimAt(cCorners, glm::vec3(0.f), glm::normalize(cCentre), kPageWidth, kPageHeight);
    REQUIRE(cHit);
    CHECK(cHit->X == 799);
    CHECK(cHit->Y == 449);
    CHECK(Near(cHit->Point, cCentre));
}

TEST_CASE("A laser near the panel's top left corner lands at the page's top left", "[panelgeometry]")
{
    const auto cCorners = PanelAhead();
    const glm::vec3 cTarget = cCorners[0] + (PanelGeometry::CentreOf(cCorners) - cCorners[0]) * 0.001f;

    const auto cHit = PanelGeometry::AimAt(cCorners, glm::vec3(0.f), glm::normalize(cTarget), kPageWidth, kPageHeight);
    REQUIRE(cHit);
    CHECK(cHit->X <= 2);
    CHECK(cHit->Y <= 2);
}

TEST_CASE("A laser that misses the panel lands nowhere", "[panelgeometry]")
{
    const auto cCorners = PanelAhead();
    const glm::vec3 cFrom(0.f);

    // Pointing away from the panel, along its face, and beside it.
    CHECK_FALSE(PanelGeometry::AimAt(cCorners, cFrom, {0.f, -1.f, 0.f}, kPageWidth, kPageHeight));
    CHECK_FALSE(PanelGeometry::AimAt(cCorners, cFrom, {1.f, 0.f, 0.f}, kPageWidth, kPageHeight));
    CHECK_FALSE(PanelGeometry::AimAt(cCorners, cFrom, glm::normalize(glm::vec3(2.f, 1.f, 0.f)), kPageWidth, kPageHeight));

    // From behind the panel, pointing back at it: the page would be seen mirrored.
    const glm::vec3 cBehind(0.f, 2.f * 1.25f * kUnitsPerMetre, -0.15f * kUnitsPerMetre);
    CHECK_FALSE(PanelGeometry::AimAt(cCorners, cBehind, {0.f, -1.f, 0.f}, kPageWidth, kPageHeight));

    // Broken numbers.
    const float cNaN = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(PanelGeometry::AimAt(cCorners, {cNaN, cNaN, cNaN}, {0.f, 1.f, 0.f}, kPageWidth, kPageHeight));
}
