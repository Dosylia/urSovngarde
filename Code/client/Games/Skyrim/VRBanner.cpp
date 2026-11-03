#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRBanner.h>

#ifdef SKYRIMVR

#include <Games/Skyrim/BannerPicture.h>

#include <immersive_launcher/launcher.h>
#include <openvr/openvr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace VRBanner
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// The banner is 1.6 m wide, 1.5 m in front of the eyes and a little below them: the middle of the view, readable without turning.
constexpr float kWidthMeters = 1.6f;
constexpr float kDistanceMeters = 1.5f;
constexpr float kBelowEyesMeters = 0.05f;
// The meter is 0.9 m wide at the same distance, near the top of the view, clear of the banner and tilted to face the eyes.
constexpr float kMeterWidthMeters = 0.9f;
constexpr float kMeterAboveEyesMeters = 0.45f;
constexpr auto kFadeIn = 800ms;
constexpr auto kFadeOut = 1200ms;
// A meter not shown again for this long fades out, so one its plugin forgot does not stay for the session.
constexpr auto kMeterStale = 10s;

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

// One overlay and its fades.
struct Panel
{
    vr::VROverlayHandle_t Handle = vr::k_ulOverlayHandleInvalid;
    bool Showing = false;
    Clock::time_point ShownAt{};
    // The latest time point, for one that stays until hidden.
    Clock::time_point FadeOutAt{};
    // The alpha last given to SteamVR, so a panel that holds still costs no call each frame.
    float Alpha = -1.f;
    // A failure is logged once, not at every try.
    bool Warned = false;
};
Panel s_banner;
Panel s_meter;
Clock::time_point s_meterCalledAt{};

// What the meter's picture shows, the fill in thousandths.
struct MeterLook
{
    std::string Label;
    long Fill{};
    uint32_t AttackRgb{};
    uint32_t DefendRgb{};

    bool operator==(const MeterLook&) const = default;
};
std::optional<MeterLook> s_meterLook;
BannerPicture::Picture s_meterPicture;

bool EnsureOverlay(vr::IVROverlay* apOverlay, Panel& aPanel, const char* acpKey, const char* acpName, const float aWidthMeters, const vr::HmdMatrix34_t& acPlacement,
                   const uint32_t aSortOrder) noexcept
{
    if (aPanel.Handle != vr::k_ulOverlayHandleInvalid)
        return true;
    const auto cError = apOverlay->CreateOverlay(acpKey, acpName, &aPanel.Handle);
    if (cError != vr::VROverlayError_None)
    {
        if (!aPanel.Warned)
            spdlog::warn("VRBanner: the overlay {} could not be created ({})", acpKey, static_cast<int>(cError));
        aPanel.Warned = true;
        aPanel.Handle = vr::k_ulOverlayHandleInvalid;
        return false;
    }
    apOverlay->SetOverlayWidthInMeters(aPanel.Handle, aWidthMeters);
    apOverlay->SetOverlayTransformTrackedDeviceRelative(aPanel.Handle, vr::k_unTrackedDeviceIndex_Hmd, &acPlacement);
    apOverlay->SetOverlaySortOrder(aPanel.Handle, aSortOrder);

    // The pictures are written in the game's own fonts, read from its Data folder.
    static bool s_fontsRead = false;
    if (!s_fontsRead)
    {
        s_fontsRead = true;
        if (const auto* pContext = launcher::GetLaunchContext())
            BannerPicture::SetGameData(pContext->gamePath / L"Data");
        spdlog::info("VRBanner: fonts {}", BannerPicture::FontReport());
    }
    return true;
}

bool EnsureBanner(vr::IVROverlay* apOverlay) noexcept
{
    const vr::HmdMatrix34_t cPlacement = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, -kBelowEyesMeters}, {0.f, 0.f, 1.f, -kDistanceMeters}}};
    // Drawn over the meter, should the two meet.
    return EnsureOverlay(apOverlay, s_banner, "ursovngarde.banner", "urSovngarde banner", kWidthMeters, cPlacement, 1);
}

