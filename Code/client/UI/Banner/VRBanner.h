#pragma once

#include <cstdint>
#include <string>

/**
 * A line of text floating in front of the headset, locked to the head, over a soft dark band; and a meter above it.
 * It fades in, holds, and fades out; a new one replaces the one showing.
 * On SteamVR it is an overlay, drawn above the game, black screen included; on OpenComposite it is drawn into the game's own eye images.
 * The flat game shows none of them.
 */
namespace VRBanner
{
/**
 * Show a line of text in the banner, in place of the one showing.
 *
 * @param acText The text, UTF-8.
 * @param aRgb The text's colour, 0xRRGGBB.
 * @param aTextHeight How tall the letters are, in 1024ths of the banner's width.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void Show(const std::string& acText, uint32_t aRgb, int aTextHeight, float aHoldSeconds) noexcept;

/**
 * Show the death card in the banner, in place of the one showing; other plugins can show it too.
 * The title in large letters between thin rules under an emblem, and a smaller line below it.
 *
 * @param acTitle The title, UTF-8.
 * @param acSubtitle The smaller line, UTF-8; empty for none.
 * @param aRgb The card's colour, 0xRRGGBB.
 * @param aHoldSeconds How long it stays fully shown, between the fades.
 */
void ShowDeath(const std::string& acTitle, const std::string& acSubtitle, uint32_t aRgb, float aHoldSeconds) noexcept;

/**
 * Fade out the banner showing, if any.
 */
void Hide() noexcept;

/**
 * Show a bar near the top of the view, a layer of its own, until HideMeter: banners and cards show beside it.
 * A call while it shows changes it in place, without a fade; one not shown again for 10 s fades out.
 *
 * @param acLabel The label above the bar, UTF-8; empty for none.
 * @param aFill From 0 to 1, how much of the bar the attackers' colour fills from the left; the defenders' colour fills the rest.
 * @param aAttackRgb The attackers' colour, 0xRRGGBB.
 * @param aDefendRgb The defenders' colour, 0xRRGGBB.
 */
void ShowMeter(const std::string& acLabel, float aFill, uint32_t aAttackRgb, uint32_t aDefendRgb) noexcept;

/**
 * Fade the meter out.
 */
void HideMeter() noexcept;

/**
 * Run the fades, and fade out a meter not shown again for a while; call once a frame.
 */
void Update() noexcept;
} // namespace VRBanner
