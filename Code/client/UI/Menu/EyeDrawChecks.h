#pragma once

#ifdef SKYRIMVR

#include <UI/Menu/EyeDrawMarks.h>
#include <UI/Menu/EyeDrawPicture.h>

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

/**
 * One check per game that each kind of mark the eye drawer draws lands in the eye image, for the log: a pixel of the left eye, read before and after the drawing.
 * Reading a pixel waits for the GPU, so a kind of mark is checked once per game, tried again on later frames until its colour shows, a few times at most.
 * Render thread only.
 */
namespace EyeDrawChecks
{
/**
 * The eye image a frame is drawn into, and what reads its pixels back.
 */
struct EyeImage
{
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pImmediate = nullptr;
    ID3D11Texture2D* pTarget = nullptr;
    // Its size, format and samples.
    D3D11_TEXTURE2D_DESC Desc{};
};

/**
 * The lowest and the highest a picture lays over the eye image within an area: any filtered sample of that area lies between the two.
 * Each is a pixel, BGRA premultiplied: the lowest has every colour's smallest value with the largest alpha, the highest the reverse.
 */
struct PaintedRange
{
    std::array<uint8_t, 4> Lowest{};
    std::array<uint8_t, 4> Highest{};
};

/**
 * A check that one kind of mark lands in the eye image: a pixel of the left eye, read before and after the drawing.
 */
struct Check
{
    // The kind of mark and the name of its colour, for the log.
    const char* pWhat = nullptr;
    const char* pColourName = nullptr;
    std::pair<UINT, UINT> Pixel{};
    // The colour the pixel should have once drawn: red, green, blue.
    std::array<uint32_t, 3> Expected{};
    // How far each colour of the pixel after may be from the expected one.
    std::array<uint32_t, 3> Tolerance{};
    // The tries counter of that kind of mark.
    int* pTries = nullptr;
    uint8_t Before[4]{};
    // Whether the pixel could be read before the drawing.
    bool Read = false;
    // For a banner layer: its picture's range where the GPU samples it, and its alpha; the colour expected is worked out once the pixel before is read.
    std::optional<PaintedRange> LayerRange;
    float LayerAlpha = 0.f;
};

// The panel's checks at most: its middle, the beam and one hand ring.
constexpr size_t kPanelChecks = 3;

/**
 * This frame's checks: at most one for each kind of mark.
 */
struct Checks
{
    // The panel's checks, then one for each banner layer.
    std::array<Check, kPanelChecks + BannerPlacement::kLayerCount> List{};
    size_t Count = 0;
};

/**
 * Choose this frame's checks for the left eye: for each kind of mark still unchecked, a pixel where it shows and the colour it should have there.
 *
 * @param acMarks The marks drawn this frame.
 * @param acEyes The eyes they are drawn for; the left one is checked.
 * @param acViewport The left eye's part of the eye image.
 * @param acPanel The panel's picture, as uploaded this frame.
 * @param acLayers Each banner layer's picture, as uploaded this frame, in drawing order.
 * @return The checks; none for a mark the left eye does not see.
 */
Checks ChecksFor(const EyeDrawMarks::Marks& acMarks, const EyeDrawMarks::EyeMatrices& acEyes, const D3D11_VIEWPORT& acViewport, const EyeDrawPicture& acPanel,
                 const std::array<EyeDrawPicture, BannerPlacement::kLayerCount>& acLayers) noexcept;

/**
 * Read each check's pixel before the drawing, to log it next to the pixel after; a banner layer's expected colour is worked out from it.
 *
 * @param aChecks The checks; each one's pixel before is filled in.
 * @param acImage The eye image.
 */
void ReadPixelsBefore(Checks& aChecks, const EyeImage& acImage) noexcept;

/**
 * Read each check's pixel again after the drawing: the expected colour ends the check, and so does its last try; either way it is logged.
 *
 * @param acChecks The checks, with their pixels before.
 * @param acImage The eye image.
 */
void FinishChecks(const Checks& acChecks, const EyeImage& acImage) noexcept;
} // namespace EyeDrawChecks

#endif
