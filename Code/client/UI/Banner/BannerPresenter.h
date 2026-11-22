#pragma once

#ifdef SKYRIMVR

#include <UI/Banner/BannerPicture.h>
#include <UI/Banner/BannerPlacement.h>

#include <cstdint>
#include <string>

/**
 * How a new picture comes into a layer.
 */
enum class PictureChange : uint8_t
{
    kInPlace,    // the layer shows on at its alpha and place: only its picture changes, at the same size
    kForShowing, // the layer starts a showing with it: its alpha is set next, then Show is called
};

/**
 * Puts the banner's pictures in front of the player: as SteamVR overlays, or into the game's own eye images on OpenComposite.
 * The timing and the fades stay in VRBanner; a presenter gives a layer its picture, shows it, sets how much of it shows, and hides it.
 * Every call comes from the game's update thread.
 */
class BannerPresenter
{
public:
    virtual ~BannerPresenter() = default;

    /**
     * Get a layer ready to show pictures, the first time it is used.
     *
     * @param aLayer Which layer.
     * @return True when it can show pictures; a failure is logged once.
     */
    virtual bool Prepare(BannerPlacement::Layer aLayer) noexcept = 0;

    /**
     * Give a layer the picture it shows from now on, until another is given.
     *
     * @param aLayer Which layer, already prepared.
     * @param acPicture The picture, straight RGBA as BannerPicture draws it.
     * @param acWhat What the picture shows, for the log when it cannot be taken.
     * @param aChange Whether it changes a showing layer in place, or starts a showing that Show completes.
     * @return True when it was taken; a failure is logged.
     */
    virtual bool SetPicture(BannerPlacement::Layer aLayer, const BannerPicture::Picture& acPicture, const std::string& acWhat, PictureChange aChange) noexcept = 0;

    /**
     * Start showing a layer at its place relative to the headset, with the picture and the alpha last given.
     *
     * @param aLayer Which layer, with a picture.
     */
    virtual void Show(BannerPlacement::Layer aLayer) noexcept = 0;

    /**
     * Set how much of a layer shows, for its fades.
     *
     * @param aLayer Which layer.
     * @param aAlpha From 0, nothing, to 1, all of it.
     */
    virtual void SetAlpha(BannerPlacement::Layer aLayer, float aAlpha) noexcept = 0;

    /**
     * Stop showing a layer; its picture is kept.
     *
     * @param aLayer Which layer.
     */
    virtual void Hide(BannerPlacement::Layer aLayer) noexcept = 0;
};

#endif
