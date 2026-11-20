#include <UI/Menu/EyeGeometry.h>

namespace EyeGeometry
{
/**
 * Turn a point seen from the headset into where one eye's projection puts it (its clip space).
 * clip = projection * (eye from head) * head point; the VR runtime's matrices multiply column vectors.
 *
 * @param acProjection The eye's projection, as the VR runtime gives it: four rows of four.
 * @param acEyeToHead Where the eye is in the head, as the VR runtime gives it: three rows of four, the last column the eye's position.
 * @param acHead The point relative to the headset, in metres.
 * @return The point in the eye's clip space: x, y, z, w.
 */
std::array<float, 4> ToClip(const float (&acProjection)[4][4], const float (&acEyeToHead)[3][4], const glm::vec3& acHead) noexcept
{
    // The eye-to-head transform is rigid: its inverse is the transposed rotation, applied after taking its translation off.
    float relative[3];
    for (int i = 0; i < 3; ++i)
        relative[i] = acHead[i] - acEyeToHead[i][3];
    float eye[4]{0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 3; ++i)
        eye[i] = acEyeToHead[0][i] * relative[0] + acEyeToHead[1][i] * relative[1] + acEyeToHead[2][i] * relative[2];
    std::array<float, 4> clip{};
    for (int i = 0; i < 4; ++i)
        clip[i] = acProjection[i][0] * eye[0] + acProjection[i][1] * eye[1] + acProjection[i][2] * eye[2] + acProjection[i][3] * eye[3];
    return clip;
}

/**
 * Find the eye image pixel a point in an eye's clip space lands on.
 *
 * @param acClip The point in the eye's clip space.
 * @param acViewport The eye's part of the eye image.
 * @return The pixel, across and down, or nothing when the point is behind the eye or outside its part of the image.
 */
std::optional<std::pair<UINT, UINT>> PixelOf(const std::array<float, 4>& acClip, const D3D11_VIEWPORT& acViewport) noexcept
{
    if (!(acClip[3] > 0.f))
        return std::nullopt;
    const float cX = acViewport.TopLeftX + (acClip[0] / acClip[3] * 0.5f + 0.5f) * acViewport.Width;
    const float cY = acViewport.TopLeftY + (0.5f - acClip[1] / acClip[3] * 0.5f) * acViewport.Height;
    if (!(cX >= acViewport.TopLeftX && cX < acViewport.TopLeftX + acViewport.Width && cY >= acViewport.TopLeftY && cY < acViewport.TopLeftY + acViewport.Height))
        return std::nullopt;
    return std::make_pair(static_cast<UINT>(cX), static_cast<UINT>(cY));
}

/**
 * Make a square facing the eyes around a point: the dot and the rings.
 *
 * @param acCentre The square's centre in the headset's space.
 * @param aHalf Half its width, in metres.
 * @return Its corners in the headset's space.
 */
HeadCorners Facing(const glm::vec3& acCentre, const float aHalf) noexcept
{
    const glm::vec3 cView = glm::dot(acCentre, acCentre) > 1e-8f ? glm::normalize(acCentre) : glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 right = glm::cross(cView, glm::vec3(0.f, 1.f, 0.f));
    right = glm::dot(right, right) > 1e-8f ? glm::normalize(right) : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 cUp = glm::cross(right, cView);
    return {acCentre - right * aHalf + cUp * aHalf, acCentre + right * aHalf + cUp * aHalf, acCentre + right * aHalf - cUp * aHalf, acCentre - right * aHalf - cUp * aHalf};
}

/**
 * Make a thin strip from one point to another, turned to face the eyes: the beam.
 *
 * @param acFrom One end in the headset's space.
 * @param acTo The other end in the headset's space.
 * @param aHalf Half the strip's width, in metres.
 * @return Its corners in the headset's space, or nothing when the beam points straight at the eyes or has no length.
 */
std::optional<HeadCorners> Strip(const glm::vec3& acFrom, const glm::vec3& acTo, const float aHalf) noexcept
{
    glm::vec3 side = glm::cross(acTo - acFrom, (acFrom + acTo) * 0.5f);
    if (!(glm::dot(side, side) > 1e-12f))
        return std::nullopt;
    side = glm::normalize(side) * aHalf;
    return HeadCorners{acFrom + side, acTo + side, acTo - side, acFrom - side};
}
} // namespace EyeGeometry
