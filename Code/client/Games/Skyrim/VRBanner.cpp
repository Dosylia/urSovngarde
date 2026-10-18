#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRBanner.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>

#include <algorithm>
#include <chrono>
#include <vector>

namespace VRBanner
{
namespace
{
using namespace std::chrono_literals;

// The overlay's picture; OpenVR bounds what SetOverlayRaw takes, and this stays well under a megabyte.
constexpr int kWidth = 1024;
constexpr int kHeight = 192;
// 1.6 m wide, 1.5 m in front of the eyes and a little below them: the middle of the view, readable without turning.
constexpr float kWidthMeters = 1.6f;
constexpr float kDistanceMeters = 1.5f;
constexpr float kBelowEyesMeters = 0.05f;
constexpr auto kFadeIn = 800ms;
constexpr auto kFadeOut = 1200ms;
// How dark the band behind the text is at its middle.
constexpr float kBandAlpha = 0.55f;

// The game has already brought OpenVR up through its own openvr_api.dll; asking that DLL shares the game's session.
vr::IVROverlay* GetOverlay() noexcept
{
    static vr::IVROverlay* s_pOverlay = nullptr;
    if (s_pOverlay)
        return s_pOverlay;
    HMODULE hOpenVR = GetModuleHandleW(L"openvr_api.dll");
    if (!hOpenVR)
        return nullptr;
    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    auto* pGetInterface = reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface"));
    if (!pGetInterface)
        return nullptr;
    vr::EVRInitError error = vr::VRInitError_None;
    s_pOverlay = static_cast<vr::IVROverlay*>(pGetInterface(vr::IVROverlay_Version, &error));
    return s_pOverlay;
}

vr::VROverlayHandle_t s_handle = vr::k_ulOverlayHandleInvalid;
bool s_showing = false;
std::chrono::steady_clock::time_point s_shownAt{};
std::chrono::steady_clock::time_point s_fadeOutAt{};

bool EnsureOverlay(vr::IVROverlay* apOverlay) noexcept
{
    if (s_handle != vr::k_ulOverlayHandleInvalid)
        return true;
    const auto cError = apOverlay->CreateOverlay("ursovngarde.banner", "urSovngarde banner", &s_handle);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: the overlay could not be created ({})", static_cast<int>(cError));
        s_handle = vr::k_ulOverlayHandleInvalid;
        return false;
    }
    apOverlay->SetOverlayWidthInMeters(s_handle, kWidthMeters);
    vr::HmdMatrix34_t placement = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, -kBelowEyesMeters}, {0.f, 0.f, 1.f, -kDistanceMeters}}};
    apOverlay->SetOverlayTransformTrackedDeviceRelative(s_handle, vr::k_unTrackedDeviceIndex_Hmd, &placement);
    return true;
}

float Smooth(const float aValue) noexcept
{
    const float cClamped = std::clamp(aValue, 0.f, 1.f);
    return cClamped * cClamped * (3.f - 2.f * cClamped);
}

