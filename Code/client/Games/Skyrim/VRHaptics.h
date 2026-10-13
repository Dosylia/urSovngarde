#pragma once

#include <cstdint>

//! Controller haptics, straight to OpenVR.
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

} // namespace VRHaptics
