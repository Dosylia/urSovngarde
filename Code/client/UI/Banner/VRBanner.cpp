#include <TiltedOnlinePCH.h>

#include <UI/Banner/VRBanner.h>

#ifdef SKYRIMVR

#include <UI/Banner/BannerEyeImages.h>
#include <UI/Banner/BannerOverlays.h>
#include <UI/Banner/BannerPicture.h>
#include <UI/Banner/BannerPresenter.h>
#include <Games/Skyrim/VR/VRRuntime.h>

#include <immersive_launcher/launcher.h>

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
using BannerPlacement::Layer;

constexpr auto kFadeIn = 800ms;
constexpr auto kFadeOut = 1200ms;
// A meter not shown again for this long fades out, so one its plugin forgot does not stay for the session.
constexpr auto kMeterStale = 10s;

/**
 * One layer's showing and its fades.
 */
struct LayerFade
{
    Layer Which;
    bool Showing = false;
    Clock::time_point ShownAt{};
    // The latest time point, for one that stays until hidden.
    Clock::time_point FadeOutAt{};
    // The alpha last given to the presenter, so a layer that holds still costs no call each frame.
    float Alpha = -1.f;
};
LayerFade s_banner{Layer::kBanner};
LayerFade s_meter{Layer::kMeter};
Clock::time_point s_meterCalledAt{};

/**
 * What the meter's picture shows, the fill in thousandths.
 */
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

/**
 * Find where this game shows the banner's pictures: SteamVR's overlays, or the game's own eye images on OpenComposite, whose overlays cannot take a picture.
 *
 * @return The presenter, or null while the VR runtime is not known yet.
 */
BannerPresenter* PresenterInUse() noexcept
{
    if (VRRuntime::IsOpenComposite())
        return &BannerEyeImages::GetPresenter();
    return BannerOverlays::GetPresenter();
}

/**
 * Read the game's fonts for the pictures from its Data folder, the first time only.
 */
void ReadGameFontsOnce() noexcept
{
    static bool s_fontsRead = false;
    if (s_fontsRead)
        return;
    s_fontsRead = true;
    if (const auto* pContext = launcher::GetLaunchContext())
        BannerPicture::SetGameData(pContext->gamePath / L"Data");
    spdlog::info("VRBanner: fonts {}", BannerPicture::FontReport());
}

/**
 * Find the presenter with a layer ready to show pictures, and the game's fonts read to draw them.
 *
 * @param aLayer Which layer.
 * @return The presenter, or null when this game cannot show that layer now.
 */
BannerPresenter* PresenterReadyFor(const Layer aLayer) noexcept
{
    BannerPresenter* pPresenter = PresenterInUse();
    if (!pPresenter || !pPresenter->Prepare(aLayer))
        return nullptr;
    ReadGameFontsOnce();
    return pPresenter;
}

/**
 * Turn UTF-8 text into the wide text the pictures are written from.
 *
 * @param acText The text, UTF-8.
 * @return The same text, wide; empty for empty or broken text.
 */
std::wstring Widen(const std::string& acText) noexcept
{
    const int cLength = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    if (cLength <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(cLength - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, wide.data(), cLength);
    return wide;
}

/**
 * Work out how much of a layer shows at a moment; a fade-out that starts during the fade-in goes down from there.
 *
 * @param acFade The layer's fades.
 * @param aNow The moment.
 * @return From 0, nothing, to 1, all of it.
 */
float AlphaAt(const LayerFade& acFade, const Clock::time_point aNow) noexcept
{
    if (aNow >= acFade.FadeOutAt)
        return std::clamp(1.f - std::chrono::duration<float>(aNow - acFade.FadeOutAt) / std::chrono::duration<float>(kFadeOut), 0.f, 1.f);
    if (aNow < acFade.ShownAt + kFadeIn)
        return std::clamp(std::chrono::duration<float>(aNow - acFade.ShownAt) / std::chrono::duration<float>(kFadeIn), 0.f, 1.f);
    return 1.f;
}

/**
 * Give a layer's alpha to the presenter, only when it changed.
 *
 * @param aPresenter Where the layer is shown.
 * @param aFade The layer's fades, which remember the alpha given.
 * @param aAlpha From 0, nothing, to 1, all of it.
 */
void SetAlpha(BannerPresenter& aPresenter, LayerFade& aFade, const float aAlpha) noexcept
{
    if (aAlpha == aFade.Alpha)
        return;
    aPresenter.SetAlpha(aFade.Which, aAlpha);
    aFade.Alpha = aAlpha;
}

/**
 * Start a layer's fade-out from what shows now.
 *
 * @param aFade The layer's fades.
 * @param aNow The moment the fade-out starts.
 */
void FadeOut(LayerFade& aFade, const Clock::time_point aNow) noexcept
{
    const float cShown = AlphaAt(aFade, aNow);
    aFade.FadeOutAt = aNow - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(kFadeOut) * (1.f - cShown));
}