bool EnsureMeter(vr::IVROverlay* apOverlay) noexcept
{
    // Turned about the x axis until it faces the eyes, its top leaning towards them.
    const float cTilt = std::atan2(kMeterAboveEyesMeters, kDistanceMeters);
    const float cCos = std::cos(cTilt);
    const float cSin = std::sin(cTilt);
    const vr::HmdMatrix34_t cPlacement = {{{1.f, 0.f, 0.f, 0.f}, {0.f, cCos, -cSin, kMeterAboveEyesMeters}, {0.f, cSin, cCos, -kDistanceMeters}}};
    return EnsureOverlay(apOverlay, s_meter, "ursovngarde.meter", "urSovngarde meter", kMeterWidthMeters, cPlacement, 0);
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

// How much of a panel shows; a fade-out that starts during the fade-in goes down from there.
float AlphaAt(const Panel& acPanel, const Clock::time_point aNow) noexcept
{
    if (aNow >= acPanel.FadeOutAt)
        return std::clamp(1.f - std::chrono::duration<float>(aNow - acPanel.FadeOutAt) / std::chrono::duration<float>(kFadeOut), 0.f, 1.f);
    if (aNow < acPanel.ShownAt + kFadeIn)
        return std::clamp(std::chrono::duration<float>(aNow - acPanel.ShownAt) / std::chrono::duration<float>(kFadeIn), 0.f, 1.f);
    return 1.f;
}

void SetAlpha(vr::IVROverlay* apOverlay, Panel& aPanel, const float aAlpha) noexcept
{
    if (aAlpha == aPanel.Alpha)
        return;
    apOverlay->SetOverlayAlpha(aPanel.Handle, aAlpha);
    aPanel.Alpha = aAlpha;
}

// Starts the fade-out from what shows now.
void FadeOut(Panel& aPanel, const Clock::time_point aNow) noexcept
{
    const float cShown = AlphaAt(aPanel, aNow);
    aPanel.FadeOutAt = aNow - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(kFadeOut) * (1.f - cShown));
}

// Fades in again from what shows now, and stays.
void FadeBackIn(Panel& aPanel, const Clock::time_point aNow) noexcept
{
    const float cShown = AlphaAt(aPanel, aNow);
    aPanel.ShownAt = aNow - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(kFadeIn) * cShown);
    aPanel.FadeOutAt = Clock::time_point::max();
}

void Present(vr::IVROverlay* apOverlay, const BannerPicture::Picture& acPicture, const std::string& acWhat, const float aHoldSeconds) noexcept
{
    if (acPicture.Rgba.empty())
        return;
    const auto cError = apOverlay->SetOverlayRaw(s_banner.Handle, const_cast<uint8_t*>(acPicture.Rgba.data()), acPicture.Width, acPicture.Height, 4);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: '{}' could not be drawn ({})", acWhat, static_cast<int>(cError));
        return;
    }
    const auto cNow = Clock::now();
    s_banner.ShownAt = cNow;
    s_banner.FadeOutAt = cNow + kFadeIn + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(aHoldSeconds));
    s_banner.Showing = true;
    SetAlpha(apOverlay, s_banner, 0.f);
    apOverlay->ShowOverlay(s_banner.Handle);
    spdlog::info("VRBanner: '{}' shown for {:.1f} s", acWhat, aHoldSeconds);
}

void Fade(vr::IVROverlay* apOverlay, Panel& aPanel, const Clock::time_point aNow) noexcept
{
    if (!aPanel.Showing)
        return;
    if (aNow >= aPanel.FadeOutAt && aNow - aPanel.FadeOutAt >= kFadeOut)
    {
        apOverlay->HideOverlay(aPanel.Handle);
        aPanel.Showing = false;
        return;
    }
    SetAlpha(apOverlay, aPanel, AlphaAt(aPanel, aNow));
}
} // namespace

void Show(const std::string& acText, const uint32_t aRgb, const int aTextHeight, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureBanner(pOverlay))
        return;
    Present(pOverlay, BannerPicture::Line(Widen(acText), aRgb, aTextHeight), acText, aHoldSeconds);
}

