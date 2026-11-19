#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <Systems/VRDashboard.h>
#include <Systems/VRPage.h>
#include <Systems/PageInput.h>

#include <Services/OverlayService.h>
#include <Services/MenuService.h>
#include <Games/Skyrim/VRRuntime.h>
#include <World.h>
#include <SafeRelease.h>

#include <OverlayClient.hpp>

#include <openvr/openvr.h>

#include <d3d11.h>
#include <dxgi.h>

namespace
{
// The tab icon's size in pixels, and its background colour, red, green, blue.
constexpr int32_t kIconSize = 64;
constexpr uint8_t kIconBackgroundRgb[3]{24, 21, 18};

/**
 * Draw the tab's icon, so it needs no image file: two gold diamond rings on a dark square with its corners cut off.
 * It stays readable at the dashboard's thumbnail size.
 *
 * @return The icon, kIconSize pixels square, RGBA.
 */
std::vector<uint8_t> DrawTabIcon() noexcept
{
    constexpr float kCentre = (kIconSize - 1) / 2.f;
    std::vector<uint8_t> icon(static_cast<size_t>(kIconSize) * kIconSize * 4);
    for (int32_t y = 0; y < kIconSize; ++y)
    {
        for (int32_t x = 0; x < kIconSize; ++x)
        {
            // Distances from the centre: along both axes added up, which draws diamonds, and the larger of the two, which draws a square.
            const float cAcross = static_cast<float>(x) - kCentre;
            const float cDown = static_cast<float>(y) - kCentre;
            const float cDiamond = std::abs(cAcross) + std::abs(cDown);
            const float cSquare = std::max(std::abs(cAcross), std::abs(cDown));

            const bool cOuterRing = cDiamond <= 26.f && cDiamond > 21.f;
            const bool cInnerRing = cDiamond <= 13.f && cDiamond > 9.f;
            const uint8_t* pColour = cOuterRing || cInnerRing ? VRPage::kGoldRgb : kIconBackgroundRgb;

            uint8_t* pPixel = icon.data() + (static_cast<size_t>(y) * kIconSize + x) * 4;
            pPixel[0] = pColour[0];
            pPixel[1] = pColour[1];
            pPixel[2] = pColour[2];
            // The square's corners are cut off.
            pPixel[3] = cSquare > 30.f ? 0 : 255;
        }
    }
    return icon;
}
} // namespace

/**
 * Remove the tab from SteamVR and release our graphics device and picture.
 */
VRDashboard::~VRDashboard()
{
    if (m_pOverlay && m_handle)
        m_pOverlay->DestroyOverlay(m_handle);

    SafeRelease(m_pTexture);
    SafeRelease(m_pContext);
    SafeRelease(m_pDevice);
}

/**
 * Keep the tab going: set it up once SteamVR is ready, send it new pictures of the page, and pass its input to the page.
 * Main thread, every frame.
 *
 * @param aOverlay The overlay service, which owns the page's browser.
 * @param aPage The menu page as pictures.
 */
void VRDashboard::Update(OverlayService& aOverlay, VRPage& aPage) noexcept
{
    if (m_failed)
        return;
    if (!m_initialized && !TryToStart())
        return;

    ProcessEvents(aOverlay, aPage);
    if (m_visible)
        UploadFrame(aPage);
    ForwardKeyboardRequest(aPage);
}

/**
 * Set the tab up, trying again every 2 seconds while OpenVR comes up, and giving up after 10 tries.
 *
 * @return True once the tab is ready.
 */
bool VRDashboard::TryToStart() noexcept
{
    // OpenComposite has no dashboard to put the menu on.
    if (VRRuntime::IsOpenComposite())
    {
        m_failed = true;
        return false;
    }

    // OpenVR can come up after the first frames.
    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < m_nextStartAttempt)
        return false;
    m_nextStartAttempt = cNow + std::chrono::seconds(2);

    m_initialized = InitializeOverlay();
    if (!m_initialized && ++m_startAttempts >= 10)
    {
        spdlog::error("VRDashboard: giving up, the Skyrim Together menu is not available in the headset");
        m_failed = true;
    }
    return m_initialized;
}

/**
 * Set the tab up: SteamVR's parts, a graphics device on the headset's GPU, the shared picture, and the tab itself.
 *
 * @return True when everything was made.
 */
