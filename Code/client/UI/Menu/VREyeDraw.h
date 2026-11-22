#pragma once

#ifdef SKYRIMVR

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include <UI/Banner/BannerPlacement.h>

/**
 * Draws pictures into the game's own eye images just before it hands them to the VR runtime (the compositor's Submit).
 * It works with any runtime, SteamVR or OpenComposite: the runtime only ever gets the game's images.
 * Drawn on the game's own device, on its render thread, with the device's state put back afterwards.
 * The calls below come from the game's update thread, another one.
 * The menu panel is placed in the game's world, and seen each frame from the headset as the game rendered that frame.
 * The banner's layers are placed relative to the headset instead, so they stay at one place in the view; they are drawn over the panel.
 */
namespace VREyeDraw
{
/**
 * A rectangle in the game's world: game units, x right, y ahead, z up.
 * Corners from the top left, clockwise as the player faces it.
 */
struct Quad
{
    std::array<glm::vec3, 4> Corners{};
};

/**
 * What pointing at the panel draws over it, in the game's world; each part only when given.
 */
struct Pointing
{
    // Where the laser lands on the panel: a round dot.
    std::optional<glm::vec3> Dot;
    // The laser, from the controller to where it lands, or a short way ahead when it misses the panel.
    std::optional<std::array<glm::vec3, 2>> Beam;
    // Each hand's controller, left then right: a ring, seen even where the panel covers the hand.
    std::array<std::optional<glm::vec3>, 2> Hands;
};

/**
 * Check if the eye images can be drawn into: everything the panel and the banner's layers need was made on the game's device at its first frame, and nothing failed since.
 * After a failure neither shows for the rest of the game: no panel, and on OpenComposite no banners.
 *
 * @return True once the panel and the banner's layers can be shown.
 */
bool IsReady() noexcept;

/**
 * Give the picture to draw on the panel, from the next frame on.
 * It is taken by swapping, so the caller gets an older buffer back to fill next time.
 *
 * @param aPixels The whole picture, BGRA premultiplied as the browser paints it; ignored when its size does not match the width and height.
 * @param aWidth Its width in pixels.
 * @param aHeight Its height in pixels.
 */
void SetPicture(std::vector<uint8_t>& aPixels, uint32_t aWidth, uint32_t aHeight) noexcept;

/**
 * Show the panel at a place in the game's world, from the next frame on, until it is hidden.
 *
 * @param acQuad The panel's corners in the game's world.
 */
void Show(const Quad& acQuad) noexcept;

/**
 * Stop drawing the panel and the pointing over it; the banner's layers are not touched.
 */
void Hide() noexcept;

/**
 * Give what pointing draws over the panel: the beam, the dot and the hand rings.
 *
 * @param acPointing What to draw; an empty one draws none of them.
 */
void SetPointing(const Pointing& acPointing) noexcept;

/**
 * Change the picture of one of the banner's layers while it shows, from the next frame on; its place and alpha stay.
 * It is taken by swapping, so the caller gets an older buffer back to fill next time.
 *
 * @param aLayer Which layer.
 * @param aPixels The whole picture, BGRA premultiplied like the panel's, top row first; ignored when its size does not match the width and height.
 * @param aWidth Its width in pixels.
 * @param aHeight Its height in pixels.
 */
void SetLayerPicture(BannerPlacement::Layer aLayer, std::vector<uint8_t>& aPixels, uint32_t aWidth, uint32_t aHeight) noexcept;

/**
 * Show one of the banner's layers from the next frame on, until it is hidden: its picture, its place relative to the headset and its alpha, in one handover.
 * A frame draws all of the old or all of the new, never a new picture at an old place or alpha.
 *
 * @param aLayer Which layer.
 * @param aPixels A new picture, BGRA premultiplied, top row first, taken by swapping; empty to keep the picture the layer has.
 * @param aWidth The new picture's width in pixels.
 * @param aHeight Its height in pixels.
 * @param acCorners The layer's corners in the headset's space, in metres, from the top left, clockwise as the player faces it.
 * @param aAlpha How much of it shows, from 0 to 1.
 */
void ShowLayer(BannerPlacement::Layer aLayer, std::vector<uint8_t>& aPixels, uint32_t aWidth, uint32_t aHeight, const EyeGeometry::HeadCorners& acCorners, float aAlpha) noexcept;

/**
 * Set how much of one of the banner's layers shows, for its fades; kept while it is hidden.
 *
 * @param aLayer Which layer.
 * @param aAlpha From 0, nothing, to 1, all of it.
 */
void SetLayerAlpha(BannerPlacement::Layer aLayer, float aAlpha) noexcept;

/**
 * Stop drawing one of the banner's layers; its picture and alpha are kept for its next showing.
 *
 * @param aLayer Which layer.
 */
void HideLayer(BannerPlacement::Layer aLayer) noexcept;
} // namespace VREyeDraw

#endif