/**
 * Fade a layer in again from what shows now, to stay.
 *
 * @param aFade The layer's fades.
 * @param aNow The moment the fade-in starts.
 */
void FadeBackIn(LayerFade& aFade, const Clock::time_point aNow) noexcept
{
    const float cShown = AlphaAt(aFade, aNow);
    aFade.ShownAt = aNow - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(kFadeIn) * cShown);
    aFade.FadeOutAt = Clock::time_point::max();
}

/**
 * Show a picture in the banner, in place of the one showing, fading in from nothing.
 *
 * @param aPresenter Where the banner is shown.
 * @param acPicture The picture; an empty one shows nothing.
 * @param acWhat What it shows, for the log.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void Present(BannerPresenter& aPresenter, const BannerPicture::Picture& acPicture, const std::string& acWhat, const float aHoldSeconds) noexcept
{
    if (acPicture.Rgba.empty())
        return;
    if (!aPresenter.SetPicture(Layer::kBanner, acPicture, acWhat, PictureChange::kForShowing))
        return;
    const auto cNow = Clock::now();
    s_banner.ShownAt = cNow;
    s_banner.FadeOutAt = cNow + kFadeIn + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(aHoldSeconds));
    s_banner.Showing = true;
    SetAlpha(aPresenter, s_banner, 0.f);
    aPresenter.Show(Layer::kBanner);
    spdlog::info("VRBanner: '{}' shown for {:.1f} s", acWhat, aHoldSeconds);
}

/**
 * Move a layer's fade on to a moment, and hide it once it has faded out.
 *
 * @param aPresenter Where the layer is shown.
 * @param aFade The layer's fades.
 * @param aNow The moment.
 */
void Fade(BannerPresenter& aPresenter, LayerFade& aFade, const Clock::time_point aNow) noexcept
{
    if (!aFade.Showing)
        return;
    if (aNow >= aFade.FadeOutAt && aNow - aFade.FadeOutAt >= kFadeOut)
    {
        aPresenter.Hide(aFade.Which);
        aFade.Showing = false;
        return;
    }
    SetAlpha(aPresenter, aFade, AlphaAt(aFade, aNow));
}
} // namespace

/**
 * Show a line of text in the banner, in place of the one showing.
 *
 * @param acText The text, UTF-8.
 * @param aRgb The text's colour, 0xRRGGBB.
 * @param aTextHeight How tall the letters are, in 1024ths of the banner's width.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void Show(const std::string& acText, const uint32_t aRgb, const int aTextHeight, const float aHoldSeconds) noexcept
{
    BannerPresenter* pPresenter = PresenterReadyFor(Layer::kBanner);
    if (!pPresenter)
        return;
    Present(*pPresenter, BannerPicture::Line(Widen(acText), aRgb, aTextHeight), acText, aHoldSeconds);
}

/**
 * Show the death card in the banner, in place of the one showing; other plugins can show it too.
 * The title in large letters between thin rules under an emblem, and a smaller line below it.
 *
 * @param acTitle The title, UTF-8.
 * @param acSubtitle The smaller line, UTF-8; empty for none.
 * @param aRgb The card's colour, 0xRRGGBB.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void ShowDeath(const std::string& acTitle, const std::string& acSubtitle, const uint32_t aRgb, const float aHoldSeconds) noexcept
{
    BannerPresenter* pPresenter = PresenterReadyFor(Layer::kBanner);
    if (!pPresenter)
        return;
    // The card is not written to a file here: encoding it took longer than drawing it, on the main thread, at every death.
    // To see one, draw its text (the "shown" line logs it) with TPTests' [.banner-preview] case.
    Present(*pPresenter, BannerPicture::DeathCard(Widen(acTitle), Widen(acSubtitle), aRgb), acSubtitle.empty() ? acTitle : acTitle + " / " + acSubtitle, aHoldSeconds);
}

/**
 * Fade out the banner showing, if any.
 */
void Hide() noexcept
{
    const auto cNow = Clock::now();
    if (s_banner.Showing && cNow < s_banner.FadeOutAt)
        FadeOut(s_banner, cNow);
}

