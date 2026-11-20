#pragma once

#include <array>
#include <optional>
#include <utility>

#include <d3d11.h>

#include <glm/glm.hpp>

/**
 * The maths that puts the panel's marks into the eye images, in plain numbers, so the tests check it directly.
 * Points are in the headset's space, in metres, with x right, y up and -z ahead, as the VR runtime counts.
 */
namespace EyeGeometry
{
// Four corners in the headset's space, from the top left, clockwise as the player faces them.
using HeadCorners = std::array<glm::vec3, 4>;

/**
 * Turn a point seen from the headset into where one eye's projection puts it (its clip space).
 * clip = projection * (eye from head) * head point; the VR runtime's matrices multiply column vectors.
 *
 * @param acProjection The eye's projection, as the VR runtime gives it: four rows of four.
 * @param acEyeToHead Where the eye is in the head, as the VR runtime gives it: three rows of four, the last column the eye's position.
 * @param acHead The point relative to the headset, in metres.
 * @return The point in the eye's clip space: x, y, z, w.
 */
std::array<float, 4> ToClip(const float (&acProjection)[4][4], const float (&acEyeToHead)[3][4], const glm::vec3& acHead) noexcept;

/**
 * Find the eye image pixel a point in an eye's clip space lands on.
 *
 * @param acClip The point in the eye's clip space.
 * @param acViewport The eye's part of the eye image.
 * @return The pixel, across and down, or nothing when the point is behind the eye or outside its part of the image.
 */
std::optional<std::pair<UINT, UINT>> PixelOf(const std::array<float, 4>& acClip, const D3D11_VIEWPORT& acViewport) noexcept;

/**
 * Make a square facing the eyes around a point: the dot and the rings.
 *
 * @param acCentre The square's centre in the headset's space.
 * @param aHalf Half its width, in metres.
 * @return Its corners in the headset's space.
 */
HeadCorners Facing(const glm::vec3& acCentre, float aHalf) noexcept;

/**
 * Make a thin strip from one point to another, turned to face the eyes: the beam.
 *
 * @param acFrom One end in the headset's space.
 * @param acTo The other end in the headset's space.
 * @param aHalf Half the strip's width, in metres.
 * @return Its corners in the headset's space, or nothing when the beam points straight at the eyes or has no length.
 */
std::optional<HeadCorners> Strip(const glm::vec3& acFrom, const glm::vec3& acTo, float aHalf) noexcept;
} // namespace EyeGeometry
