#pragma once

#include <cstdint>
#include <string>

//! A line of text floating in front of the headset (an OpenVR overlay locked to the head), over a soft dark band.
//! It fades in, holds, and fades out; a new one replaces the one showing. SteamVR draws it above the game, black screen included.
namespace VRBanner
{
//! @param acText UTF-8.
//! @param aRgb The text's colour, 0xRRGGBB.
//! @param aTextHeight How tall the letters are, in 1024ths of the overlay's width.
//! @param aHoldSeconds How long it stays fully shown, between the fades.
void Show(const std::string& acText, uint32_t aRgb, int aTextHeight, float aHoldSeconds) noexcept;

//! Fades out the one showing, if any.
void Hide() noexcept;

//! The fades; call once a frame.
void Update() noexcept;
} // namespace VRBanner
