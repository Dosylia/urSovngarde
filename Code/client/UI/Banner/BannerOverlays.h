#pragma once

#ifdef SKYRIMVR

#include <UI/Banner/BannerPresenter.h>

/**
 * The banner's pictures as SteamVR overlays locked to the head: SteamVR draws them above the game, black screen included.
 */
namespace BannerOverlays
{
/**
 * Get the presenter that shows the banner's pictures as SteamVR overlays.
 *
 * @return The presenter, or null unless the game runs on SteamVR with VR started.
 */
BannerPresenter* GetPresenter() noexcept;
} // namespace BannerOverlays

#endif