void ShowDeath(const std::string& acTitle, const std::string& acSubtitle, const uint32_t aRgb, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureBanner(pOverlay))
        return;
    // The card is not written to a file here: encoding it took longer than drawing it, on the main thread, at every death.
    // To see one, draw its text (the "shown" line logs it) with TPTests' [.banner-preview] case.
    Present(pOverlay, BannerPicture::DeathCard(Widen(acTitle), Widen(acSubtitle), aRgb), acSubtitle.empty() ? acTitle : acTitle + " / " + acSubtitle, aHoldSeconds);
}

void Hide() noexcept
{
    const auto cNow = Clock::now();
    if (s_banner.Showing && cNow < s_banner.FadeOutAt)
        FadeOut(s_banner, cNow);
}

void ShowMeter(const std::string& acLabel, const float aFill, const uint32_t aAttackRgb, const uint32_t aDefendRgb) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureMeter(pOverlay))
        return;

    // Drawn again only when it would look different, and its label and frame only with a new label: those take most of the time.
    const MeterLook cLook{acLabel, std::lround(aFill * 1000.f), aAttackRgb, aDefendRgb};
    const bool cNewLabel = !s_meterLook || s_meterLook->Label != acLabel;
    if (cLook != s_meterLook)
    {
        if (cNewLabel || s_meterPicture.Rgba.empty())
            s_meterPicture = BannerPicture::MeterFace(Widen(acLabel));
        if (s_meterPicture.Rgba.empty())
            return;
        BannerPicture::FillMeter(s_meterPicture, aFill, aAttackRgb, aDefendRgb);
        const auto cError = pOverlay->SetOverlayRaw(s_meter.Handle, s_meterPicture.Rgba.data(), s_meterPicture.Width, s_meterPicture.Height, 4);
        if (cError != vr::VROverlayError_None)
        {
            if (!s_meter.Warned)
                spdlog::warn("VRBanner: the meter '{}' could not be drawn ({})", acLabel, static_cast<int>(cError));
            s_meter.Warned = true;
            return;
        }
        s_meterLook = cLook;
    }

    const auto cNow = Clock::now();
    s_meterCalledAt = cNow;
    if (!s_meter.Showing || cNow >= s_meter.FadeOutAt || cNewLabel)
        spdlog::info("VRBanner: meter '{}' at {:.0f}%", acLabel, aFill * 100.f);
    if (!s_meter.Showing)
    {
        s_meter.ShownAt = cNow;
        s_meter.FadeOutAt = Clock::time_point::max();
        s_meter.Showing = true;
        SetAlpha(pOverlay, s_meter, 0.f);
        pOverlay->ShowOverlay(s_meter.Handle);
    }
    else if (cNow >= s_meter.FadeOutAt)
        FadeBackIn(s_meter, cNow);
}

void HideMeter() noexcept
{
    const auto cNow = Clock::now();
    if (!s_meter.Showing || cNow >= s_meter.FadeOutAt)
        return;
    FadeOut(s_meter, cNow);
    spdlog::info("VRBanner: meter hidden");
}

void Update() noexcept
{
    if (!s_banner.Showing && !s_meter.Showing)
        return;
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay)
        return;
    const auto cNow = Clock::now();
    if (s_meter.Showing && cNow < s_meter.FadeOutAt && cNow - s_meterCalledAt >= kMeterStale)
    {
        FadeOut(s_meter, cNow);
        spdlog::info("VRBanner: meter not shown again for {} s, so hidden", std::chrono::duration_cast<std::chrono::seconds>(kMeterStale).count());
    }
    Fade(pOverlay, s_banner, cNow);
    Fade(pOverlay, s_meter, cNow);
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

void ShowMeter(const std::string&, float, uint32_t, uint32_t) noexcept
{
}

void HideMeter() noexcept
{
}

void Update() noexcept
{
}
} // namespace VRBanner

#endif
