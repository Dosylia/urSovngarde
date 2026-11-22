#pragma once

#ifdef SKYRIMVR

#include <UI/Banner/BannerPlacement.h>

#include <openvr/openvr.h>

#include <d3d11.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <optional>

/**
 * What the eye drawer draws in one frame, as its drawing and its checks both read it: the marks in each eye, and the eyes they are worked out for.
 */
namespace EyeDrawMarks
{
/**
 * One corner of a mark as the GPU takes it: where it lands in an eye, and which point of the picture it shows.
 */
struct Vertex
{
    // In the eye's clip space: x, y, z, w.
    float Position[4];
    // Across and down the picture, 0 to 1.
    float Uv[2];
};
// A mark's four corners in one eye, as a triangle strip: top left, top right, bottom left, bottom right.
using EyeVertices = std::array<Vertex, 4>;

/**
 * Each eye's projection and place in the head, left then right, as the runtime gives them.
 */
struct EyeMatrices
{
    std::array<vr::HmdMatrix44_t, 2> Projection{};
    std::array<vr::HmdMatrix34_t, 2> EyeToHead{};
};

/**
 * One mark in both eyes: its corners, the shader that colours it, the picture that shader reads, and how it is laid over the eye image.
 */
struct Mark
{
    std::array<EyeVertices, 2> Eyes{};
    ID3D11PixelShader* pShader = nullptr;
    // The panel's page for the panel and its pointing, or a banner layer's picture.
    ID3D11ShaderResourceView* pPicture = nullptr;
    // A banner layer's alpha, laid over the eye image; nothing for the panel and its pointing, drawn opaque.
    std::optional<float> LayerAlpha;
};

// The panel's marks at most: the panel, the beam, the dot and a ring for each hand.
constexpr size_t kPanelMarks = 5;

/**
 * Everything to draw this frame, and the points the once-per-game checks sample.
 */
struct Marks
{
    // The panel, then the beam, the dot and the rings over it, then the banner layers over everything, in that order: nothing has depth, the last drawn is seen.
    std::array<Mark, kPanelMarks + BannerPlacement::kLayerCount> List{};
    size_t Count = 0;
    // Whether the first mark is the panel.
    bool HasPanel = false;
    // Where each banner layer's mark is in the list, when it is drawn.
    std::array<std::optional<size_t>, BannerPlacement::kLayerCount> LayerMarks;
    // The beam's middle in the headset's space, when the beam is drawn.
    std::optional<glm::vec3> BeamMiddle;
    // A point on each hand's ring in the headset's space, when that ring is drawn.
    std::array<std::optional<glm::vec3>, 2> RingPoints;
};
} // namespace EyeDrawMarks

#endif
