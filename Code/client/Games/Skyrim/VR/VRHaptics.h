#pragma once

#include <cstdint>

//! Controller haptics, and the grip buttons, straight to OpenVR.
//!
//! Used by VRBodySync's weapon touch: PLANCK, HIGGS and parry mods do not buzz when a weapon rests against another weapon.
//! Only this mod knows where another player's weapon is.
namespace VRHaptics
{
//! One pulse on one controller, clamped to what OpenVR accepts in a single call (about 4 ms).
//! A longer buzz is made by asking again on the next frame.
//! @param aRightHand Which controller.
//! @param aDurationMicroSec 0 to 3999.
//! @return false if OpenVR is not up, or that controller is not tracked.
bool Pulse(bool aRightHand, uint16_t aDurationMicroSec) noexcept;

//! How many controllers hold their grip, read two ways; reading never takes the press from the game.
//! OpenVR: straight from the runtime, whatever HIGGS or the game do with the press.
//! Game: what the game's own controller devices last read, which is also where test tools put their presses.
struct Grips
{
    uint32_t OpenVR = 0;
    uint32_t Game = 0;

    bool Both() const noexcept { return OpenVR >= 2 || Game >= 2; }
};
Grips GripsHeld() noexcept;

} // namespace VRHaptics