// GDI draws the text white on black; how white each pixel came out is how much of the letter covers it.
// The picture is then the letter's colour over a dark band that fades out towards its edges.
std::vector<uint8_t> Render(const std::wstring& acText, const uint32_t aRgb, const int aTextHeight) noexcept
{
    std::vector<uint8_t> rgba(static_cast<size_t>(kWidth) * kHeight * 4, 0);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kWidth;
    info.bmiHeader.biHeight = -kHeight; // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(nullptr);
    void* pBits = nullptr;
    HBITMAP hBitmap = hdc ? CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &pBits, nullptr, 0) : nullptr;
    if (!hBitmap || !pBits)
    {
        if (hdc)
            DeleteDC(hdc);
        return rgba;
    }
    HGDIOBJ oldBitmap = SelectObject(hdc, hBitmap);
    std::fill_n(static_cast<uint8_t*>(pBits), rgba.size(), uint8_t{0});

    HFONT hFont = CreateFontW(-aTextHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              VARIABLE_PITCH | FF_ROMAN, L"Georgia");
    HGDIOBJ oldFont = SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetTextCharacterExtra(hdc, aTextHeight / 8);
    RECT area{0, 0, kWidth, kHeight};
    DrawTextW(hdc, acText.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();

    const float cRed = static_cast<float>((aRgb >> 16) & 0xFF);
    const float cGreen = static_cast<float>((aRgb >> 8) & 0xFF);
    const float cBlue = static_cast<float>(aRgb & 0xFF);
    const auto* pDrawn = static_cast<const uint8_t*>(pBits);
    for (int y = 0; y < kHeight; ++y)
    {
        const float cAcross = 1.f - std::abs(static_cast<float>(y) - kHeight * 0.5f) / (kHeight * 0.5f);
        for (int x = 0; x < kWidth; ++x)
        {
            const size_t cAt = (static_cast<size_t>(y) * kWidth + x) * 4;
            // BGRA from GDI; antialiasing greys every channel alike.
            const float cCover = pDrawn[cAt + 2] / 255.f;
            const float cAlong = std::min(static_cast<float>(std::min(x, kWidth - 1 - x)) / (kWidth * 0.3f), 1.f);
            const float cBand = kBandAlpha * Smooth(cAcross * 1.6f) * Smooth(cAlong);
            const float cAlpha = cCover + cBand * (1.f - cCover);
            // The colour that, at this alpha over the game, shows the letter's colour where it covers and black elsewhere.
            const float cShare = cAlpha > 0.f ? cCover / cAlpha : 0.f;
            rgba[cAt + 0] = static_cast<uint8_t>(cRed * cShare);
            rgba[cAt + 1] = static_cast<uint8_t>(cGreen * cShare);
            rgba[cAt + 2] = static_cast<uint8_t>(cBlue * cShare);
            rgba[cAt + 3] = static_cast<uint8_t>(cAlpha * 255.f);
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    SelectObject(hdc, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdc);
    return rgba;
}

std::wstring Widen(const std::string& acText) noexcept
{
    const int cLength = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    if (cLength <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(cLength - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, wide.data(), cLength);
    return wide;
}
} // namespace

void Show(const std::string& acText, const uint32_t aRgb, const int aTextHeight, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureOverlay(pOverlay))
        return;

    std::vector<uint8_t> pixels = Render(Widen(acText), aRgb, aTextHeight);
    const auto cError = pOverlay->SetOverlayRaw(s_handle, pixels.data(), kWidth, kHeight, 4);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: '{}' could not be drawn ({})", acText, static_cast<int>(cError));
        return;
    }
    const auto cNow = std::chrono::steady_clock::now();
    s_shownAt = cNow;
    s_fadeOutAt = cNow + kFadeIn + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(aHoldSeconds));
    s_showing = true;
    pOverlay->SetOverlayAlpha(s_handle, 0.f);
    pOverlay->ShowOverlay(s_handle);
    spdlog::info("VRBanner: '{}' shown for {:.1f} s", acText, aHoldSeconds);
}

void Hide() noexcept
{
    if (s_showing)
        s_fadeOutAt = std::min(s_fadeOutAt, std::chrono::steady_clock::now());
}

void Update() noexcept
{
    if (!s_showing)
        return;
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay)
        return;

    const auto cNow = std::chrono::steady_clock::now();
    float alpha = 1.f;
    if (cNow < s_shownAt + kFadeIn)
        alpha = std::chrono::duration<float>(cNow - s_shownAt) / std::chrono::duration<float>(kFadeIn);
    else if (cNow >= s_fadeOutAt + kFadeOut)
    {
        pOverlay->HideOverlay(s_handle);
        s_showing = false;
        return;
    }
    else if (cNow >= s_fadeOutAt)
        alpha = 1.f - std::chrono::duration<float>(cNow - s_fadeOutAt) / std::chrono::duration<float>(kFadeOut);
    pOverlay->SetOverlayAlpha(s_handle, std::clamp(alpha, 0.f, 1.f));
}
} // namespace VRBanner

#else

namespace VRBanner
{
void Show(const std::string&, uint32_t, int, float) noexcept
{
}

void Hide() noexcept
{
}

void Update() noexcept
{
}
} // namespace VRBanner

#endif
