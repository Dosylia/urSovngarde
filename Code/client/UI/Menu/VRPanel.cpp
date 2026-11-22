#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Menu/VRPanel.h>
#include <UI/Menu/VRPage.h>
#include <UI/Menu/VREyeDraw.h>
#include <UI/Menu/PageInput.h>
#include <UI/Menu/PanelGeometry.h>

#include <Services/MenuService.h>
#include <Services/OverlayService.h>

#include <Games/Skyrim/VR/VRControllers.h>

#include <World.h>
#include <Utils.h>

namespace
{
// In metres, from the headset as the panel opens: wide enough to read the page at its zoom, ahead, a little below the eyes.
constexpr float kPanelWidth = 1.4f;
constexpr PanelGeometry::Placement kPlacement{kPanelWidth, kPanelWidth * VRPage::kHeight / VRPage::kWidth, 1.25f, 0.15f};
// A headset this far from the panel's centre, after a door or a fall, brings it back in front.
constexpr float kBringBackBeyond = 3.f;
// How far the beam reaches when it misses the panel.
constexpr float kBeamReach = 2.f;
// The stick scrolls past this much, a wheel notch's worth at most this often.
constexpr float kStickDeadZone = 0.3f;
constexpr auto kScrollEvery = std::chrono::milliseconds(60);
// Held this long, both stick clicks open or close the panel: a passing press while moving or turning does not.
constexpr auto kChordHold = std::chrono::seconds(1);

/**
 * Read both hands' buttons and sticks.
 *
 * @return Each hand's buttons, left then right, as the game last read them.
 */
std::array<VRControllers::Buttons, 2> ButtonsOfBothHands() noexcept
{
    return {VRControllers::ButtonsOf(VRControllers::Hand::kLeft), VRControllers::ButtonsOf(VRControllers::Hand::kRight)};
}

/**
 * Find a hand's place in arrays kept left then right.
 *
 * @param aHand Which hand.
 * @return 0 for the left hand, 1 for the right.
 */
constexpr size_t IndexOf(const VRControllers::Hand aHand) noexcept
{
    return static_cast<size_t>(aHand);
}

/**
 * Find the other hand.
 *
 * @param aHand One hand.
 * @return The right hand for the left, the left for the right.
 */
constexpr VRControllers::Hand OtherHand(const VRControllers::Hand aHand) noexcept
{
    return aHand == VRControllers::Hand::kLeft ? VRControllers::Hand::kRight : VRControllers::Hand::kLeft;
}

/**
 * Place the panel in front of the headset as it is now, upright and level, in the game's world: it then stays there while the head moves.
 *
 * @param acHead The headset's space.
 * @return The panel's corners in the game's world, or nothing when the headset's numbers give no direction.
 */
std::optional<VREyeDraw::Quad> PlaceInFront(const VRControllers::HeadSpace& acHead) noexcept
{
    const glm::vec3 cHeadTop = acHead.WorldDirection({0.f, 1.f, 0.f});
    const auto cAhead = PanelGeometry::LevelAhead(acHead.Ahead(), cHeadTop);
    if (!cAhead)
        return std::nullopt;
    VREyeDraw::Quad quad;
    quad.Corners = PanelGeometry::CornersInFront(acHead.Position, *cAhead, acHead.UnitsPerMetre, kPlacement);
    return quad;
}

/**
 * Check if the player has ended up far from the panel, as after a door or a fall.
 *
 * @param acHead The headset's space now.
 * @param acPanel The panel's corners.
 * @return True when the headset is more than kBringBackBeyond metres from the panel's centre; false for broken numbers.
 */
bool IsFarFrom(const VRControllers::HeadSpace& acHead, const VREyeDraw::Quad& acPanel) noexcept
{
    return glm::length(acHead.Point(PanelGeometry::CentreOf(acPanel.Corners))) > kBringBackBeyond;
}

/**
 * Work out what to draw for the controllers: the pointing hand's beam and its dot on the panel, and a ring at each hand.
 * The beam shows where the hand points before it reaches the panel; the rings show both hands where the panel hides them.
 *
 * @param acPointers Each hand's controller, left then right; nothing for a hand the game has none for.
 * @param aPointingHand The hand whose laser works the panel.
 * @param acAim Where that laser lands on the panel, or nothing when it misses.
 * @param aUnitsPerMetre Game units in a metre, for the beam's length when it misses.
 * @return What to draw over the panel.
 */
VREyeDraw::Pointing PointingFor(const std::array<std::optional<VRControllers::Pointer>, 2>& acPointers, const VRControllers::Hand aPointingHand, const std::optional<PanelGeometry::Hit>& acAim,
                                const float aUnitsPerMetre) noexcept
{
    VREyeDraw::Pointing pointing;
    if (acAim)
        pointing.Dot = acAim->Point;
    if (const auto& cPointing = acPointers[IndexOf(aPointingHand)])
    {
        const glm::vec3 cEnd = acAim ? acAim->Point : cPointing->From + cPointing->Along * (kBeamReach * aUnitsPerMetre);
        pointing.Beam = std::array<glm::vec3, 2>{cPointing->From, cEnd};
    }
    for (size_t i = 0; i < acPointers.size(); ++i)
    {
        if (acPointers[i])
            pointing.Hands[i] = acPointers[i]->From;
    }
    return pointing;
}
} // namespace

