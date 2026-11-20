#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <glm/glm.hpp>

/**
 * The in-game panel's geometry in plain numbers: where it opens in front of the player, and where a laser lands on it.
 * It needs nothing from the game, so the tests check it directly.
 * The game's world counts in game units, with x right, y ahead and z up.
 */
namespace PanelGeometry
{
// Four corners in the game's world, from the top left, clockwise as the player faces them.
using Corners = std::array<glm::vec3, 4>;

/**
 * The panel's size and where it opens, in metres.
 */
struct Placement
{
    float Width = 0.f;
    float Height = 0.f;
    // How far ahead of the headset it opens, and how far below the eyes.
    float Ahead = 0.f;
    float BelowEyes = 0.f;
};

/**
 * Where a laser lands on the panel.
 */
struct Hit
{
    // The page pixel, across and down from its top left.
    uint16_t X = 0;
    uint16_t Y = 0;
    // The same spot in the game's world.
    glm::vec3 Point{0.f};
};

/**
 * Find which way is ahead for the player, level with the ground: where the head looks, without looking up or down.
 * Looking straight down, that is where the top of the head points; looking straight up, the opposite way.
 *
 * @param acLooking Which way the head looks, in the game's world.
 * @param acHeadTop Which way the top of the head points, in the game's world.
 * @return A level direction of length 1, or nothing when the numbers give none.
 */
std::optional<glm::vec3> LevelAhead(const glm::vec3& acLooking, const glm::vec3& acHeadTop) noexcept;

/**
 * Work out the panel's corners in front of the headset, upright and level.
 *
 * @param acHead Where the headset is, in game units.
 * @param acLevelAhead Which way is ahead: level, and of length 1.
 * @param aUnitsPerMetre How many game units make one metre.
 * @param acPlacement The panel's size and distance, in metres.
 * @return The panel's corners in the game's world.
 */
Corners CornersInFront(const glm::vec3& acHead, const glm::vec3& acLevelAhead, float aUnitsPerMetre, const Placement& acPlacement) noexcept;

/**
 * Find the middle of the panel.
 *
 * @param acCorners The panel's corners.
 * @return The point halfway between opposite corners, in the game's world.
 */
glm::vec3 CentreOf(const Corners& acCorners) noexcept;

/**
 * Find where a laser lands on the panel, as a page pixel and as a point in the game's world.
 *
 * @param acCorners The panel's corners.
 * @param acFrom Where the laser starts, in the game's world.
 * @param acAlong Which way it points, as a direction of length 1.
 * @param aPageWidth The page's width in pixels.
 * @param aPageHeight The page's height in pixels.
 * @return Where it lands, or nothing when it points elsewhere, comes from behind the panel, or has broken numbers.
 */
std::optional<Hit> AimAt(const Corners& acCorners, const glm::vec3& acFrom, const glm::vec3& acAlong, int aPageWidth, int aPageHeight) noexcept;
} // namespace PanelGeometry
