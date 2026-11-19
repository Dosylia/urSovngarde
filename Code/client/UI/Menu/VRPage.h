#pragma once

#ifdef SKYRIMVR

#include <OverlayApp.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

struct OverlayService;

/**
 * The menu page as pictures, for the two places VR shows it: the SteamVR tab and the panel inside the game.
 * There is no game window to draw the page into on VR, so the browser renders it offscreen at a fixed size.
 * Each new picture gets a number, so each presenter copies only the pictures it has not had yet.
 * The page renders only while a presenter shows it, to save frame time.
 */
struct VRPage final : TiltedPhoques::OverlayApp::RenderProvider
{
    // The page's size in pixels: the flat-screen menu's own size.
    static constexpr int kWidth = 1600;
    static constexpr int kHeight = 900;

    // The dark panel colour behind the page, red, green, blue: the SteamVR tab and the in-game panel both use it.
    static constexpr uint8_t kPanelRgb[3]{18, 21, 24};
    // The menu's gold, red, green, blue: the laser and its dot on the in-game panel.
    static constexpr uint8_t kGoldRgb[3]{200, 169, 110};

    /**
     * What the page's text fields last asked of a keyboard.
     */
    enum class KeyboardRequest : int
    {
        kNone,
        kShow,
        kHide,
    };

    /**
     * Who shows the page; it renders while any of them does.
     */
    enum Presenter : uint32_t
    {
        kDashboard = 1,
        kPanel = 2,
    };

    /**
     * Make an empty page picture, kWidth by kHeight, ready for the browser's first paint.
     */
    VRPage() noexcept;
    ~VRPage() override = default;

    TP_NOCOPYMOVE(VRPage);

    /**
     * Make what the browser paints the page into: this page's picture.
     *
     * @return The browser's paint receiver, owned by the browser.
     */
    TiltedPhoques::OverlayRenderHandler* Create() override;

    /**
     * Tell the browser there is no window to draw into: the page is drawn offscreen.
     *
     * @return Always null.
     */
    HWND GetWindow() override { return nullptr; }

    /**
     * Tell whether one presenter shows the page now.
     *
     * @param aPresenter The SteamVR tab or the in-game panel.
     * @param aShown True while it shows the page.
     */
    void SetShownBy(Presenter aPresenter, bool aShown) noexcept;

    /**
     * Tell the browser whether the page is seen, and zoom it the first time it is. Main thread, every frame.
     *
     * @param aOverlay The overlay service, which owns the browser.
     */
    void Update(OverlayService& aOverlay) noexcept;

    /**
     * Copy the newest picture as the browser painted it, when it is newer than the presenter's: premultiplied, mostly transparent.
     * For a presenter that puts the dark panel behind it itself.
     *
     * @param aVersion The number of the presenter's last picture; set to the new one's.
     * @param aPixels Filled with the picture, BGRA.
     * @return True when a newer picture was copied.
     */
    bool CopyPaintedIfNewer(uint64_t& aVersion, std::vector<uint8_t>& aPixels) noexcept;

    /**
     * Copy the newest picture laid over the dark panel colour, when it is newer than the presenter's: opaque, ready to show.
     * The page is made to sit over the game, so most of it is transparent; on its own it would show as an empty panel.
     *
     * @param aVersion The number of the presenter's last picture; set to the new one's.
     * @param aPixels Filled with the picture, BGRA.
     * @param aContentPercent Set to how much of the page has content, to tell a blank page from a slow one.
     * @return True when a newer picture was copied.
     */
    bool CopyOverPanelIfNewer(uint64_t& aVersion, std::vector<uint8_t>& aPixels, uint32_t& aContentPercent) noexcept;

    /**
     * Lay one pixel of the page over the dark panel colour, as the tab and the in-game panel show it.
     *
     * @param acpPainted The pixel as the browser painted it: blue, green, red and alpha, premultiplied.
     * @param aChannel Which colour to give: 0 red, 1 green, 2 blue.
     * @return That colour of the pixel over the panel, 0 to 255.
     */
    static uint8_t OverPanel(const uint8_t* acpPainted, const int aChannel) noexcept
    {
        // Premultiplied: the page's colour plus as much of the panel as the page lets through.
        const uint32_t cPanelShare = 255 - acpPainted[3];
        return static_cast<uint8_t>(std::min<uint32_t>(255, acpPainted[2 - aChannel] + (kPanelRgb[aChannel] * cPanelShare + 127) / 255));
    }

    /**
     * Take the browser's last request for a keyboard.
     *
     * @return Show or hide one, or none when no request came since the last call.
     */
    KeyboardRequest TakeKeyboardRequest() noexcept { return static_cast<KeyboardRequest>(m_keyboardRequest.exchange(static_cast<int>(KeyboardRequest::kNone))); }

    /**
     * Keep a picture the browser just painted. From the browser's paint thread.
     *
     * @param apBuffer The painted picture, kWidth by kHeight, BGRA premultiplied.
     */
    void StorePaint(const void* apBuffer) noexcept;

    /**
     * Keep the browser's request for a keyboard, for the presenter to take. From the browser's thread.
     *
     * @param aShow True when a text field wants a keyboard.
     */
    void RequestKeyboard(bool aShow) noexcept { m_keyboardRequest = static_cast<int>(aShow ? KeyboardRequest::kShow : KeyboardRequest::kHide); }

private:
    /**
     * Check if a newer picture than a presenter's is here, and give the presenter its number. Call with m_lock held.
     *
     * @param aVersion The number of the presenter's last picture; set to the new one's.
     * @return True when the picture here is newer.
     */
    bool TakeNewerVersion(uint64_t& aVersion) noexcept;

    std::mutex m_lock;
    std::vector<uint8_t> m_pixels;
    uint64_t m_version{0};
    std::atomic<int> m_keyboardRequest{static_cast<int>(KeyboardRequest::kNone)};
    uint32_t m_shownBy{0};
    bool m_browserHidden{false};
    bool m_zoomed{false};
};

#endif
