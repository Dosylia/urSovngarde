#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Menu/EyeDrawChecks.h>
#include <UI/Menu/EyeGeometry.h>
#include <UI/Menu/PanelColours.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace EyeDrawChecks
{
namespace
{
using EyeDrawMarks::EyeMatrices;
using EyeDrawMarks::EyeVertices;
using EyeDrawMarks::Mark;
using EyeDrawMarks::Marks;

// A sample can fall under another mark, so a check tries again on later frames until its colour shows, a few times at most.
constexpr int kCheckTries = 20;
// How far each colour of a checked pixel may be from the expected one, for the eye image's rounding.
constexpr uint32_t kColourTolerance = 12;
// A banner layer is checked only once at least this much of it shows: early in its fade-in it barely changes the eye image.
constexpr float kCheckedLayerAlpha = 0.5f;
// The widest area of a layer's picture a check reads, in picture pixels: a wider one would only loosen the check.
constexpr uint32_t kLargestFootprint = 64;
// Every picture has four bytes a pixel: blue, green, red, alpha.
constexpr size_t kBytesPerPixel = 4;

// How many times each kind of mark has been tried; kCheckTries once it is done.
int s_panelTries = 0;
int s_beamTries = 0;
int s_ringTries = 0;
std::array<int, BannerPlacement::kLayerCount> s_layerTries{};

/**
 * Check if a kind of mark still has to be checked.
 *
 * @param aTries How many times its check has tried so far; kCheckTries once it is done.
 * @return True while its check has tries left.
 */
bool IsCheckPending(const int aTries) noexcept
{
    return aTries < kCheckTries;
}

/**
 * Read one pixel of the eye image back from the GPU; it waits for the GPU to get there.
 *
 * @param acImage The eye image.
 * @param aX The pixel, across.
 * @param aY The pixel, down.
 * @param aOut Filled with the pixel's four bytes, in the eye image's own order.
 * @return True when it was read; never for a multisampled image.
 */
bool ReadPixel(const EyeImage& acImage, const UINT aX, const UINT aY, uint8_t (&aOut)[4]) noexcept
{
    D3D11_TEXTURE2D_DESC staging = acImage.Desc;
    staging.Width = staging.Height = 1;
    staging.MipLevels = staging.ArraySize = 1;
    staging.SampleDesc.Count = 1;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.MiscFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* pStaging = nullptr;
    if (acImage.Desc.SampleDesc.Count != 1 || FAILED(acImage.pDevice->CreateTexture2D(&staging, nullptr, &pStaging)))
        return false;
    const D3D11_BOX cBox{aX, aY, 0, aX + 1, aY + 1, 1};
    acImage.pImmediate->CopySubresourceRegion(pStaging, 0, 0, 0, 0, acImage.pTarget, 0, &cBox);
    D3D11_MAPPED_SUBRESOURCE pixel{};
    const bool cRead = SUCCEEDED(acImage.pImmediate->Map(pStaging, 0, D3D11_MAP_READ, 0, &pixel));
    if (cRead)
    {
        std::memcpy(aOut, pixel.pData, 4);
        acImage.pImmediate->Unmap(pStaging, 0);
    }
    pStaging->Release();
    return cRead;
}

/**
 * Find the middle of a mark in an eye's clip space.
 *
 * @param acVertices The mark's corners in that eye.
 * @return The average of its four corners: x, y, z, w.
 */
std::array<float, 4> ClipCentreOf(const EyeVertices& acVertices) noexcept
{
    std::array<float, 4> centre{};
    for (const auto& cVertex : acVertices)
        for (int c = 0; c < 4; ++c)
            centre[c] += cVertex.Position[c] / 4.f;
    return centre;
}

/**
 * Turn a colour constant into the form a check expects.
 *
 * @param acRgb The colour: red, green, blue.
 * @return The same colour, one number per part.
 */
std::array<uint32_t, 3> RgbOf(const uint8_t (&acRgb)[3]) noexcept
{
    return {acRgb[0], acRgb[1], acRgb[2]};
}

/**
 * Add a check of one kind of mark, at a pixel of the left eye, with the tolerance for the eye image's rounding.
 *
 * @param aChecks This frame's checks.
 * @param acpWhat The kind of mark, for the log.
 * @param acpColourName The name of the colour expected, for the log.
 * @param acPixel The pixel to read, or nothing when the left eye does not see the mark: then no check is added.
 * @param acExpected The colour the pixel should have once drawn; a banner layer's is worked out later.
 * @param aTries The tries counter of that kind of mark.
 * @return The check added, or null when none was.
 */
Check* AddCheck(Checks& aChecks, const char* acpWhat, const char* acpColourName, const std::optional<std::pair<UINT, UINT>>& acPixel, const std::array<uint32_t, 3>& acExpected,
                int& aTries) noexcept
{
    if (!acPixel || aChecks.Count == aChecks.List.size())
        return nullptr;
    Check& check = aChecks.List[aChecks.Count++];
    check.pWhat = acpWhat;
    check.pColourName = acpColourName;
    check.Pixel = *acPixel;
    check.Expected = acExpected;
    check.Tolerance = {kColourTolerance, kColourTolerance, kColourTolerance};
    check.pTries = &aTries;
    return &check;
}

/**
 * Work out the colour the middle of the panel's picture should have in the eye image: the page's pixel over the dark panel, as the panel's shader lays it.
 *
 * @param acPanel The panel's picture.
 * @return Red, green, blue; black before a picture is on the device.
 */
std::array<uint32_t, 3> PictureCentreOverPanel(const EyeDrawPicture& acPanel) noexcept
{
    std::array<uint32_t, 3> colour{};
    const std::vector<uint8_t>& cPixels = acPanel.GetPixels();
    const size_t cIndex = (static_cast<size_t>(acPanel.GetHeight() / 2) * acPanel.GetWidth() + acPanel.GetWidth() / 2) * kBytesPerPixel;
    if (cIndex + 3 < cPixels.size())
    {
        for (int c = 0; c < 3; ++c)
            colour[c] = PanelColours::OverPanel(&cPixels[cIndex], c);
    }
    return colour;
}

/**
 * Add the checks of the panel's marks still unchecked: the panel's middle, the beam, and the first hand ring the left eye sees.
 *
 * @param aChecks This frame's checks.
 * @param acMarks The marks drawn this frame, the panel first.
 * @param acEyes The eyes they are drawn for; the left one is checked.
 * @param acViewport The left eye's part of the eye image.
 * @param acPanel The panel's picture.
 */
void AddPanelChecks(Checks& aChecks, const Marks& acMarks, const EyeMatrices& acEyes, const D3D11_VIEWPORT& acViewport, const EyeDrawPicture& acPanel) noexcept
{
    const auto cLeftEyePixel = [&](const glm::vec3& acHeadPoint)
    { return EyeGeometry::PixelOf(EyeGeometry::ToClip(acEyes.Projection[0].m, acEyes.EyeToHead[0].m, acHeadPoint), acViewport); };

    if (IsCheckPending(s_panelTries))
        AddCheck(aChecks, "panel", "picture centre", EyeGeometry::PixelOf(ClipCentreOf(acMarks.List[0].Eyes[0]), acViewport), PictureCentreOverPanel(acPanel), s_panelTries);
    if (IsCheckPending(s_beamTries) && acMarks.BeamMiddle)
        AddCheck(aChecks, "beam", "beam", cLeftEyePixel(*acMarks.BeamMiddle), RgbOf(PanelColours::kGoldRgb), s_beamTries);
    // The first ring the left eye sees: a hand held low can be out of view.
    for (const auto& cPoint : acMarks.RingPoints)
    {
        if (!IsCheckPending(s_ringTries) || aChecks.Count == aChecks.List.size())
            break;
        if (!cPoint)
            continue;
        const auto cPixel = cLeftEyePixel(*cPoint);
        if (!cPixel)
            continue;
        AddCheck(aChecks, "hand ring", "ring", cPixel, RgbOf(PanelColours::kPaleRgb), s_ringTries);
        break;
    }
}

/**
 * Work out how wide an area of a picture one sample of the GPU may read where a mark lands in the left eye.
 * The GPU reads the smaller level that matches how many picture pixels fall on one eye pixel, blends it with the next level, and blends neighbours within each.
 * That reaches up to four times as many picture pixels across.
 *
 * @param acVertices The mark's corners in the left eye's clip space, as a triangle strip.
 * @param acViewport The left eye's part of the eye image.
 * @param aWidth The picture's width in pixels.
 * @param aHeight Its height in pixels.
 * @return The side of that area in picture pixels, 2 to kLargestFootprint; nothing when the mark's corners are behind the eye or it covers less than a pixel.
 */
std::optional<uint32_t> SampledFootprint(const EyeVertices& acVertices, const D3D11_VIEWPORT& acViewport, const uint32_t aWidth, const uint32_t aHeight) noexcept
{
    // The strip's first three corners: top left, top right, bottom left.
    for (size_t i = 0; i < 3; ++i)
    {
        if (!(acVertices[i].Position[3] > 0.f))
            return std::nullopt;
    }
    const auto cAcross = [](const EyeDrawMarks::Vertex& acVertex) { return acVertex.Position[0] / acVertex.Position[3]; };
    const auto cDown = [](const EyeDrawMarks::Vertex& acVertex) { return acVertex.Position[1] / acVertex.Position[3]; };
    const float cWidthInEye = std::abs(cAcross(acVertices[1]) - cAcross(acVertices[0])) * 0.5f * acViewport.Width;
    const float cHeightInEye = std::abs(cDown(acVertices[2]) - cDown(acVertices[0])) * 0.5f * acViewport.Height;
    if (!(cWidthInEye >= 1.f && cHeightInEye >= 1.f))
        return std::nullopt;
    const float cPicturePerEyePixel = std::max(static_cast<float>(aWidth) / cWidthInEye, static_cast<float>(aHeight) / cHeightInEye);
    const float cSide = std::clamp(std::ceil(4.f * cPicturePerEyePixel), 2.f, static_cast<float>(kLargestFootprint));
    return static_cast<uint32_t>(cSide);
}

/**
 * Find the lowest and the highest a picture lays over the eye image within a square around its middle.
 *
 * @param acPicture The picture.
 * @param aSide The square's side, in picture pixels.
 * @return The range, or nothing before a picture is on the device.
 */
std::optional<PaintedRange> RangeAroundCentre(const EyeDrawPicture& acPicture, const uint32_t aSide) noexcept
{
    const std::vector<uint8_t>& cPixels = acPicture.GetPixels();
    const uint32_t cWidth = acPicture.GetWidth();
    const uint32_t cHeight = acPicture.GetHeight();
    if (cWidth == 0 || cHeight == 0 || cPixels.size() != static_cast<size_t>(cWidth) * cHeight * kBytesPerPixel)
        return std::nullopt;
    const uint32_t cHalf = aSide / 2;
    const uint32_t cLeft = cWidth / 2 > cHalf ? cWidth / 2 - cHalf : 0;
    const uint32_t cRight = std::min(cWidth, cWidth / 2 + cHalf + 1);
    const uint32_t cTop = cHeight / 2 > cHalf ? cHeight / 2 - cHalf : 0;
    const uint32_t cBottom = std::min(cHeight, cHeight / 2 + cHalf + 1);

    PaintedRange range{{255, 255, 255, 0}, {0, 0, 0, 255}};
    for (uint32_t y = cTop; y < cBottom; ++y)
    {
        for (uint32_t x = cLeft; x < cRight; ++x)
        {
            const uint8_t* pPixel = &cPixels[(static_cast<size_t>(y) * cWidth + x) * kBytesPerPixel];
            for (size_t c = 0; c < 3; ++c)
            {
                range.Lowest[c] = std::min(range.Lowest[c], pPixel[c]);
                range.Highest[c] = std::max(range.Highest[c], pPixel[c]);
            }
            // More alpha hides more of the eye image: the lowest takes the largest.
            range.Lowest[3] = std::max(range.Lowest[3], pPixel[3]);
            range.Highest[3] = std::min(range.Highest[3], pPixel[3]);
        }
    }
    return range;
}

/**
 * Add the check of each banner layer drawn this frame that is still unchecked and shows enough to change the eye image, at the middle of its picture.
 *
 * @param aChecks This frame's checks.
 * @param acMarks The marks drawn this frame.
 * @param acViewport The left eye's part of the eye image.
 * @param acLayers Each banner layer's picture, in drawing order.
 */
void AddLayerChecks(Checks& aChecks, const Marks& acMarks, const D3D11_VIEWPORT& acViewport, const std::array<EyeDrawPicture, BannerPlacement::kLayerCount>& acLayers) noexcept
{
    for (size_t i = 0; i < acMarks.LayerMarks.size(); ++i)
    {
        if (!acMarks.LayerMarks[i] || !IsCheckPending(s_layerTries[i]))
            continue;
        const Mark& cMark = acMarks.List[*acMarks.LayerMarks[i]];
        const float cAlpha = cMark.LayerAlpha.value_or(0.f);
        if (!(cAlpha >= kCheckedLayerAlpha))
            continue;
        const auto cFootprint = SampledFootprint(cMark.Eyes[0], acViewport, acLayers[i].GetWidth(), acLayers[i].GetHeight());
        const auto cRange = cFootprint ? RangeAroundCentre(acLayers[i], *cFootprint) : std::nullopt;
        if (!cRange)
            continue;
        const char* pName = BannerPlacement::NamesOf(static_cast<BannerPlacement::Layer>(i)).Log;
        Check* pCheck = AddCheck(aChecks, pName, "sampled centre over before", EyeGeometry::PixelOf(ClipCentreOf(cMark.Eyes[0]), acViewport), {}, s_layerTries[i]);
        if (!pCheck)
            continue;
        pCheck->LayerRange = cRange;
        pCheck->LayerAlpha = cAlpha;
    }
}

/**
 * Check if any banner layer is drawn this frame.
 *
 * @param acMarks This frame's marks.
 * @return True when at least one layer has a mark.
 */
bool HasLayerMark(const Marks& acMarks) noexcept
{
    return std::any_of(acMarks.LayerMarks.begin(), acMarks.LayerMarks.end(), [](const std::optional<size_t>& acIndex) { return acIndex.has_value(); });
}

/**
 * Work out what a banner layer's check expects, now its pixel before is read: the middle of the lowest and the highest the layer can give over it.
 * The tolerance covers that range on top of the eye image's rounding.
 *
 * @param aCheck The layer's check, with its range, its alpha and its pixel before; its expected colour and tolerance are filled in.
 */
void ExpectLayerOverBefore(Check& aCheck) noexcept
{
    for (int c = 0; c < 3; ++c)
    {
        const uint32_t cLow = PanelColours::OverImage(aCheck.LayerRange->Lowest.data(), aCheck.LayerAlpha, aCheck.Before[c], c);
        const uint32_t cHigh = PanelColours::OverImage(aCheck.LayerRange->Highest.data(), aCheck.LayerAlpha, aCheck.Before[c], c);
        aCheck.Expected[c] = (cLow + cHigh + 1) / 2;
        aCheck.Tolerance[c] = (std::max(cLow, cHigh) - std::min(cLow, cHigh) + 1) / 2 + kColourTolerance;
    }
}
} // namespace

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
Checks ChecksFor(const Marks& acMarks, const EyeMatrices& acEyes, const D3D11_VIEWPORT& acViewport, const EyeDrawPicture& acPanel,
                 const std::array<EyeDrawPicture, BannerPlacement::kLayerCount>& acLayers) noexcept
{
    Checks checks;
    // A pixel is read before any of this frame's drawing, so a mark is checked only on a frame where nothing else can cover it.
    // The panel's marks are checked while no banner layer is drawn, and a banner layer while it is drawn alone.
    const bool cLayerDrawn = HasLayerMark(acMarks);
    if (acMarks.HasPanel && !cLayerDrawn)
        AddPanelChecks(checks, acMarks, acEyes, acViewport, acPanel);
    if (cLayerDrawn && acMarks.Count == 1)
        AddLayerChecks(checks, acMarks, acViewport, acLayers);
    return checks;
}

/**
 * Read each check's pixel before the drawing, to log it next to the pixel after; a banner layer's expected colour is worked out from it.
 *
 * @param aChecks The checks; each one's pixel before is filled in.
 * @param acImage The eye image.
 */
void ReadPixelsBefore(Checks& aChecks, const EyeImage& acImage) noexcept
{
    for (size_t i = 0; i < aChecks.Count; ++i)
    {
        Check& check = aChecks.List[i];
        check.Read = ReadPixel(acImage, check.Pixel.first, check.Pixel.second, check.Before);
        if (check.Read && check.LayerRange)
            ExpectLayerOverBefore(check);
    }
}

/**
 * Read each check's pixel again after the drawing: the expected colour ends the check, and so does its last try; either way it is logged.
 *
 * @param acChecks The checks, with their pixels before.
 * @param acImage The eye image.
 */
void FinishChecks(const Checks& acChecks, const EyeImage& acImage) noexcept
{
    for (size_t i = 0; i < acChecks.Count; ++i)
    {
        const Check& cCheck = acChecks.List[i];
        uint8_t after[4]{};
        if (!cCheck.Read || !ReadPixel(acImage, cCheck.Pixel.first, cCheck.Pixel.second, after))
            continue;
        // Its colour, within the check's tolerance, ends the check; so does the last try, which is logged as it is.
        bool matches = true;
        for (int c = 0; c < 3; ++c)
            matches = matches && std::abs(static_cast<int>(after[c]) - static_cast<int>(cCheck.Expected[c])) <= static_cast<int>(cCheck.Tolerance[c]);
        *cCheck.pTries = matches ? kCheckTries : *cCheck.pTries + 1;
        if (!matches && IsCheckPending(*cCheck.pTries))
            continue;
        spdlog::info("VREyeDraw: {} drawn into the left eye at {},{}; pixel before {},{},{}, after {},{},{}, {} {},{},{}", cCheck.pWhat, cCheck.Pixel.first,
                     cCheck.Pixel.second, cCheck.Before[0], cCheck.Before[1], cCheck.Before[2], after[0], after[1], after[2], cCheck.pColourName, cCheck.Expected[0],
                     cCheck.Expected[1], cCheck.Expected[2]);
    }
}
} // namespace EyeDrawChecks

#endif
