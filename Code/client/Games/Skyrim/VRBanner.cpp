#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRBanner.h>

#ifdef SKYRIMVR

#include <Games/Skyrim/BannerPicture.h>

#include <immersive_launcher/launcher.h>
#include <openvr/openvr.h>

#include <algorithm>
#include <chrono>

namespace VRBanner
{
namespace
{
using namespace std::chrono_literals;

// 1.6 m wide, 1.5 m in front of the eyes and a little below them: the middle of the view, readable without turning.
constexpr float kWidthMeters = 1.6f;
constexpr float kDistanceMeters = 1.5f;
constexpr float kBelowEyesMeters = 0.05f;
constexpr auto kFadeIn = 800ms;
constexpr auto kFadeOut = 1200ms;

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

    // The pictures are written in the game's own fonts, read from its Data folder.
    if (const auto* pContext = launcher::GetLaunchContext())
        BannerPicture::SetGameData(pContext->gamePath / L"Data");
    spdlog::info("VRBanner: fonts {}", BannerPicture::FontReport());
    return true;
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

void Present(vr::IVROverlay* apOverlay, const BannerPicture::Picture& acPicture, const std::string& acWhat, const float aHoldSeconds) noexcept
{
    if (acPicture.Rgba.empty())
        return;
    const auto cError = apOverlay->SetOverlayRaw(s_handle, const_cast<uint8_t*>(acPicture.Rgba.data()), acPicture.Width, acPicture.Height, 4);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: '{}' could not be drawn ({})", acWhat, static_cast<int>(cError));
        return;
    }
    const auto cNow = std::chrono::steady_clock::now();
    s_shownAt = cNow;
    s_fadeOutAt = cNow + kFadeIn + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(aHoldSeconds));
    s_showing = true;
    apOverlay->SetOverlayAlpha(s_handle, 0.f);
    apOverlay->ShowOverlay(s_handle);
    spdlog::info("VRBanner: '{}' shown for {:.1f} s", acWhat, aHoldSeconds);
}
} // namespace

void Show(const std::string& acText, const uint32_t aRgb, const int aTextHeight, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureOverlay(pOverlay))
        return;
    Present(pOverlay, BannerPicture::Line(Widen(acText), aRgb, aTextHeight), acText, aHoldSeconds);
}

void ShowDeath(const std::string& acTitle, const std::string& acSubtitle, const uint32_t aRgb, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureOverlay(pOverlay))
        return;
    const BannerPicture::Picture cCard = BannerPicture::DeathCard(Widen(acTitle), Widen(acSubtitle), aRgb);
    Present(pOverlay, cCard, acSubtitle.empty() ? acTitle : acTitle + " / " + acSubtitle, aHoldSeconds);

    // The last death card, to see what a player saw.
    BannerPicture::WritePng(TiltedPhoques::GetPath() / L"logs" / L"banner-death.png", cCard);
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

void ShowDeath(const std::string&, const std::string&, uint32_t, float) noexcept
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