/**
 * Stop drawing the panel, so nothing is left in the eye images.
 */
VRPanel::~VRPanel()
{
    VREyeDraw::Hide();
}

/**
 * Keep the panel going: open or close it on both stick clicks, show it while the menu is on it, keep it in front of the player, follow the laser, and send it new pictures of the page.
 * The game's update thread, every frame, before the page's own update.
 *
 * @param aOverlay The overlay service, which owns the page's browser.
 * @param aPage The menu page as pictures.
 */
void VRPanel::Update(OverlayService& aOverlay, VRPage& aPage) noexcept
{
    auto& menu = World::Get().ctx().at<MenuService>();
    // Placed and drawn from the headset's node: without it the menu would hold the game's input with nothing shown.
    const auto cHead = VRControllers::HeadSpaceNow();
    menu.SetShowable(MenuSurface::kPanel, VREyeDraw::IsReady() && cHead.has_value());
    ToggleOnStickChord();

    const bool cShown = menu.GetSurface() == MenuSurface::kPanel;
    if (cShown != m_shown)
        SetShown(cShown, aOverlay, aPage);
    // Without the headset, SetShowable above has just closed the panel.
    if (!cShown || !cHead)
        return;

    KeepInFront(*cHead);
    FollowPointer(aOverlay, *cHead);

    // As CEF painted it: the drawer puts the panel behind it on the GPU.
    if (aPage.CopyPaintedIfNewer(m_pictureVersion, m_pixels))
        VREyeDraw::SetPicture(m_pixels, VRPage::kWidth, VRPage::kHeight);
}

/**
 * Open the menu on the panel, or close it, once both stick clicks have been held for a second.
 * One hold does it once: both thumbs must come up before the next hold counts.
 */
void VRPanel::ToggleOnStickChord() noexcept
{
    const auto cButtons = ButtonsOfBothHands();
    const bool cLeft = cButtons[IndexOf(VRControllers::Hand::kLeft)].StickClick;
    const bool cRight = cButtons[IndexOf(VRControllers::Hand::kRight)].StickClick;
    // A thumb lifted for a moment starts the second over; the hold counts as done until both are up.
    if (!cLeft || !cRight)
    {
        m_chordSince = {};
        if (!cLeft && !cRight)
            m_chordDone = false;
        return;
    }
    const auto cNow = std::chrono::steady_clock::now();
    if (m_chordSince == std::chrono::steady_clock::time_point{})
        m_chordSince = cNow;
    if (m_chordDone || cNow - m_chordSince < kChordHold)
        return;

    // Once per hold; the SteamVR dashboard, when it is up, keeps the menu.
    m_chordDone = true;
    auto& menu = World::Get().ctx().at<MenuService>();
    if (menu.GetSurface() == MenuSurface::kPanel)
    {
        menu.Close();
        spdlog::info("VRPanel: closed by both stick clicks");
    }
    else if (menu.GetSurface() == MenuSurface::kNone)
    {
        if (menu.Open(MenuSurface::kPanel))
            spdlog::info("VRPanel: opened by both stick clicks");
        else
            Utils::ShowModHudMessage("the menu cannot open here");
    }
}

