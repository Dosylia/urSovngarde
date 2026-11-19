#pragma once

#include <cstdint>
#include <string>

//! A line of text floating in front of the headset (an OpenVR overlay locked to the head), over a soft dark band; and a meter above it.
//! It fades in, holds, and fades out; a new one replaces the one showing. SteamVR draws it above the game, black screen included.
namespace VRBanner
{
//! @param acText UTF-8.
//! @param aRgb The text's colour, 0xRRGGBB.
//! @param aTextHeight How tall the letters are, in 1024ths of the overlay's width.
//! @param aHoldSeconds How long it stays fully shown, between the fades.
void Show(const std::string& acText, uint32_t aRgb, int aTextHeight, float aHoldSeconds) noexcept;

//! The death card, which other plugins can show too: the title in large letters between thin rules under an emblem, and a smaller line below it.
//! @param acSubtitle UTF-8; empty for none.
void ShowDeath(const std::string& acTitle, const std::string& acSubtitle, uint32_t aRgb, float aHoldSeconds) noexcept;

//! Fades out the one showing, if any.
void Hide() noexcept;

//! A bar near the top of the view, in an overlay of its own, until HideMeter: banners and cards show beside it.
//! A call while it shows changes it in place, without a fade; one not shown again for 10 s fades out.
//! @param acLabel UTF-8; empty for none.
//! @param aFill From 0 to 1, how much of the bar the attackers' colour fills from the left; the defenders' colour fills the rest.
void ShowMeter(const std::string& acLabel, float aFill, uint32_t aAttackRgb, uint32_t aDefendRgb) noexcept;

//! Fades the meter out.
void HideMeter() noexcept;

//! The fades; call once a frame.
void Update() noexcept;
} // namespace VRBanner