/**
 * Show a bar near the top of the view, a layer of its own, until HideMeter: banners and cards show beside it.
 * A call while it shows changes it in place, without a fade; one not shown again for 10 s fades out.
 *
 * @param acLabel The label above the bar, UTF-8; empty for none.
 * @param aFill From 0 to 1, how much of the bar the attackers' colour fills from the left; the defenders' colour fills the rest.
 * @param aAttackRgb The attackers' colour, 0xRRGGBB.
 * @param aDefendRgb The defenders' colour, 0xRRGGBB.
 */
void ShowMeter(const std::string& acLabel, const float aFill, const uint32_t aAttackRgb, const uint32_t aDefendRgb) noexcept
{
    BannerPresenter* pPresenter = PresenterReadyFor(Layer::kMeter);
    if (!pPresenter)
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
        // A meter already showing changes in place; one that is not starts a showing below.
        const PictureChange cChange = s_meter.Showing ? PictureChange::kInPlace : PictureChange::kForShowing;
        if (!pPresenter->SetPicture(Layer::kMeter, s_meterPicture, acLabel, cChange))
            return;
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
        SetAlpha(*pPresenter, s_meter, 0.f);
        pPresenter->Show(Layer::kMeter);
    }
    else if (cNow >= s_meter.FadeOutAt)
        FadeBackIn(s_meter, cNow);
}

/**
 * Fade the meter out.
 */
void HideMeter() noexcept
{
    const auto cNow = Clock::now();
    if (!s_meter.Showing || cNow >= s_meter.FadeOutAt)
        return;
    FadeOut(s_meter, cNow);
    spdlog::info("VRBanner: meter hidden");
}

/**
 * Run the fades, and fade out a meter not shown again for a while; call once a frame.
 */
void Update() noexcept
{
    if (!s_banner.Showing && !s_meter.Showing)
        return;
    BannerPresenter* pPresenter = PresenterInUse();
    if (!pPresenter)
        return;
    const auto cNow = Clock::now();
    if (s_meter.Showing && cNow < s_meter.FadeOutAt && cNow - s_meterCalledAt >= kMeterStale)
    {
        FadeOut(s_meter, cNow);
        spdlog::info("VRBanner: meter not shown again for {} s, so hidden", std::chrono::duration_cast<std::chrono::seconds>(kMeterStale).count());
    }
    Fade(*pPresenter, s_banner, cNow);
    Fade(*pPresenter, s_meter, cNow);
}
} // namespace VRBanner

#else

namespace VRBanner
{
/**
 * Show a line of text in the banner: nothing in the flat game, which shows no banner.
 *
 * @param acText The text, UTF-8.
 * @param aRgb The text's colour, 0xRRGGBB.
 * @param aTextHeight How tall the letters are, in 1024ths of the banner's width.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void Show(const std::string& /*acText*/, uint32_t /*aRgb*/, int /*aTextHeight*/, float /*aHoldSeconds*/) noexcept
{
}

/**
 * Show the death card in the banner: nothing in the flat game, which shows no banner.
 *
 * @param acTitle The title, UTF-8.
 * @param acSubtitle The smaller line, UTF-8; empty for none.
 * @param aRgb The card's colour, 0xRRGGBB.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void ShowDeath(const std::string& /*acTitle*/, const std::string& /*acSubtitle*/, uint32_t /*aRgb*/, float /*aHoldSeconds*/) noexcept
{
}

/**
 * Fade out the banner showing: nothing in the flat game, which shows no banner.
 */
void Hide() noexcept
{
}

/**
 * Show a bar near the top of the view: nothing in the flat game, which shows no meter.
 *
 * @param acLabel The label above the bar, UTF-8; empty for none.
 * @param aFill From 0 to 1, how much of the bar the attackers' colour fills from the left; the defenders' colour fills the rest.
 * @param aAttackRgb The attackers' colour, 0xRRGGBB.
 * @param aDefendRgb The defenders' colour, 0xRRGGBB.
 */
void ShowMeter(const std::string& /*acLabel*/, float /*aFill*/, uint32_t /*aAttackRgb*/, uint32_t /*aDefendRgb*/) noexcept
{
}

/**
 * Fade the meter out: nothing in the flat game, which shows no meter.
 */
void HideMeter() noexcept
{
}

/**
 * Run the fades: nothing in the flat game, which shows no banner.
 */
void Update() noexcept
{
}
} // namespace VRBanner

#endif