/**
 * Start or stop showing the panel, as the menu opens on it or leaves it.
 *
 * @param aShown True when the menu has just opened on the panel, false when it has just left it.
 * @param aOverlay The overlay service, which owns the page's browser.
 * @param aPage The menu page as pictures.
 */
void VRPanel::SetShown(const bool aShown, OverlayService& aOverlay, VRPage& aPage) noexcept
{
    m_shown = aShown;
    aPage.SetShownBy(VRPage::kPanel, aShown);
    // Placed again in front of the player each time it opens.
    m_placement.reset();
    if (aShown)
    {
        // A trigger already held as the panel opens is not a click.
        RememberTriggers(ButtonsOfBothHands());
    }
    else
    {
        StopPointing(aOverlay);
        VREyeDraw::Hide();
    }
    spdlog::info("VRPanel: {}", aShown ? "shown" : "hidden");
}

/**
 * Place the panel in front of the player when it opens, and again when the player ends up far from it, as after a door or a fall.
 *
 * @param acHead The headset's space this frame.
 */
void VRPanel::KeepInFront(const VRControllers::HeadSpace& acHead) noexcept
{
    const bool cWasPlaced = m_placement.has_value();
    if (cWasPlaced && !IsFarFrom(acHead, *m_placement))
        return;

    m_placement = PlaceInFront(acHead);
    if (!m_placement)
        return;
    VREyeDraw::Show(*m_placement);
    if (cWasPlaced)
        spdlog::info("VRPanel: brought back in front of the player");
}

/**
 * Follow the pointing hand's laser: move the page's mouse to where it lands, draw the beam, the dot and the hand rings, click with the trigger and scroll with the stick.
 *
 * @param aOverlay The overlay service, which owns the page's browser.
 * @param acHead The headset's space this frame, for the beam's length in metres.
 */
void VRPanel::FollowPointer(OverlayService& aOverlay, const VRControllers::HeadSpace& acHead) noexcept
{
    OverlayApp* pApp = aOverlay.GetOverlayApp();

    const auto cButtons = ButtonsOfBothHands();
    TakeOverWithOtherTrigger(cButtons);

    const std::array<std::optional<VRControllers::Pointer>, 2> cPointers{VRControllers::PointerOf(VRControllers::Hand::kLeft), VRControllers::PointerOf(VRControllers::Hand::kRight)};
    const auto& cPointing = cPointers[IndexOf(m_pointingHand)];
    const auto cAim = m_placement && cPointing ? PanelGeometry::AimAt(m_placement->Corners, cPointing->From, cPointing->Along, VRPage::kWidth, VRPage::kHeight) : std::nullopt;

    const bool cWasOnPanel = m_aimsAtPanel;
    m_aimsAtPanel = cAim.has_value();
    if (cAim)
    {
        // Back on the panel, the page is told even for the pixel it last had: it lost track of the pointer meanwhile.
        if (!cWasOnPanel || cAim->X != m_dotX || cAim->Y != m_dotY)
            PageInput::MouseMove(pApp, cAim->X, cAim->Y);
        m_dotX = cAim->X;
        m_dotY = cAim->Y;
    }

    VREyeDraw::SetPointing(PointingFor(cPointers, m_pointingHand, cAim, acHead.UnitsPerMetre));

    const VRControllers::Buttons& cPointingButtons = cButtons[IndexOf(m_pointingHand)];
    ClickWithTrigger(pApp, cPointingButtons.Trigger);
    ScrollWithStick(pApp, cPointingButtons.StickY);

    RememberTriggers(cButtons);
}