bool VRDashboard::InitializeOverlay() noexcept
{
    int error = 0;
    m_pSystem = static_cast<vr::IVRSystem*>(VRRuntime::GetOpenVRInterface(vr::IVRSystem_Version, &error));
    m_pOverlay = VRRuntime::GetOpenVROverlay();
    if (!m_pSystem || !m_pOverlay)
    {
        spdlog::warn("VRDashboard: OpenVR interfaces unavailable (error {})", error);
        return false;
    }

    if (!MakeDevice() || !MakeSharedPicture() || !CreateTab())
        return false;

    spdlog::info("VRDashboard: Skyrim Together dashboard overlay created");
    return true;
}

/**
 * Make our own graphics device, on the GPU the headset is connected to, as SteamVR needs the picture there.
 *
 * @return True when the device is there, made now or by an earlier try.
 */
bool VRDashboard::MakeDevice() noexcept
{
    // Made once: a later try after another step failed keeps it.
    if (m_pDevice)
        return true;

    int32_t adapterIndex = -1;
    m_pSystem->GetDXGIOutputInfo(&adapterIndex);

    IDXGIFactory1* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory))) && adapterIndex >= 0)
        pFactory->EnumAdapters1(adapterIndex, &pAdapter);

    // Without the headset's GPU, the default one.
    const HRESULT cResult = D3D11CreateDevice(pAdapter, pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pContext);

    if (pAdapter)
        pAdapter->Release();
    if (pFactory)
        pFactory->Release();

    if (FAILED(cResult))
    {
        spdlog::error("VRDashboard: D3D11CreateDevice failed ({:#x})", static_cast<uint32_t>(cResult));
        return false;
    }
    return true;
}

/**
 * Make the picture SteamVR shows in the tab, shared so SteamVR can open it from its own process.
 *
 * @return True when the picture is there, made now or by an earlier try.
 */
bool VRDashboard::MakeSharedPicture() noexcept
{
    // Made once: a later try after another step failed keeps it.
    if (m_pTexture)
        return true;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = VRPage::kWidth;
    desc.Height = VRPage::kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // as the browser paints it
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // SteamVR opens it from its own process

    if (FAILED(m_pDevice->CreateTexture2D(&desc, nullptr, &m_pTexture)))
    {
        spdlog::error("VRDashboard: could not create the overlay texture");
        return false;
    }
    return true;
}

/**
 * Create the dashboard tab, with its size, its mouse input, and its icon.
 *
 * @return True when SteamVR created the tab.
 */
bool VRDashboard::CreateTab() noexcept
{
    const auto cError = m_pOverlay->CreateDashboardOverlay("skyrimtogether.menu", "urSovngarde", &m_handle, &m_thumbnailHandle);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::error("VRDashboard: CreateDashboardOverlay failed ({})", static_cast<int>(cError));
        return false;
    }

    m_pOverlay->SetOverlayWidthInMeters(m_handle, 2.5f);

    // SteamVR's laser works the tab like a mouse, in the page's own pixels.
    m_pOverlay->SetOverlayInputMethod(m_handle, vr::VROverlayInputMethod_Mouse);
    const vr::HmdVector2_t cMouseScale{{static_cast<float>(VRPage::kWidth), static_cast<float>(VRPage::kHeight)}};
    m_pOverlay->SetOverlayMouseScale(m_handle, &cMouseScale);
    m_pOverlay->SetOverlayFlag(m_handle, vr::VROverlayFlags_SendVRSmoothScrollEvents, true);

    std::vector<uint8_t> icon = DrawTabIcon();
    m_pOverlay->SetOverlayRaw(m_thumbnailHandle, icon.data(), kIconSize, kIconSize, 4);
    return true;
}

/**
 * Send SteamVR the newest picture of the page, when there is one it has not had.
 *
 * @param aPage The menu page as pictures.
 */
