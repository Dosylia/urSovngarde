#include <UI/Banner/BannerPlacement.h>

#include <cmath>

namespace BannerPlacement
{
namespace
{
// The banner is 1.6 m wide, 1.5 m in front of the eyes and a little below them: the middle of the view, readable without turning.
constexpr float kWidthMeters = 1.6f;
constexpr float kDistanceMeters = 1.5f;
constexpr float kBelowEyesMeters = 0.05f;
// The meter is 0.9 m wide at the same distance, near the top of the view, clear of the banner and tilted to face the eyes.
constexpr float kMeterWidthMeters = 0.9f;
constexpr float kMeterAboveEyesMeters = 0.45f;

/**
 * Place the banner: upright, facing the eyes, its centre ahead and a little below them.
 *
 * @return The banner's place.
 */
Placement BannerPlace() noexcept
{
    return {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, -kBelowEyesMeters}, {0.f, 0.f, 1.f, -kDistanceMeters}}, kWidthMeters};
}

/**
 * Place the meter: above the eyes, turned about the x axis until it faces them, its top leaning towards them.
 *
 * @return The meter's place.
 */
Placement MeterPlace() noexcept
{
    const float cTilt = std::atan2(kMeterAboveEyesMeters, kDistanceMeters);
    const float cCos = std::cos(cTilt);
    const float cSin = std::sin(cTilt);
    return {{{1.f, 0.f, 0.f, 0.f}, {0.f, cCos, -cSin, kMeterAboveEyesMeters}, {0.f, cSin, cCos, -kDistanceMeters}}, kMeterWidthMeters};
}
} // namespace

/**
 * Find where a layer is shown: the banner 1.6 m wide, 1.5 m ahead and a little below the eyes; the meter 0.9 m wide near the top of the view, tilted to face the eyes.
 *
 * @param aLayer Which layer.
 * @return Its place relative to the headset.
 */
Placement PlacementOf(const Layer aLayer) noexcept
{
    if (aLayer == Layer::kMeter)
        return MeterPlace();
    return BannerPlace();
}

/**
 * Find a picture's corners at its place, in the headset's space.
 *
 * @param acPlacement Where the picture is shown.
 * @param aPictureWidth The picture's width in pixels, for its proportions.
 * @param aPictureHeight Its height in pixels.
 * @return Its corners in metres, from the top left, clockwise as the player faces it; a picture with no width gets no height.
 */
EyeGeometry::HeadCorners CornersOf(const Placement& acPlacement, const int aPictureWidth, const int aPictureHeight) noexcept
{
    const float cHalfWidth = acPlacement.WidthMeters * 0.5f;
    const float cHalfHeight = aPictureWidth > 0 ? cHalfWidth * static_cast<float>(aPictureHeight) / static_cast<float>(aPictureWidth) : 0.f;
    // The corners in the picture's own plane: top left, top right, bottom right, bottom left.
    const float cPlane[4][2] = {{-cHalfWidth, cHalfHeight}, {cHalfWidth, cHalfHeight}, {cHalfWidth, -cHalfHeight}, {-cHalfWidth, -cHalfHeight}};
    EyeGeometry::HeadCorners corners{};
    for (size_t i = 0; i < corners.size(); ++i)
    {
        // The plane's z is 0, so the third column takes no part.
        for (int row = 0; row < 3; ++row)
            corners[i][row] = acPlacement.Rows[row][0] * cPlane[i][0] + acPlacement.Rows[row][1] * cPlane[i][1] + acPlacement.Rows[row][3];
    }
    return corners;
}
} // namespace BannerPlacement