/**
 * Check if a hand's trigger has just been pulled: held now, but not last frame.
 *
 * @param aHand Which hand.
 * @param aTriggerHeld True while that hand's trigger is held now.
 * @return True on the frame the pull starts.
 */
bool VRPanel::IsFreshPull(const VRControllers::Hand aHand, const bool aTriggerHeld) const noexcept
{
    return aTriggerHeld && !m_triggerLastFrame[IndexOf(aHand)];
}

/**
 * Keep each hand's trigger as read now, so the next frame can tell a fresh pull from a held trigger.
 *
 * @param acButtons Each hand's buttons now, left then right.
 */
void VRPanel::RememberTriggers(const std::array<VRControllers::Buttons, 2>& acButtons) noexcept
{
    for (size_t i = 0; i < acButtons.size(); ++i)
        m_triggerLastFrame[i] = acButtons[i].Trigger;
}

/**
 * Make the other hand the pointing one on a fresh pull of its trigger, unless a click is held.
 * That pull only switches: a click takes a second one, so the switch never presses what the new laser happens to cross.
 *
 * @param acButtons Each hand's buttons this frame, left then right.
 */
void VRPanel::TakeOverWithOtherTrigger(const std::array<VRControllers::Buttons, 2>& acButtons) noexcept
{
    const VRControllers::Hand cOther = OtherHand(m_pointingHand);
    if (m_clickHeld || !IsFreshPull(cOther, acButtons[IndexOf(cOther)].Trigger))
        return;
    m_pointingHand = cOther;
    // The pull counts as already seen, so it does not click as well.
    m_triggerLastFrame[IndexOf(cOther)] = true;
}

/**
 * Click the page where the dot is on a fresh pull of the pointing hand's trigger, and let go on its release, wherever the laser went meanwhile.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aTriggerHeld True while the pointing hand's trigger is held.
 */
void VRPanel::ClickWithTrigger(OverlayApp* apApp, const bool aTriggerHeld) noexcept
{
    if (!m_clickHeld && m_aimsAtPanel && IsFreshPull(m_pointingHand, aTriggerHeld))
    {
        PageInput::MouseDown(apApp, m_dotX, m_dotY, PageInput::Button::kLeft);
        m_clickHeld = true;
    }
    else if (m_clickHeld && !aTriggerHeld)
    {
        PageInput::MouseUp(apApp, m_dotX, m_dotY, PageInput::Button::kLeft);
        m_clickHeld = false;
    }
}

/**
 * Scroll what is under the dot with the pointing hand's stick, faster the further it is pushed.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aStickY The stick's up and down position, -1 to 1, up positive.
 */
void VRPanel::ScrollWithStick(OverlayApp* apApp, const float aStickY) noexcept
{
    const auto cNow = std::chrono::steady_clock::now();
    if (m_aimsAtPanel && std::abs(aStickY) > kStickDeadZone && cNow >= m_nextScroll)
    {
        // Pushed all the way, one notch each time.
        PageInput::MouseWheel(apApp, m_dotX, m_dotY, static_cast<int16_t>(aStickY * PageInput::kWheelNotch));
        m_nextScroll = cNow + kScrollEvery;
    }
}

/**
 * Stop pointing at the panel: let go of a held click and stop drawing the beam, the dot and the rings.
 *
 * @param aOverlay The overlay service, which owns the page's browser.
 */
void VRPanel::StopPointing(OverlayService& aOverlay) noexcept
{
    if (m_clickHeld)
        PageInput::MouseUp(aOverlay.GetOverlayApp(), m_dotX, m_dotY, PageInput::Button::kLeft);
    m_clickHeld = false;
    m_aimsAtPanel = false;
    VREyeDraw::SetPointing({});
}

#endif