void VRDashboard::UploadFrame(VRPage& aPage) noexcept
{
    uint32_t contentPercent = 0;
    if (!aPage.CopyOverPanelIfNewer(m_pictureVersion, m_pixels, contentPercent))
        return;

    m_pContext->UpdateSubresource(m_pTexture, 0, nullptr, m_pixels.data(), VRPage::kWidth * 4, 0);

    // Nothing ever presents on this device, so without a flush the upload stays queued and SteamVR reads a blank texture.
    // Flush so the pixels are on the GPU before the compositor opens it.
    m_pContext->Flush();

    vr::Texture_t texture{m_pTexture, vr::TextureType_DirectX, vr::ColorSpace_Auto};
    const auto cError = m_pOverlay->SetOverlayTexture(m_handle, &texture);

    if (m_logNextUpload)
    {
        m_logNextUpload = false;
        spdlog::info("VRDashboard: page frame sent to SteamVR (result {}), {}% of the page has content", static_cast<int>(cError), contentPercent);
    }
}

/**
 * Pass the tab's events to the page and the menu: mouse, scroll, typed text, and the tab being opened or closed.
 *
 * @param aOverlay The overlay service, which owns the page's browser.
 * @param aPage The menu page as pictures.
 */
void VRDashboard::ProcessEvents(OverlayService& aOverlay, VRPage& aPage) noexcept
{
    OverlayApp* pApp = aOverlay.GetOverlayApp();

    vr::VREvent_t event{};
    while (m_pOverlay->PollNextOverlayEvent(m_handle, &event, sizeof(event)))
    {
        switch (event.eventType)
        {
        case vr::VREvent_MouseMove:
        case vr::VREvent_MouseButtonDown:
        case vr::VREvent_MouseButtonUp:
        {
            // OpenVR mouse coordinates start at the bottom left, the page's at the top left.
            m_mouseX = static_cast<uint16_t>(std::clamp(event.data.mouse.x, 0.f, static_cast<float>(VRPage::kWidth - 1)));
            m_mouseY = static_cast<uint16_t>(std::clamp(VRPage::kHeight - event.data.mouse.y, 0.f, static_cast<float>(VRPage::kHeight - 1)));

            if (event.eventType == vr::VREvent_MouseMove)
            {
                PageInput::MouseMove(pApp, m_mouseX, m_mouseY);
                break;
            }
            const auto cButton = event.data.mouse.button == vr::VRMouseButton_Right ? PageInput::Button::kRight : PageInput::Button::kLeft;
            if (event.eventType == vr::VREvent_MouseButtonUp)
                PageInput::MouseUp(pApp, m_mouseX, m_mouseY, cButton);
            else
                PageInput::MouseDown(pApp, m_mouseX, m_mouseY, cButton);
            break;
        }
        // OpenVR scrolls in notches, the page in wheel units.
        case vr::VREvent_ScrollSmooth: PageInput::MouseWheel(pApp, m_mouseX, m_mouseY, static_cast<int16_t>(event.data.scroll.ydelta * PageInput::kWheelNotch)); break;
        case vr::VREvent_KeyboardCharInput:
            PageInput::Text(pApp, event.data.keyboard.cNewInput, strnlen(event.data.keyboard.cNewInput, sizeof(event.data.keyboard.cNewInput)));
            break;
        case vr::VREvent_OverlayShown:
            spdlog::info("VRDashboard: tab opened (in game: {})", aOverlay.GetInGame());
            m_visible = true;
            m_logNextUpload = true;
            aPage.SetShownBy(VRPage::kDashboard, true);
            World::Get().ctx().at<MenuService>().Open(MenuSurface::kDashboard);
            break;
        case vr::VREvent_OverlayHidden:
            m_visible = false;
            aPage.SetShownBy(VRPage::kDashboard, false);
            World::Get().ctx().at<MenuService>().CloseOn(MenuSurface::kDashboard);
            break;
        default: break;
        }
    }
}

/**
 * Show SteamVR's keyboard when a text field on the page asks for one while the tab is open, and hide it when asked.
 *
 * @param aPage The menu page, which holds the field's request.
 */
void VRDashboard::ForwardKeyboardRequest(VRPage& aPage) noexcept
{
    // SteamVR's keyboard belongs to the tab; a request while the tab is closed is dropped, as the in-game panel uses the page's own keyboard.
    const VRPage::KeyboardRequest cRequest = aPage.TakeKeyboardRequest();
    if (cRequest == VRPage::KeyboardRequest::kShow && m_visible)
        m_pOverlay->ShowKeyboardForOverlay(m_handle, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeSingleLine, vr::KeyboardFlag_Minimal, "Skyrim Together", 256, "", 0);
    else if (cRequest == VRPage::KeyboardRequest::kHide)
        m_pOverlay->HideKeyboard();
}

#endif
