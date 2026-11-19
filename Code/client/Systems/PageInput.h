#pragma once

#include <OverlayApp.hpp>

#include <cstdint>

/**
 * Sends pointer and text input into the menu page, the same from every place it is shown (the SteamVR tab, the panel inside the game).
 * Points are the page's own pixels, from its top left corner.
 */
namespace PageInput
{
/**
 * Which mouse button the page is told about.
 */
enum class Button
{
    kLeft,
    kRight,
};

// One notch of a mouse wheel, in the wheel units the page counts in.
constexpr int16_t kWheelNotch = 120;

/**
 * Move the page's mouse pointer to a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 */
void MouseMove(TiltedPhoques::OverlayApp* apApp, uint16_t aX, uint16_t aY) noexcept;

/**
 * Press a mouse button on the page, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aButton Which button.
 */
void MouseDown(TiltedPhoques::OverlayApp* apApp, uint16_t aX, uint16_t aY, Button aButton) noexcept;

/**
 * Let go of a mouse button on the page, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aButton Which button.
 */
void MouseUp(TiltedPhoques::OverlayApp* apApp, uint16_t aX, uint16_t aY, Button aButton) noexcept;

/**
 * Scroll the page with the mouse wheel, at a point.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param aX Across the page, in its pixels.
 * @param aY Down the page, in its pixels.
 * @param aDelta How far, in wheel units: kWheelNotch is one notch, positive scrolls up.
 */
void MouseWheel(TiltedPhoques::OverlayApp* apApp, uint16_t aX, uint16_t aY, int16_t aDelta) noexcept;

/**
 * Type text into the page, where its caret is.
 * Backspace and Enter become real key presses, as the page's fields expect.
 *
 * @param apApp The page's browser; nothing happens without one.
 * @param acpUtf8 The typed characters, in UTF-8.
 * @param aLength How many bytes of them.
 */
void Text(TiltedPhoques::OverlayApp* apApp, const char* acpUtf8, size_t aLength) noexcept;
} // namespace PageInput
