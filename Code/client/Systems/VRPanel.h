#pragma once

#ifdef SKYRIMVR

#include <Systems/VREyeDraw.h>
#include <Games/Skyrim/VRControllers.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace TiltedPhoques
{
struct OverlayApp;
}
struct OverlayService;
struct VRPage;

/**
 * The menu page on a panel inside the game, drawn into the eye images (VREyeDraw), so it shows on any VR runtime.
 * It shows while the menu is open on the panel surface (MenuService), and tells MenuService whether it can be shown at all.
 * It opens in front of the headset, a little below eye level, and stays there in the game's world while the head moves.
 * Either controller points at it like a laser: a beam and a dot show where, the trigger clicks there, and the stick scrolls.
 * A ring shows each hand, as the panel is drawn over the game's own hands.
 * Both thumbstick clicks held for a second open it, and close it; other mods of the big lists leave that chord free.
 */
struct VRPanel final
{
    VRPanel() noexcept = default;

    /**
     * Stop drawing the panel, so nothing is left in the eye images.
     */
    ~VRPanel();

    TP_NOCOPYMOVE(VRPanel);

    /**
     * Keep the panel going: open or close it on both stick clicks, show it while the menu is on it, keep it in front of the player, follow the laser, and send it new pictures of the page.
     * The game's update thread, every frame, before the page's own update.
     *
     * @param aOverlay The overlay service, which owns the page's browser.
     * @param aPage The menu page as pictures.
     */
    void Update(OverlayService& aOverlay, VRPage& aPage) noexcept;

private:
    /**
     * Open the menu on the panel, or close it, once both stick clicks have been held for a second.
     * One hold does it once: both thumbs must come up before the next hold counts.
     */
    void ToggleOnStickChord() noexcept;

    /**
     * Start or stop showing the panel, as the menu opens on it or leaves it.
     *
     * @param aShown True when the menu has just opened on the panel, false when it has just left it.
     * @param aOverlay The overlay service, which owns the page's browser.
     * @param aPage The menu page as pictures.
     */
    void SetShown(bool aShown, OverlayService& aOverlay, VRPage& aPage) noexcept;

    /**
     * Place the panel in front of the player when it opens, and again when the player ends up far from it, as after a door or a fall.
     *
     * @param acHead The headset's space this frame.
     */
    void KeepInFront(const VRControllers::HeadSpace& acHead) noexcept;

    /**
     * Follow the pointing hand's laser: move the page's mouse to where it lands, draw the beam, the dot and the hand rings, click with the trigger and scroll with the stick.
     *
     * @param aOverlay The overlay service, which owns the page's browser.
     * @param acHead The headset's space this frame, for the beam's length in metres.
     */
    void FollowPointer(OverlayService& aOverlay, const VRControllers::HeadSpace& acHead) noexcept;

    /**
     * Check if a hand's trigger has just been pulled: held now, but not last frame.
     *
     * @param aHand Which hand.
     * @param aTriggerHeld True while that hand's trigger is held now.
     * @return True on the frame the pull starts.
     */
    bool IsFreshPull(VRControllers::Hand aHand, bool aTriggerHeld) const noexcept;

    /**
     * Keep each hand's trigger as read now, so the next frame can tell a fresh pull from a held trigger.
     *
     * @param acButtons Each hand's buttons now, left then right.
     */
    void RememberTriggers(const std::array<VRControllers::Buttons, 2>& acButtons) noexcept;

    /**
     * Make the other hand the pointing one on a fresh pull of its trigger, unless a click is held.
     * That pull only switches: a click takes a second one, so the switch never presses what the new laser happens to cross.
     *
     * @param acButtons Each hand's buttons this frame, left then right.
     */
    void TakeOverWithOtherTrigger(const std::array<VRControllers::Buttons, 2>& acButtons) noexcept;

    /**
     * Click the page where the dot is on a fresh pull of the pointing hand's trigger, and let go on its release, wherever the laser went meanwhile.
     *
     * @param apApp The page's browser; nothing happens without one.
     * @param aTriggerHeld True while the pointing hand's trigger is held.
     */
    void ClickWithTrigger(TiltedPhoques::OverlayApp* apApp, bool aTriggerHeld) noexcept;

    /**
     * Scroll what is under the dot with the pointing hand's stick, faster the further it is pushed.
     *
     * @param apApp The page's browser; nothing happens without one.
     * @param aStickY The stick's up and down position, -1 to 1, up positive.
     */
    void ScrollWithStick(TiltedPhoques::OverlayApp* apApp, float aStickY) noexcept;

    /**
     * Stop pointing at the panel: let go of a held click and stop drawing the beam, the dot and the rings.
     *
     * @param aOverlay The overlay service, which owns the page's browser.
     */
    void StopPointing(OverlayService& aOverlay) noexcept;

    std::vector<uint8_t> m_pixels;
    uint64_t m_pictureVersion{0};
    bool m_shown{false};
    // Where the panel is in the game's world while it is shown.
    std::optional<VREyeDraw::Quad> m_placement;

    // The hand whose laser works the panel; a fresh pull of the other hand's trigger takes it over.
    VRControllers::Hand m_pointingHand{VRControllers::Hand::kRight};
    // Each hand's trigger as last frame read it, left then right: tells a fresh pull from a held trigger.
    bool m_triggerLastFrame[2]{};
    // Whether the laser lands on the panel, and the page pixel it last landed on: clicks and scrolls go there.
    bool m_aimsAtPanel{false};
    uint16_t m_dotX{0};
    uint16_t m_dotY{0};
    // Whether the page holds a click from the trigger, let go when the trigger is.
    bool m_clickHeld{false};
    // When the stick may scroll the next notch.
    std::chrono::steady_clock::time_point m_nextScroll{};
    // Since when both stick clicks are held, and whether this hold has already opened or closed the panel.
    std::chrono::steady_clock::time_point m_chordSince{};
    bool m_chordDone{false};
};

#endif
