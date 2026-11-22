#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Banner/BannerOverlays.h>

#include <Games/Skyrim/VR/VRRuntime.h>

#include <openvr/openvr.h>

#include <array>
#include <cstring>

namespace
{
using BannerPlacement::Layer;

// Every overlay's picture has four bytes a pixel: red, green, blue, alpha.
constexpr uint32_t kBytesPerPixel = 4;

/**
 * Get SteamVR's overlays, kept once found.
 *
 * @return The overlay part; null under OpenComposite, and before VR starts.
 */
vr::IVROverlay* GetOverlay() noexcept
{
    static vr::IVROverlay* s_pOverlay = nullptr;
    if (!s_pOverlay)
        s_pOverlay = VRRuntime::GetOpenVROverlay();
    return s_pOverlay;
}

/**
 * Turn a layer's place into the transform SteamVR takes: both are the same three rows of four.
 *
 * @param acPlacement The layer's place relative to the headset.
 * @return The same place as SteamVR's matrix.
 */
vr::HmdMatrix34_t ToOpenVR(const BannerPlacement::Placement& acPlacement) noexcept
{
    vr::HmdMatrix34_t matrix{};
    std::memcpy(matrix.m, acPlacement.Rows, sizeof(matrix.m));
    return matrix;
}

/**
 * Shows the banner's pictures as SteamVR overlays locked to the head, one overlay for each layer.
 */
class OverlayPresenter final : public BannerPresenter
{
public:
    /**
     * Make a layer's overlay the first time: its width, its place locked to the head, and its sort order.
     *
     * @param aLayer Which layer.
     * @return True when its overlay exists; a failure to make it is logged once.
     */
    bool Prepare(const Layer aLayer) noexcept override
    {
        Overlay& overlay = m_overlays[BannerPlacement::IndexOf(aLayer)];
        if (overlay.Handle != vr::k_ulOverlayHandleInvalid)
            return true;
        vr::IVROverlay* pOverlay = GetOverlay();
        const BannerPlacement::LayerNames& cNames = BannerPlacement::NamesOf(aLayer);
        const auto cError = pOverlay->CreateOverlay(cNames.OverlayKey, cNames.OverlayTitle, &overlay.Handle);
        if (cError != vr::VROverlayError_None)
        {
            if (!overlay.Warned)
                spdlog::warn("VRBanner: the overlay {} could not be created ({})", cNames.OverlayKey, static_cast<int>(cError));
            overlay.Warned = true;
            overlay.Handle = vr::k_ulOverlayHandleInvalid;
            return false;
        }
        const BannerPlacement::Placement cPlacement = BannerPlacement::PlacementOf(aLayer);
        const vr::HmdMatrix34_t cTransform = ToOpenVR(cPlacement);
        pOverlay->SetOverlayWidthInMeters(overlay.Handle, cPlacement.WidthMeters);
        pOverlay->SetOverlayTransformTrackedDeviceRelative(overlay.Handle, vr::k_unTrackedDeviceIndex_Hmd, &cTransform);
        // Its place in the drawing order is its sort order: the banner is drawn over the meter, should the two meet.
        pOverlay->SetOverlaySortOrder(overlay.Handle, static_cast<uint32_t>(BannerPlacement::IndexOf(aLayer)));
        return true;
    }

    /**
     * Give a layer's overlay its picture at once, whether it changes in place or starts a showing.
     * A banner's failure is logged each time; the meter is drawn again at every change, so its failure is logged once.
     *
     * @param aLayer Which layer, already prepared.
     * @param acPicture The picture, straight RGBA, as SteamVR takes it.
     * @param acWhat What the picture shows, for the log.
     * @param aChange Whether it changes a showing layer in place or starts a showing; the same here.
     * @return True when SteamVR took it.
     */
    bool SetPicture(const Layer aLayer, const BannerPicture::Picture& acPicture, const std::string& acWhat, [[maybe_unused]] const PictureChange aChange) noexcept override
    {
        Overlay& overlay = m_overlays[BannerPlacement::IndexOf(aLayer)];
        // SteamVR copies the pixels during the call; its parameter is only not declared const.
        const auto cError = GetOverlay()->SetOverlayRaw(overlay.Handle, const_cast<uint8_t*>(acPicture.Rgba.data()), acPicture.Width, acPicture.Height, kBytesPerPixel);
        if (cError == vr::VROverlayError_None)
            return true;
        if (aLayer == Layer::kBanner)
        {
            spdlog::warn("VRBanner: '{}' could not be drawn ({})", acWhat, static_cast<int>(cError));
            return false;
        }
        if (!overlay.Warned)
            spdlog::warn("VRBanner: the meter '{}' could not be drawn ({})", acWhat, static_cast<int>(cError));
        overlay.Warned = true;
        return false;
    }

    /**
     * Show a layer's overlay.
     *
     * @param aLayer Which layer, with a picture.
     */
    void Show(const Layer aLayer) noexcept override
    {
        GetOverlay()->ShowOverlay(m_overlays[BannerPlacement::IndexOf(aLayer)].Handle);
    }

    /**
     * Set how much of a layer's overlay shows.
     *
     * @param aLayer Which layer.
     * @param aAlpha From 0, nothing, to 1, all of it.
     */
    void SetAlpha(const Layer aLayer, const float aAlpha) noexcept override
    {
        GetOverlay()->SetOverlayAlpha(m_overlays[BannerPlacement::IndexOf(aLayer)].Handle, aAlpha);
    }

    /**
     * Hide a layer's overlay; SteamVR keeps its picture.
     *
     * @param aLayer Which layer.
     */
    void Hide(const Layer aLayer) noexcept override
    {
        GetOverlay()->HideOverlay(m_overlays[BannerPlacement::IndexOf(aLayer)].Handle);
    }

private:
    /**
     * One layer's SteamVR overlay.
     */
    struct Overlay
    {
        vr::VROverlayHandle_t Handle = vr::k_ulOverlayHandleInvalid;
        // A failure is logged once, not at every try.
        bool Warned = false;
    };
    // Each layer's overlay, in drawing order.
    std::array<Overlay, BannerPlacement::kLayerCount> m_overlays{};
};
} // namespace

namespace BannerOverlays
{
/**
 * Get the presenter that shows the banner's pictures as SteamVR overlays.
 *
 * @return The presenter, or null unless the game runs on SteamVR with VR started.
 */
BannerPresenter* GetPresenter() noexcept
{
    static OverlayPresenter s_presenter;
    if (!GetOverlay())
        return nullptr;
    return &s_presenter;
}
} // namespace BannerOverlays

#endif
