#pragma once

#include <algorithm>
#include <cstdint>

/**
 * The menu's colours, shared by the SteamVR tab, the in-game panel and the shaders that draw the panel.
 * Also how those shaders lay a picture over the eye image, for the drawer's checks.
 * Plain numbers, so the tests check them directly.
 */
namespace PanelColours
{
// The dark panel colour behind the page, red, green, blue.
constexpr uint8_t kPanelRgb[3]{18, 21, 24};
// The menu's gold, red, green, blue: the laser and its dot on the in-game panel.
constexpr uint8_t kGoldRgb[3]{200, 169, 110};
// The hand rings' colour on the in-game panel, red, green, blue.
constexpr uint8_t kPaleRgb[3]{235, 235, 235};

/**
 * Lay one pixel of the page over the dark panel colour, as the tab and the in-game panel show it.
 *
 * @param acpPainted The pixel as the browser painted it: blue, green, red and alpha, premultiplied.
 * @param aChannel Which colour to give: 0 red, 1 green, 2 blue.
 * @return That colour of the pixel over the panel, 0 to 255.
 */
inline uint8_t OverPanel(const uint8_t* acpPainted, const int aChannel) noexcept
{
    // Premultiplied: the page's colour plus as much of the panel as the page lets through.
    const uint32_t cPanelShare = 255 - acpPainted[3];
    return static_cast<uint8_t>(std::min<uint32_t>(255, acpPainted[2 - aChannel] + (kPanelRgb[aChannel] * cPanelShare + 127) / 255));
}

/**
 * Lay one pixel of a banner layer over the eye image at the layer's alpha, as the layer's shader and blend do.
 *
 * @param acpPainted The layer's pixel: blue, green, red and alpha, premultiplied.
 * @param aAlpha How much of the layer shows, 0 to 1.
 * @param aBehind That colour of the eye image's pixel before the layer, 0 to 255.
 * @param aChannel Which colour to give: 0 red, 1 green, 2 blue.
 * @return That colour of the pixel with the layer over it, 0 to 255.
 */
inline uint8_t OverImage(const uint8_t* acpPainted, const float aAlpha, const uint8_t aBehind, const int aChannel) noexcept
{
    // Premultiplied: the layer's colour at its alpha, plus as much of the eye image as the layer lets through.
    const float cLetThrough = 1.f - acpPainted[3] / 255.f * aAlpha;
    const float cColour = acpPainted[2 - aChannel] * aAlpha + aBehind * cLetThrough;
    return static_cast<uint8_t>(std::min(255.f, cColour + 0.5f));
}
} // namespace PanelColours
