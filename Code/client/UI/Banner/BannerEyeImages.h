#pragma once

#ifdef SKYRIMVR

#include <UI/Banner/BannerPresenter.h>

/**
 * The banner's pictures drawn into the game's own eye images, for a runtime whose overlays cannot take a picture: OpenComposite.
 * They are placed relative to the headset like SteamVR's overlays, but drawn by the game's frames: none during a loading screen.
 */
namespace BannerEyeImages
{
/**
 * Get the presenter that draws the banner's pictures into the game's eye images.
 *
 * @return The presenter; it shows pictures once the eye images can be drawn into, from the game's first frame on.
 */
BannerPresenter& GetPresenter() noexcept;
} // namespace BannerEyeImages

#endif
