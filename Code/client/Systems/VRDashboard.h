#pragma once

#ifdef SKYRIMVR

#include <chrono>
#include <cstdint>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct OverlayService;
struct VRPage;

namespace vr
{
class IVRSystem;
class IVROverlay;
}

/**
 * Shows the menu page (VRPage) as a SteamVR dashboard tab, "urSovngarde", opened with the headset's system button.
 * The picture lives on our own graphics device, apart from the game's renderer and its render thread.
 * SteamVR's laser pointer sends mouse events, and its own keyboard types into text fields.
 * Under OpenComposite there is no dashboard, and the tab stays off.
 */
struct VRDashboard final
{
    VRDashboard() noexcept = default;

    /**
     * Remove the tab from SteamVR and release our graphics device and picture.
     */
    ~VRDashboard();

    TP_NOCOPYMOVE(VRDashboard);

    /**
     * Keep the tab going: set it up once SteamVR is ready, send it new pictures of the page, and pass its input to the page.
     * Main thread, every frame.
     *
     * @param aOverlay The overlay service, which owns the page's browser.
     * @param aPage The menu page as pictures.
     */
    void Update(OverlayService& aOverlay, VRPage& aPage) noexcept;

private:
    /**
     * Set the tab up, trying again every 2 seconds while OpenVR comes up, and giving up after 10 tries.
     *
     * @return True once the tab is ready.
     */
    bool TryToStart() noexcept;

    /**
     * Set the tab up: SteamVR's parts, a graphics device on the headset's GPU, the shared picture, and the tab itself.
     *
     * @return True when everything was made.
     */
    bool InitializeOverlay() noexcept;

    /**
     * Make our own graphics device, on the GPU the headset is connected to, as SteamVR needs the picture there.
     *
     * @return True when the device is there, made now or by an earlier try.
     */
    bool MakeDevice() noexcept;

    /**
     * Make the picture SteamVR shows in the tab, shared so SteamVR can open it from its own process.
     *
     * @return True when the picture is there, made now or by an earlier try.
     */
    bool MakeSharedPicture() noexcept;

    /**
     * Create the dashboard tab, with its size, its mouse input, and its icon.
     *
     * @return True when SteamVR created the tab.
     */
    bool CreateTab() noexcept;

    /**
     * Send SteamVR the newest picture of the page, when there is one it has not had.
     *
     * @param aPage The menu page as pictures.
     */
    void UploadFrame(VRPage& aPage) noexcept;

    /**
     * Pass the tab's events to the page and the menu: mouse, scroll, typed text, and the tab being opened or closed.
     *
     * @param aOverlay The overlay service, which owns the page's browser.
     * @param aPage The menu page as pictures.
     */
    void ProcessEvents(OverlayService& aOverlay, VRPage& aPage) noexcept;

    /**
     * Show SteamVR's keyboard when a text field on the page asks for one while the tab is open, and hide it when asked.
     *
     * @param aPage The menu page, which holds the field's request.
     */
    void ForwardKeyboardRequest(VRPage& aPage) noexcept;

    std::vector<uint8_t> m_pixels;
    uint64_t m_pictureVersion{0};

    vr::IVRSystem* m_pSystem = nullptr;
    vr::IVROverlay* m_pOverlay = nullptr;
    uint64_t m_handle = 0;
    uint64_t m_thumbnailHandle = 0;

    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    ID3D11Texture2D* m_pTexture = nullptr;

    bool m_initialized = false;
    bool m_failed = false;
    int m_startAttempts = 0;
    std::chrono::steady_clock::time_point m_nextStartAttempt{};
    bool m_visible = false;
    bool m_logNextUpload = false;
    uint16_t m_mouseX = 0;
    uint16_t m_mouseY = 0;
};

#endif
