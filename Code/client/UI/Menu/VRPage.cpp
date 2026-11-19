#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Menu/VRPage.h>

#include <Services/OverlayService.h>

#include <OverlayClient.hpp>
#include <OverlayRenderHandler.hpp>

namespace
{
/**
 * Receives the browser's paints of the page and hands each picture to the page.
 * The browser paints on its own thread; presenters copy the picture on the main thread.
 */
struct PageRenderHandler final : TiltedPhoques::OverlayRenderHandler
{
    /**
     * Make the receiver for one page, shown from the start: the page itself decides when it is seen.
     *
     * @param aPage The page that keeps the pictures.
     */
    explicit PageRenderHandler(VRPage& aPage)
        : m_page(aPage)
    {
        SetVisible(true);
    }

    /**
     * Set up nothing: the page has no window or device of its own to draw with.
     */
    void Create() override {}

    /**
     * Draw nothing: the presenters draw the picture themselves.
     */
    void Render() override {}

    /**
     * Reset nothing: there is no device to lose.
     */
    void Reset() override {}

    /**
     * Tell the browser how big the page is.
     *
     * @param aRect Set to the page's size, kWidth by kHeight from the top left.
     */
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& aRect) override { aRect = CefRect(0, 0, VRPage::kWidth, VRPage::kHeight); }

    /**
     * Keep a new picture of the whole page; popups and pictures of another size are not the page.
     *
     * @param aType What was painted: the page itself, or a popup over it.
     * @param apBuffer The painted picture, BGRA premultiplied.
     * @param aWidth Its width in pixels.
     * @param aHeight Its height in pixels.
     */
    void OnPaint(CefRefPtr<CefBrowser>, PaintElementType aType, const RectList&, const void* apBuffer, int aWidth, int aHeight) override
    {
        static std::atomic<bool> s_loggedFirstPaint = false;
        if (!s_loggedFirstPaint.exchange(true))
            spdlog::info("VRPage: first page paint, type {}, {}x{}", static_cast<int>(aType), aWidth, aHeight);

        if (aType != PET_VIEW || aWidth != VRPage::kWidth || aHeight != VRPage::kHeight)
            return;
        m_page.StorePaint(apBuffer);
    }

    /**
     * Pass on a text field's request for a keyboard.
     *
     * @param aInputMode What kind of text the field takes; none when no field wants a keyboard any more.
     */
    void OnVirtualKeyboardRequested(CefRefPtr<CefBrowser>, TextInputMode aInputMode) override { m_page.RequestKeyboard(aInputMode != CEF_TEXT_INPUT_MODE_NONE); }

    IMPLEMENT_REFCOUNTING(PageRenderHandler);

private:
    VRPage& m_page;
};

/**
 * Lay a premultiplied picture over the dark panel colour, so it shows opaque.
 *
 * @param acPainted The picture as the browser painted it, BGRA premultiplied.
 * @param aOpaque Filled with the same picture over the panel, BGRA, fully opaque.
 * @return How many of its pixels have any content, as opposed to fully transparent ones.
 */
size_t LayOverPanel(const std::vector<uint8_t>& acPainted, std::vector<uint8_t>& aOpaque) noexcept
{
    aOpaque.resize(acPainted.size());
    size_t contentPixels = 0;
    for (size_t i = 0; i < acPainted.size(); i += 4)
    {
        contentPixels += acPainted[i + 3] != 0;
        // The picture is blue, green, red: its first byte is the panel's third colour.
        for (int c = 0; c < 3; ++c)
            aOpaque[i + c] = VRPage::OverPanel(&acPainted[i], 2 - c);
        aOpaque[i + 3] = 255;
    }
    return contentPixels;
}
} // namespace

/**
 * Make an empty page picture, kWidth by kHeight, ready for the browser's first paint.
 */
VRPage::VRPage() noexcept
{
    m_pixels.resize(static_cast<size_t>(kWidth) * kHeight * 4);
}

/**
 * Make what the browser paints the page into: this page's picture.
 *
 * @return The browser's paint receiver, owned by the browser.
 */
TiltedPhoques::OverlayRenderHandler* VRPage::Create()
{
    return new PageRenderHandler(*this);
}

/**
 * Tell whether one presenter shows the page now.
 *
 * @param aPresenter The SteamVR tab or the in-game panel.
 * @param aShown True while it shows the page.
 */
