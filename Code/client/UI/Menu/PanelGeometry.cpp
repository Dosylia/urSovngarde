#include <UI/Menu/PanelGeometry.h>

#include <cmath>

namespace PanelGeometry
{
/**
 * Find which way is ahead for the player, level with the ground: where the head looks, without looking up or down.
 * Looking straight down, that is where the top of the head points; looking straight up, the opposite way.
 *
 * @param acLooking Which way the head looks, in the game's world.
 * @param acHeadTop Which way the top of the head points, in the game's world.
 * @return A level direction of length 1, or nothing when the numbers give none.
 */
std::optional<glm::vec3> LevelAhead(const glm::vec3& acLooking, const glm::vec3& acHeadTop) noexcept
{
    // The game's world has z up: the level part of a direction is its x and y.
    glm::vec3 ahead = acLooking;
    ahead.z = 0.f;
    if (!(glm::dot(ahead, ahead) > 1e-4f))
    {
        ahead = acHeadTop * (acLooking.z > 0.f ? -1.f : 1.f);
        ahead.z = 0.f;
    }
    // The test passes only for a real number, so broken numbers give no direction.
    if (!(glm::dot(ahead, ahead) > 1e-4f))
        return std::nullopt;
    return glm::normalize(ahead);
}

/**
 * Work out the panel's corners in front of the headset, upright and level.
 *
 * @param acHead Where the headset is, in game units.
 * @param acLevelAhead Which way is ahead: level, and of length 1.
 * @param aUnitsPerMetre How many game units make one metre.
 * @param acPlacement The panel's size and distance, in metres.
 * @return The panel's corners in the game's world.
 */
Corners CornersInFront(const glm::vec3& acHead, const glm::vec3& acLevelAhead, const float aUnitsPerMetre, const Placement& acPlacement) noexcept
{
    const glm::vec3 cUp(0.f, 0.f, 1.f);
    const glm::vec3 cRight = glm::cross(acLevelAhead, cUp);

    const glm::vec3 cCentre = acHead + acLevelAhead * (acPlacement.Ahead * aUnitsPerMetre) - cUp * (acPlacement.BelowEyes * aUnitsPerMetre);
    const glm::vec3 cHalfWidth = cRight * (acPlacement.Width / 2.f * aUnitsPerMetre);
    const glm::vec3 cHalfHeight = cUp * (acPlacement.Height / 2.f * aUnitsPerMetre);
    return {cCentre - cHalfWidth + cHalfHeight, cCentre + cHalfWidth + cHalfHeight, cCentre + cHalfWidth - cHalfHeight, cCentre - cHalfWidth - cHalfHeight};
}

/**
 * Find the middle of the panel.
 *
 * @param acCorners The panel's corners.
 * @return The point halfway between opposite corners, in the game's world.
 */
glm::vec3 CentreOf(const Corners& acCorners) noexcept
{
    return (acCorners[0] + acCorners[2]) * 0.5f;
}

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
std::optional<Hit> AimAt(const Corners& acCorners, const glm::vec3& acFrom, const glm::vec3& acAlong, const int aPageWidth, const int aPageHeight) noexcept
{
    const glm::vec3& cTopLeft = acCorners[0];
    const glm::vec3 cAcross = acCorners[1] - cTopLeft;
    const glm::vec3 cDown = acCorners[3] - cTopLeft;
    // Away from the player, who faces the panel's front.
    const glm::vec3 cNormal = glm::cross(cAcross, cDown);
    // Each test passes only for a real number, so a broken transform points nowhere.
    // A laser from behind the panel points nowhere too: the page would be seen from its back, mirrored.
    if (!(glm::dot(acFrom - cTopLeft, cNormal) < 0.f))
        return std::nullopt;
    const float cFacing = glm::dot(acAlong, cNormal);
    if (!(std::abs(cFacing) > 1e-6f))
        return std::nullopt;
    const float cDistance = glm::dot(cTopLeft - acFrom, cNormal) / cFacing;
    if (!(cDistance > 0.f))
        return std::nullopt;
    const glm::vec3 cHit = acFrom + cDistance * acAlong;
    // How far across and down the panel the laser lands, from 0 at the top left to 1 at the bottom right.
    const float cU = glm::dot(cHit - cTopLeft, cAcross) / glm::dot(cAcross, cAcross);
    const float cV = glm::dot(cHit - cTopLeft, cDown) / glm::dot(cDown, cDown);
    if (!(cU >= 0.f && cU <= 1.f && cV >= 0.f && cV <= 1.f))
        return std::nullopt;
    return Hit{static_cast<uint16_t>(cU * static_cast<float>(aPageWidth - 1)), static_cast<uint16_t>(cV * static_cast<float>(aPageHeight - 1)), cHit};
}
} // namespace PanelGeometry
