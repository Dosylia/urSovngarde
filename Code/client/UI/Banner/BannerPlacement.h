#pragma once

#include <UI/Menu/EyeGeometry.h>

#include <cstddef>
#include <cstdint>

/**
 * Where the banner and the meter are shown relative to the headset: the same places for SteamVR's overlays and for the game's eye images.
 * Plain numbers, so the tests check them directly.
 * The headset's space counts in metres, with x right, y up and -z ahead, as the VR runtime does.
 */
namespace BannerPlacement
{
/**
 * The two pictures shown at once, each in its own place, in drawing order: where they meet, the later one is drawn over the earlier.
 */
enum class Layer : uint8_t
{
    kMeter,  // the siege meter, near the top of the view
    kBanner, // a line of text or the death card, in the middle of the view
};

// How many layers there are, for arrays kept in drawing order.
constexpr size_t kLayerCount = 2;

/**
 * Find a layer's place in arrays kept in drawing order.
 *
 * @param aLayer Which layer.
 * @return 0 for the meter, 1 for the banner.
 */
constexpr size_t IndexOf(const Layer aLayer) noexcept
{
    return static_cast<size_t>(aLayer);
}

/**
 * What a layer is called: in the log, and as a SteamVR overlay.
 */
struct LayerNames
{
    // In the log: "meter" or "banner".
    const char* Log;
    // The SteamVR overlay's key, unique among every program's overlays.
    const char* OverlayKey;
    // The SteamVR overlay's name, as SteamVR shows it to people.
    const char* OverlayTitle;
};

// Each layer's names, in drawing order.
constexpr LayerNames kLayerNames[kLayerCount]{
    {"meter", "ursovngarde.meter", "urSovngarde meter"},
    {"banner", "ursovngarde.banner", "urSovngarde banner"},
};

/**
 * Find what a layer is called.
 *
 * @param aLayer Which layer.
 * @return Its names in the log and as a SteamVR overlay.
 */
constexpr const LayerNames& NamesOf(const Layer aLayer) noexcept
{
    return kLayerNames[IndexOf(aLayer)];
}

/**
 * A picture's place relative to the headset, the way the VR runtime places an overlay locked to the head.
 */
struct Placement
{
    // Turns the picture's own plane into the headset's space: three rows of four, the last column where its centre is.
    // In its own plane, x runs right and y up across the picture, which faces +z.
    float Rows[3][4]{};
    // The picture's width in metres; its height follows its proportions.
    float WidthMeters = 0.f;
};

/**
 * Find where a layer is shown: the banner 1.6 m wide, 1.5 m ahead and a little below the eyes; the meter 0.9 m wide near the top of the view, tilted to face the eyes.
 *
 * @param aLayer Which layer.
 * @return Its place relative to the headset.
 */
Placement PlacementOf(Layer aLayer) noexcept;

/**
 * Find a picture's corners at its place, in the headset's space.
 *
 * @param acPlacement Where the picture is shown.
 * @param aPictureWidth The picture's width in pixels, for its proportions.
 * @param aPictureHeight Its height in pixels.
 * @return Its corners in metres, from the top left, clockwise as the player faces it; a picture with no width gets no height.
 */
EyeGeometry::HeadCorners CornersOf(const Placement& acPlacement, int aPictureWidth, int aPictureHeight) noexcept;
} // namespace BannerPlacement