void VRPage::SetShownBy(const Presenter aPresenter, const bool aShown) noexcept
{
    const uint32_t cWas = m_shownBy;
    m_shownBy = aShown ? (m_shownBy | aPresenter) : (m_shownBy & ~static_cast<uint32_t>(aPresenter));
    // The browser may not repaint an unchanged page when it is shown again: the last picture counts as new for whoever shows it now.
    if (aShown && !(cWas & aPresenter))
    {
        std::scoped_lock lock(m_lock);
        ++m_version;
    }
}

/**
 * Keep a picture the browser just painted. From the browser's paint thread.
 *
 * @param apBuffer The painted picture, kWidth by kHeight, BGRA premultiplied.
 */
void VRPage::StorePaint(const void* apBuffer) noexcept
{
    std::scoped_lock lock(m_lock);
    std::memcpy(m_pixels.data(), apBuffer, m_pixels.size());
    ++m_version;
}

/**
 * Check if a newer picture than a presenter's is here, and give the presenter its number. Call with m_lock held.
 *
 * @param aVersion The number of the presenter's last picture; set to the new one's.
 * @return True when the picture here is newer.
 */
bool VRPage::TakeNewerVersion(uint64_t& aVersion) noexcept
{
    if (m_version == aVersion)
        return false;
    aVersion = m_version;
    return true;
}

/**
 * Copy the newest picture as the browser painted it, when it is newer than the presenter's: premultiplied, mostly transparent.
 * For a presenter that puts the dark panel behind it itself.
 *
 * @param aVersion The number of the presenter's last picture; set to the new one's.
 * @param aPixels Filled with the picture, BGRA.
 * @return True when a newer picture was copied.
 */
bool VRPage::CopyPaintedIfNewer(uint64_t& aVersion, std::vector<uint8_t>& aPixels) noexcept
{
    std::scoped_lock lock(m_lock);
    if (!TakeNewerVersion(aVersion))
        return false;
    aPixels = m_pixels;
    return true;
}

/**
 * Copy the newest picture laid over the dark panel colour, when it is newer than the presenter's: opaque, ready to show.
 * The page is made to sit over the game, so most of it is transparent; on its own it would show as an empty panel.
 *
 * @param aVersion The number of the presenter's last picture; set to the new one's.
 * @param aPixels Filled with the picture, BGRA.
 * @param aContentPercent Set to how much of the page has content, to tell a blank page from a slow one.
 * @return True when a newer picture was copied.
 */
bool VRPage::CopyOverPanelIfNewer(uint64_t& aVersion, std::vector<uint8_t>& aPixels, uint32_t& aContentPercent) noexcept
{
    std::scoped_lock lock(m_lock);
    if (!TakeNewerVersion(aVersion))
        return false;

    const size_t cContentPixels = LayOverPanel(m_pixels, aPixels);
    const size_t cAllPixels = m_pixels.size() / 4;
    aContentPercent = static_cast<uint32_t>(cContentPixels * 100 / cAllPixels);
    return true;
}

/**
 * Tell the browser whether the page is seen, and zoom it the first time it is. Main thread, every frame.
 *
 * @param aOverlay The overlay service, which owns the browser.
 */
void VRPage::Update(OverlayService& aOverlay) noexcept
{
    OverlayApp* pApp = aOverlay.GetOverlayApp();
    const auto pBrowser = pApp && pApp->GetClient() ? pApp->GetClient()->GetBrowser() : nullptr;
    if (!pBrowser || !pBrowser->GetHost())
        return;

    // Only a change is told to the browser.
    const bool cShown = m_shownBy != 0;
    if (m_browserHidden != cShown)
        return;
    m_browserHidden = !cShown;
    pBrowser->GetHost()->WasHidden(m_browserHidden);

    // The page is the flat-screen UI at 1600x900, so on a panel a couple of metres wide its menu is a small box in one corner.
    // Chrome zoom scales everything on it; each level is x1.2, so 3 is about 173%.
    if (cShown && !m_zoomed)
    {
        m_zoomed = true;
        constexpr double kZoomLevel = 3.0;
        pBrowser->GetHost()->SetZoomLevel(kZoomLevel);
        spdlog::info("VRPage: page zoom level set to {}", kZoomLevel);
    }
}

#endif
