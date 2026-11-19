#pragma once

#ifdef SKYRIMVR

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

/**
 * Draws a picture into the game's own eye images just before it hands them to the VR runtime (the compositor's Submit).
 * It works with any runtime, SteamVR or OpenComposite: the runtime only ever gets the game's images.
 * Drawn on the game's own device, on its render thread, with the device's state put back afterwards.
 * The calls below come from the game's update thread, another one.
 * Everything is placed in the game's world, and seen each frame from the headset as the game rendered that frame.
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
 * Check if the panel can be drawn: everything it needs was made on the game's device at its first frame, and nothing failed since.
 *
 * @return True once the panel can be shown.
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
 * Stop drawing the panel and everything over it.
 */
void Hide() noexcept;

/**
 * Give what pointing draws over the panel: the beam, the dot and the hand rings.
 *
 * @param acPointing What to draw; an empty one draws none of them.
 */
void SetPointing(const Pointing& acPointing) noexcept;
} // namespace VREyeDraw

#endif
