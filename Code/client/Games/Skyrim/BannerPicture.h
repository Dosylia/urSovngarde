#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

//! The pictures the VR banner shows, drawn without OpenVR, so a test can draw them too.
//! Text is written in the game's own fonts, read from its font library; Georgia stands in for a font or letter it lacks.
namespace BannerPicture
{
struct Picture
{
    int Width = 0;
    int Height = 0;
    //! RGBA, top row first: the colour to show at that alpha over the game.
    std::vector<uint8_t> Rgba;
};

//! The game's Data folder; its font library is read from there on first use.
void SetGameData(const std::filesystem::path& acData) noexcept;

//! One line of text in Skyrim's handwriting over a soft dark band (2048 x 384).
//! @param aTextHeight How tall the letters are, in 1024ths of the picture's width.
Picture Line(const std::wstring& acText, uint32_t aRgb, int aTextHeight) noexcept;

//! The death card (2048 x 640): an emblem over a rule, the title, a rule, a smaller line in spaced capitals, a rule.
//! An empty subtitle leaves its line and the last rule out.
Picture DeathCard(const std::wstring& acTitle, const std::wstring& acSubtitle, uint32_t aRgb) noexcept;

//! The meter (1152 x 200) with its bar still empty: a label in spaced capitals over a frame.
//! It changes only with the label, so it is drawn once for each; an empty label leaves its line blank.
Picture MeterFace(const std::wstring& acLabel) noexcept;

//! Fills a meter's bar, in place: the attackers' colour aFill (0 to 1) of it from the left, the defenders' colour the rest.
void FillMeter(Picture& aMeter, float aFill, uint32_t aAttackRgb, uint32_t aDefendRgb) noexcept;

//! Which of the fonts the pictures use could be read, for the log.
std::string FontReport() noexcept;

//! The picture over a dark grey ground, as a PNG: to look at it outside the headset.
bool WritePng(const std::filesystem::path& acPath, const Picture& acPicture) noexcept;
} // namespace BannerPicture
