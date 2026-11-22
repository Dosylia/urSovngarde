#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Banner/BannerEyeImages.h>

#include <UI/Menu/VREyeDraw.h>

#include <array>
#include <vector>

namespace
{
using BannerPlacement::Layer;

/**
 * Shows the banner's pictures in the game's own eye images, through the drawer that draws the menu panel there.
 * Each layer keeps its place relative to the headset, so it stays at one place in the view as the head turns.
 * A new showing's picture, place and alpha reach the drawer in one handover, so no frame mixes old and new.
 */
class EyeImagePresenter final : public BannerPresenter
{
public:
    /**
     * Check if the eye images can be drawn into: the drawer made what it needs on the game's device at its first frame.
     *
     * @param aLayer Which layer; both depend on the drawer alone.
     * @return True when pictures can be shown; false before the game's first frame, or after the drawer failed, logged once.
     */
    bool Prepare([[maybe_unused]] const Layer aLayer) noexcept override
    {
        if (VREyeDraw::IsReady())
            return true;
        if (!m_warned)
            spdlog::warn("VRBanner: the game's eye images cannot be drawn into now; the banner is not shown");
        m_warned = true;
        return false;
    }

    /**
     * Give a layer its picture in the drawer's form, BGRA premultiplied, written into the buffer the drawer last gave back.
     * In place, it goes to the drawer at once; the layer keeps its place and alpha, unless the picture's size changed.
     * For a showing, it waits for Show, which hands it over with the place and the alpha.
     *
     * @param aLayer Which layer.
     * @param acPicture The picture, straight RGBA.
     * @param acWhat What the picture shows, for the log.
     * @param aChange Whether it changes a showing layer in place, or starts a showing.
     * @return True when it was taken; false for a picture whose pixels do not match its size, logged.
     */
    bool SetPicture(const Layer aLayer, const BannerPicture::Picture& acPicture, const std::string& acWhat, const PictureChange aChange) noexcept override
    {
        LayerState& layer = m_layers[BannerPlacement::IndexOf(aLayer)];
        if (!BannerPicture::ToPremultipliedBgra(acPicture, layer.Pixels))
        {
            spdlog::warn("VRBanner: '{}' could not be drawn into the eye images", acWhat);
            return false;
        }
        const bool cResized = FitTo(aLayer, layer, acPicture);
        if (aChange == PictureChange::kForShowing)
        {
            layer.IsPictureWaiting = true;
            return true;
        }
        // The drawer swaps the buffer for an older one, which the next picture is written into.
        if (cResized)
            VREyeDraw::ShowLayer(aLayer, layer.Pixels, layer.Width, layer.Height, layer.Corners, layer.Alpha);
        else
            VREyeDraw::SetLayerPicture(aLayer, layer.Pixels, layer.Width, layer.Height);
        return true;
    }

    /**
     * Start drawing a layer: the picture waiting for this showing, if any, its place and its alpha, in one handover.
     *
     * @param aLayer Which layer, with a picture.
     */
    void Show(const Layer aLayer) noexcept override
    {
        LayerState& layer = m_layers[BannerPlacement::IndexOf(aLayer)];
        // Without a waiting picture, the layer shows the one the drawer already has.
        std::vector<uint8_t> noNewPicture;
        std::vector<uint8_t>& pixels = layer.IsPictureWaiting ? layer.Pixels : noNewPicture;
        VREyeDraw::ShowLayer(aLayer, pixels, layer.Width, layer.Height, layer.Corners, layer.Alpha);
        layer.IsPictureWaiting = false;
    }

    /**
     * Set how much of a layer the drawer lays over the eye images; while a picture waits for Show, it goes over with it.
     *
     * @param aLayer Which layer.
     * @param aAlpha From 0, nothing, to 1, all of it.
     */
    void SetAlpha(const Layer aLayer, const float aAlpha) noexcept override
    {
        LayerState& layer = m_layers[BannerPlacement::IndexOf(aLayer)];
        layer.Alpha = aAlpha;
        if (!layer.IsPictureWaiting)
            VREyeDraw::SetLayerAlpha(aLayer, aAlpha);
    }

    /**
     * Stop drawing a layer; the drawer keeps its picture.
     *
     * @param aLayer Which layer.
     */
    void Hide(const Layer aLayer) noexcept override
    {
        VREyeDraw::HideLayer(aLayer);
    }

private:
    /**
     * One layer as this presenter keeps it.
     */
    struct LayerState
    {
        // The picture in the drawer's form; after a handover, the older buffer the drawer gave back.
        std::vector<uint8_t> Pixels;
        uint32_t Width = 0;
        uint32_t Height = 0;
        // Whether Pixels holds a picture for the showing Show starts, not handed over yet.
        bool IsPictureWaiting = false;
        // The picture's corners in the headset's space, fitted to its proportions.
        EyeGeometry::HeadCorners Corners{};
        // The alpha last set, from 0 to 1.
        float Alpha = 0.f;
    };

    /**
     * Fit a layer's size and corners to a new picture.
     *
     * @param aLayer Which layer.
     * @param aState The layer as kept here; its size and corners change with the picture's size.
     * @param acPicture The new picture.
     * @return True when the picture's size differs from the one before.
     */
    static bool FitTo(const Layer aLayer, LayerState& aState, const BannerPicture::Picture& acPicture) noexcept
    {
        const auto cWidth = static_cast<uint32_t>(acPicture.Width);
        const auto cHeight = static_cast<uint32_t>(acPicture.Height);
        if (cWidth == aState.Width && cHeight == aState.Height)
            return false;
        aState.Width = cWidth;
        aState.Height = cHeight;
        aState.Corners = BannerPlacement::CornersOf(BannerPlacement::PlacementOf(aLayer), acPicture.Width, acPicture.Height);
        return true;
    }

    // Each layer, in drawing order.
    std::array<LayerState, BannerPlacement::kLayerCount> m_layers{};
    // A refusal is logged once, not at every banner.
    bool m_warned = false;
};
} // namespace

namespace BannerEyeImages
{
/**
 * Get the presenter that draws the banner's pictures into the game's eye images.
 *
 * @return The presenter; it shows pictures once the eye images can be drawn into, from the game's first frame on.
 */
BannerPresenter& GetPresenter() noexcept
{
    static EyeImagePresenter s_presenter;
    return s_presenter;
}
} // namespace BannerEyeImages

#endif
