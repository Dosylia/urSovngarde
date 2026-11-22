#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Menu/VREyeDraw.h>
#include <UI/Menu/EyeDrawChecks.h>
#include <UI/Menu/EyeDrawMarks.h>
#include <UI/Menu/EyeDrawPicture.h>
#include <UI/Menu/EyeGeometry.h>
#include <UI/Menu/PanelColours.h>

#include <Games/Skyrim/VR/VRRuntime.h>
#include <Games/Skyrim/VR/VRControllers.h>
#include <Helpers/SafeRelease.h>

#include <openvr/openvr.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

#pragma comment(lib, "d3dcompiler")

namespace
{
using EyeDrawMarks::EyeMatrices;
using EyeDrawMarks::EyeVertices;
using EyeDrawMarks::Mark;
using EyeDrawMarks::Marks;
using EyeDrawMarks::Vertex;

using TVR_GetGenericInterface = void*(__cdecl*)(const char*, vr::EVRInitError*);
TVR_GetGenericInterface RealVR_GetGenericInterface = nullptr;

using TSubmit = vr::EVRCompositorError (*)(void*, vr::EVREye, const vr::Texture_t*, const vr::VRTextureBounds_t*, vr::EVRSubmitFlags);
TSubmit RealSubmit = nullptr;
std::atomic<bool> s_hooked{false};

// The compositor's interface names start with this, whatever their version.
constexpr char kCompositorPrefix[] = "IVRCompositor_";
// Submit is the sixth slot of every compositor version's method table.
constexpr size_t kSubmitSlot = 5;

// The thread the game renders and submits on; around a load it submits from another, and nothing is drawn then.
// The menu opens and closes on the game's update thread, another one: what the two share goes under s_lock or is atomic.
DWORD s_mainThread = 0;

// The shaders' code; their colours Panel, Gold and Pale are written in front of it from the C++ constants (ShaderSource).
// The picture is premultiplied, as CEF paints it: the dark panel shows where the page is transparent.
// A banner layer's picture is premultiplied too; PsLayer scales it by the layer's alpha, and the blend lays it over the eye image.
constexpr const char* kShaderCode = R"(
struct VsIn { float4 Position : POSITION; float2 Uv : TEXCOORD0; };
struct PsIn { float4 Position : SV_POSITION; float2 Uv : TEXCOORD0; };
PsIn VsMain(VsIn i) { PsIn o; o.Position = i.Position; o.Uv = i.Uv; return o; }
Texture2D Picture : register(t0);
SamplerState Linear : register(s0);
float4 PsPanel(PsIn i) : SV_Target { float4 c = Picture.Sample(Linear, i.Uv); return float4(c.rgb + Panel * (1.0 - c.a), 1.0); }
float4 PsBeam(PsIn i) : SV_Target { return float4(Gold, 1.0); }
float4 PsDot(PsIn i) : SV_Target { float2 d = i.Uv * 2.0 - 1.0; clip(1.0 - dot(d, d)); return float4(Gold, 1.0); }
float4 PsRing(PsIn i) : SV_Target { float2 d = i.Uv * 2.0 - 1.0; float r = dot(d, d); clip(1.0 - r); clip(r - 0.45); return float4(Pale, 1.0); }
cbuffer Layer : register(b0) { float LayerAlpha; float3 LayerPadding; };
float4 PsLayer(PsIn i) : SV_Target { return Picture.Sample(Linear, i.Uv) * LayerAlpha; }
)";

// The shaders in kShaderCode: the vertex shader every mark shares, and the pixel shader that colours each kind of mark.
enum Shader : size_t
{
    kVertexShader,
    kPanelShader,
    kBeamShader,
    kDotShader,
    kRingShader,
    kLayerShader,
    kShaderCount,
};
// Each shader's function in kShaderCode, in the order of Shader.
constexpr const char* kShaderEntries[kShaderCount]{"VsMain", "PsPanel", "PsBeam", "PsDot", "PsRing", "PsLayer"};

/**
 * What the layer shader reads from its constant buffer; the GPU takes constants in blocks of 16 bytes.
 */
struct LayerConstants
{
    // How much of the layer shows, 0 to 1.
    float Alpha = 0.f;
    float Padding[3]{};
};
// Every sample of a pixel takes the blend: its sample mask.
constexpr UINT kAllSamples = 0xFFFFFFFF;

// In metres: half the dot's width, half the beam's, half the hand ring's.
constexpr float kDotHalf = 0.008f;
constexpr float kBeamHalf = 0.002f;
constexpr float kRingHalf = 0.02f;
// In metres: the nearest and the farthest the eyes' projections draw.
constexpr float kNearClip = 0.05f;
constexpr float kFarClip = 100.f;

// Four corners in the headset's space, from the top left, clockwise as the player faces them.
using HeadCorners = EyeGeometry::HeadCorners;

/**
 * Everything the panel and the banner layers are drawn with, all on the game's device, made at its first frame submitted from the main thread.
 */
struct Drawer
{
    vr::IVRSystem* pSystem = nullptr;
    ID3D11Device* pDevice = nullptr;
    // Records the drawing, so it runs in one go with the game's own state put back afterwards.
    ID3D11DeviceContext* pDeferred = nullptr;
    ID3D11VertexShader* pVertexShader = nullptr;
    ID3D11PixelShader* pPanelShader = nullptr;
    ID3D11PixelShader* pBeamShader = nullptr;
    ID3D11PixelShader* pDotShader = nullptr;
    ID3D11PixelShader* pRingShader = nullptr;
    ID3D11PixelShader* pLayerShader = nullptr;
    ID3D11InputLayout* pLayout = nullptr;
    ID3D11Buffer* pVertices = nullptr;
    ID3D11SamplerState* pSampler = nullptr;
    ID3D11RasterizerState* pRaster = nullptr;
    // How a banner layer is laid over the eye image, and the constant buffer that gives the layer shader its alpha.
    ID3D11BlendState* pLayerBlend = nullptr;
    ID3D11Buffer* pLayerAlpha = nullptr;
    // The eye image last drawn into, and the view to draw into it.
    ID3D11Texture2D* pTarget = nullptr;
    ID3D11RenderTargetView* pTargetView = nullptr;
    std::atomic<bool> Made{false};
    std::atomic<bool> Failed{false};
};
Drawer s_drawer;

std::mutex s_lock;
// The panel's page, and each banner layer's picture in drawing order: given under s_lock, taken over with the scene.
EyeDrawPicture s_panelPicture;
std::array<EyeDrawPicture, BannerPlacement::kLayerCount> s_layerPictures;
// The panel in the game's world, while it is shown, and what pointing draws over it.
std::optional<VREyeDraw::Quad> s_panel;
VREyeDraw::Pointing s_pointing;
// Each banner layer's corners in the headset's space while it is shown, in drawing order.
std::array<std::optional<EyeGeometry::HeadCorners>, BannerPlacement::kLayerCount> s_layerCorners;
// How much of each banner layer shows, 0 to 1.
std::array<float, BannerPlacement::kLayerCount> s_layerAlpha{};
// The eyes, asked of the runtime as the panel or a layer starts showing.
EyeMatrices s_eyes;
// The double-wide eye image the right eye was already drawn into with the left, at the left eye's Submit.
ID3D11Texture2D* s_bothEyesDrawnInto = nullptr;

/**
 * Give up drawing into the eye images for the rest of the game, and log why the first time.
 * Then no panel shows inside the game, and on OpenComposite no banner either.
 *
 * @param acpWhat What went wrong, for the log.
 */
void Fail(const char* acpWhat) noexcept
{
    if (!s_drawer.Failed.exchange(true))
        spdlog::error("VREyeDraw: {}; no panel inside the game this session, and on OpenComposite no banners either", acpWhat);
}

/**
 * Write the shaders' whole source: their colours from the C++ constants, then their code.
 * GPU code cannot read C++ constants, so each colour is written into the source instead of being typed twice.
 *
 * @return The HLSL source of every shader in Shader.
 */
std::string ShaderSource()
{
    // A colour as an HLSL constant, each part from 0 to 1.
    const auto cColour = [](const char* acpName, const uint8_t (&acRgb)[3])
    {
        return fmt::format("static const float3 {} = float3({}.0, {}.0, {}.0) / 255.0;\n", acpName, static_cast<int>(acRgb[0]), static_cast<int>(acRgb[1]),
                           static_cast<int>(acRgb[2]));
    };
    return cColour("Panel", PanelColours::kPanelRgb) + cColour("Gold", PanelColours::kGoldRgb) + cColour("Pale", PanelColours::kPaleRgb) + kShaderCode;
}

/**
 * Compile the shaders.
 *
 * @param aBlobs Filled with each compiled shader, in the order of Shader; the caller releases them.
 * @return True when all of them compiled; otherwise the errors are logged and nothing is left to release.
 */
bool CompileShaders(std::array<ID3DBlob*, kShaderCount>& aBlobs) noexcept
{
    const std::string cSource = ShaderSource();
    ID3DBlob* pErrors = nullptr;
    bool compiled = true;
    for (size_t i = 0; i < kShaderCount && compiled; ++i)
    {
        // Each compile gives its own messages: only the failing one's are logged.
        SafeRelease(pErrors);
        const char* pProfile = i == kVertexShader ? "vs_4_0" : "ps_4_0";
        compiled = SUCCEEDED(D3DCompile(cSource.data(), cSource.size(), nullptr, nullptr, nullptr, kShaderEntries[i], pProfile, 0, 0, &aBlobs[i], &pErrors));
    }
    if (!compiled)
    {
        spdlog::error("VREyeDraw: shader errors: {}", pErrors ? static_cast<const char*>(pErrors->GetBufferPointer()) : "none given");
        for (auto*& pBlob : aBlobs)
            SafeRelease(pBlob);
    }
    SafeRelease(pErrors);
    return compiled;
}

/**
 * Make what a banner layer is laid over the eye image with: its blend, and the constant buffer that gives its alpha.
 * The layer is premultiplied: its colour is added, and the eye image shows through as much as the layer lets.
 * The eye image's own alpha is left as the game wrote it.
 *
 * @param apDevice The game's device.
 * @return True when both were made.
 */
bool MakeLayerObjects(ID3D11Device* apDevice) noexcept
{
    D3D11_BLEND_DESC blend{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_ZERO;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

    D3D11_BUFFER_DESC alpha{};
    alpha.ByteWidth = sizeof(LayerConstants);
    alpha.Usage = D3D11_USAGE_DYNAMIC;
    alpha.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    alpha.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(apDevice->CreateBlendState(&blend, &s_drawer.pLayerBlend)) && SUCCEEDED(apDevice->CreateBuffer(&alpha, nullptr, &s_drawer.pLayerAlpha));
}

/**
 * Make the objects the panel and the banner layers are drawn with on the game's device: shaders, the corners' layout and buffer, the sampler, the rasterizer, the layers' blend.
 *
 * @param acBlobs The compiled shaders, in the order of Shader.
 * @return True when every object was made.
 */
bool MakeDrawingObjects(const std::array<ID3DBlob*, kShaderCount>& acBlobs) noexcept
{
    ID3D11Device* pDevice = s_drawer.pDevice;
    ID3DBlob* pVertexBlob = acBlobs[kVertexShader];
    const auto cMakePixelShader = [&](const Shader aShader, ID3D11PixelShader** appShader)
    { return SUCCEEDED(pDevice->CreatePixelShader(acBlobs[aShader]->GetBufferPointer(), acBlobs[aShader]->GetBufferSize(), nullptr, appShader)); };

    // Each corner: where it lands in the eye, then which point of the picture it shows.
    const D3D11_INPUT_ELEMENT_DESC cLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    bool made = SUCCEEDED(pDevice->CreateDeferredContext(0, &s_drawer.pDeferred)) &&
                SUCCEEDED(pDevice->CreateVertexShader(pVertexBlob->GetBufferPointer(), pVertexBlob->GetBufferSize(), nullptr, &s_drawer.pVertexShader)) &&
                cMakePixelShader(kPanelShader, &s_drawer.pPanelShader) && cMakePixelShader(kBeamShader, &s_drawer.pBeamShader) &&
                cMakePixelShader(kDotShader, &s_drawer.pDotShader) && cMakePixelShader(kRingShader, &s_drawer.pRingShader) &&
                cMakePixelShader(kLayerShader, &s_drawer.pLayerShader) &&
                SUCCEEDED(pDevice->CreateInputLayout(cLayout, 2, pVertexBlob->GetBufferPointer(), pVertexBlob->GetBufferSize(), &s_drawer.pLayout));

    D3D11_BUFFER_DESC vertices{};
    vertices.ByteWidth = sizeof(EyeVertices);
    vertices.Usage = D3D11_USAGE_DYNAMIC;
    vertices.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertices.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    made = made && SUCCEEDED(pDevice->CreateBuffer(&vertices, nullptr, &s_drawer.pVertices));

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    made = made && SUCCEEDED(pDevice->CreateSamplerState(&sampler, &s_drawer.pSampler));

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    made = made && SUCCEEDED(pDevice->CreateRasterizerState(&raster, &s_drawer.pRaster));
    return made && MakeLayerObjects(pDevice);
}

/**
 * Make everything the panel is drawn with, on the game's device, at the first eye image submitted from the main thread.
 *
 * @param apTarget That first eye image; the game's device is the one it lives on.
 * @return True when the panel can be drawn; otherwise the panel is off for the rest of the game.
 */
bool MakeDrawer(ID3D11Texture2D* apTarget) noexcept
{
    s_drawer.pSystem = VRRuntime::GetOpenVRSystem();
    if (!s_drawer.pSystem)
    {
        Fail("the VR runtime gave no system interface");
        return false;
    }
    apTarget->GetDevice(&s_drawer.pDevice);
    if (!s_drawer.pDevice)
    {
        Fail("the eye image has no device");
        return false;
    }
    // The game's device lives as long as the game: the reference is kept.

    std::array<ID3DBlob*, kShaderCount> blobs{};
    if (!CompileShaders(blobs))
    {
        Fail("the panel's shaders did not compile");
        return false;
    }
    const bool cMade = MakeDrawingObjects(blobs);
    for (auto*& pBlob : blobs)
        SafeRelease(pBlob);
    if (!cMade)
    {
        Fail("the panel's drawing objects could not be made on the game's device");
        return false;
    }
    s_drawer.Made = true;
    spdlog::info("VREyeDraw: drawing objects made on the game's device; the panel can be shown");
    return true;
}

/**
 * Put a picture taken over this frame on the game's device.
 *
 * @param aPicture The picture.
 * @param acpWhat What it shows, for the log: "panel", or a banner layer's name.
 * @param apImmediate The game's device context, which runs the upload.
 * @return True when it can be drawn; a texture that cannot be made turns the panel and the banner layers off for the rest of the game.
 */
bool UploadForFrame(EyeDrawPicture& aPicture, const char* acpWhat, ID3D11DeviceContext* apImmediate) noexcept
{
    const EyeDrawPicture::Upload cUpload = aPicture.PutOnDevice(s_drawer.pDevice, apImmediate);
    if (cUpload == EyeDrawPicture::Upload::kFailed)
        Fail(fmt::format("the {}'s picture could not be made on the game's device", acpWhat).c_str());
    return cUpload == EyeDrawPicture::Upload::kReady;
}

/**
 * Make the view to draw into an eye image, when it is not the one last drawn into.
 *
 * @param apTarget The eye image the game submits now.
 * @return True when it can be drawn into; otherwise the panel is off for the rest of the game.
 */
bool EnsureTargetView(ID3D11Texture2D* apTarget) noexcept
{
    if (s_drawer.pTarget == apTarget && s_drawer.pTargetView)
        return true;
    SafeRelease(s_drawer.pTargetView);
    s_drawer.pTarget = nullptr;
    if (FAILED(s_drawer.pDevice->CreateRenderTargetView(apTarget, nullptr, &s_drawer.pTargetView)))
    {
        Fail("the game's eye image cannot be drawn into");
        return false;
    }
    s_drawer.pTarget = apTarget;
    return true;
}

/**
 * Work out a mark's corners as one eye sees them, ready for the GPU.
 *
 * @param acProjection The eye's projection.
 * @param acEyeToHead Where the eye is in the head.
 * @param acCorners The mark's corners in the headset's space.
 * @return The corners in the eye's clip space, with the picture's points they show, as a triangle strip.
 */
EyeVertices VerticesFor(const vr::HmdMatrix44_t& acProjection, const vr::HmdMatrix34_t& acEyeToHead, const HeadCorners& acCorners) noexcept
{
    // A triangle strip: top left, top right, bottom left, bottom right.
    constexpr int kOrder[4] = {0, 1, 3, 2};
    constexpr float kUv[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
    EyeVertices vertices{};
    for (int i = 0; i < 4; ++i)
    {
        const auto cClip = EyeGeometry::ToClip(acProjection.m, acEyeToHead.m, acCorners[kOrder[i]]);
        std::memcpy(vertices[i].Position, cClip.data(), sizeof(vertices[i].Position));
        vertices[i].Uv[0] = kUv[i][0];
        vertices[i].Uv[1] = kUv[i][1];
    }
    return vertices;
}

/**
 * Find one eye's part of the eye image, from the bounds the game submits it with.
 *
 * @param acTarget The eye image's description: its size.
 * @param acBounds The eye's part of it, from 0 to 1 across and down.
 * @return That part in pixels, for drawing into.
 */
D3D11_VIEWPORT ViewportFor(const D3D11_TEXTURE2D_DESC& acTarget, const vr::VRTextureBounds_t& acBounds) noexcept
{
    // Bounds may run either way in v (an image stored upside down); the viewport only takes them top to bottom.
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = std::min(acBounds.uMin, acBounds.uMax) * acTarget.Width;
    viewport.TopLeftY = std::min(acBounds.vMin, acBounds.vMax) * acTarget.Height;
    viewport.Width = std::abs(acBounds.uMax - acBounds.uMin) * acTarget.Width;
    viewport.Height = std::abs(acBounds.vMax - acBounds.vMin) * acTarget.Height;
    viewport.MaxDepth = 1.f;
    return viewport;
}

/**
 * Record how the next mark is laid over the eye image: opaque, or a banner layer at its alpha.
 *
 * @param aLayerAlpha A banner layer's alpha, 0 to 1; nothing for a mark drawn opaque.
 * @return True when recorded; false when the layer's alpha could not be written, which turns the panel and the layers off for the rest of the game.
 */
bool RecordBlend(const std::optional<float> aLayerAlpha) noexcept
{
    ID3D11DeviceContext* pContext = s_drawer.pDeferred;
    if (!aLayerAlpha)
    {
        // No blend state: the default draws the mark opaque.
        pContext->OMSetBlendState(nullptr, nullptr, kAllSamples);
        return true;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(pContext->Map(s_drawer.pLayerAlpha, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        Fail("a banner layer's alpha could not be written");
        return false;
    }
    LayerConstants constants;
    constants.Alpha = *aLayerAlpha;
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    pContext->Unmap(s_drawer.pLayerAlpha, 0);
    pContext->OMSetBlendState(s_drawer.pLayerBlend, nullptr, kAllSamples);
    pContext->PSSetConstantBuffers(0, 1, &s_drawer.pLayerAlpha);
    return true;
}

/**
 * Record drawing one mark into an eye's part of the eye image, to run later with the rest.
 *
 * @param acVertices The mark's corners in that eye.
 * @param acViewport The eye's part of the eye image.
 * @param acMark The mark: its shader, its picture, and how it is laid over the eye image.
 */
void RecordDraw(const EyeVertices& acVertices, const D3D11_VIEWPORT& acViewport, const Mark& acMark) noexcept
{
    ID3D11DeviceContext* pContext = s_drawer.pDeferred;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(pContext->Map(s_drawer.pVertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        Fail("the panel's corners could not be written");
        return;
    }
    std::memcpy(mapped.pData, acVertices.data(), sizeof(EyeVertices));
    pContext->Unmap(s_drawer.pVertices, 0);
    if (!RecordBlend(acMark.LayerAlpha))
        return;

    const UINT cStride = sizeof(Vertex);
    const UINT cOffset = 0;
    // No depth state: every mark is drawn over everything the game drew, so a menu is never behind a wall.
    pContext->OMSetRenderTargets(1, &s_drawer.pTargetView, nullptr);
    pContext->RSSetViewports(1, &acViewport);
    pContext->RSSetState(s_drawer.pRaster);
    pContext->IASetInputLayout(s_drawer.pLayout);
    pContext->IASetVertexBuffers(0, 1, &s_drawer.pVertices, &cStride, &cOffset);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->VSSetShader(s_drawer.pVertexShader, nullptr, 0);
    pContext->PSSetShader(acMark.pShader, nullptr, 0);
    pContext->PSSetShaderResources(0, 1, &acMark.pPicture);
    pContext->PSSetSamplers(0, 1, &s_drawer.pSampler);
    pContext->Draw(4, 0);
}

/**
 * A banner layer as it is drawn this frame: where it is in the view, and how much of it shows.
 */
struct ShownLayer
{
    // Its corners in the headset's space.
    HeadCorners Corners{};
    // From 0 to 1.
    float Alpha = 0.f;
};

/**
 * What the update thread last gave to draw, copied for one frame: the panel, the pointing over it, the banner layers, and the eyes to draw them for.
 */
struct Scene
{
    // The panel in the game's world, or nothing while it is hidden.
    std::optional<VREyeDraw::Quad> Panel;
    VREyeDraw::Pointing Pointing;
    // Each banner layer shown, in drawing order.
    std::array<std::optional<ShownLayer>, BannerPlacement::kLayerCount> Layers;
    EyeMatrices Eyes;
};

/**
 * Check if any part of a scene is left to draw.
 *
 * @param acScene The scene.
 * @return True when the panel or a banner layer is in it.
 */
bool HasAnything(const Scene& acScene) noexcept
{
    return acScene.Panel || std::any_of(acScene.Layers.begin(), acScene.Layers.end(), [](const std::optional<ShownLayer>& acLayer) { return acLayer.has_value(); });
}

/**
 * Copy what the update thread last gave to draw, and take over the pictures it gave, under the lock shared with it.
 * Taken together, a frame draws a picture only with the place and alpha it was given with.
 *
 * @return The scene, or nothing while neither the panel nor a banner layer is shown.
 */
std::optional<Scene> TakeScene() noexcept
{
    std::scoped_lock lock(s_lock);
    s_panelPicture.TakeOver();
    for (EyeDrawPicture& picture : s_layerPictures)
        picture.TakeOver();
    Scene scene{s_panel, s_pointing, {}, s_eyes};
    for (size_t i = 0; i < scene.Layers.size(); ++i)
    {
        if (s_layerCorners[i])
            scene.Layers[i] = ShownLayer{*s_layerCorners[i], s_layerAlpha[i]};
    }
    if (!HasAnything(scene))
        return std::nullopt;
    return scene;
}

/**
 * Put the pictures of what is shown on the game's device, and leave out what cannot be drawn this frame.
 * The panel is left out without the headset's place or before its first picture; a banner layer while it shows nothing or before its first picture.
 *
 * @param aScene What the update thread gave to draw; the parts that cannot be drawn are taken out of it.
 * @param acHead The headset where the game rendered this frame from, or nothing when not known.
 * @param apImmediate The game's device context, which runs the uploads.
 * @return True when anything is left to draw.
 */
bool KeepDrawableParts(Scene& aScene, const std::optional<VRControllers::HeadSpace>& acHead, ID3D11DeviceContext* apImmediate) noexcept
{
    if (aScene.Panel && !(acHead && UploadForFrame(s_panelPicture, "panel", apImmediate)))
        aScene.Panel.reset();
    for (size_t i = 0; i < aScene.Layers.size(); ++i)
    {
        std::optional<ShownLayer>& layer = aScene.Layers[i];
        if (!layer)
            continue;
        const char* pName = BannerPlacement::NamesOf(static_cast<BannerPlacement::Layer>(i)).Log;
        const bool cVisible = layer->Alpha > 0.f;
        if (!cVisible || !UploadForFrame(s_layerPictures[i], pName, apImmediate))
            layer.reset();
    }
    return HasAnything(aScene);
}

/**
 * Add one mark to this frame's list, worked out for both eyes.
 *
 * @param aMarks The list it goes into.
 * @param acEyes The eyes it is drawn for.
 * @param acCorners Its corners in the headset's space.
 * @param apShader The pixel shader that colours it.
 * @param apPicture The picture that shader reads.
 * @param aLayerAlpha A banner layer's alpha; nothing for a mark drawn opaque.
 * @return Where it is in the list.
 */
size_t AddMark(Marks& aMarks, const EyeMatrices& acEyes, const HeadCorners& acCorners, ID3D11PixelShader* apShader, ID3D11ShaderResourceView* apPicture,
               const std::optional<float> aLayerAlpha) noexcept
{
    const std::array<EyeVertices, 2> cEyes{VerticesFor(acEyes.Projection[0], acEyes.EyeToHead[0], acCorners), VerticesFor(acEyes.Projection[1], acEyes.EyeToHead[1], acCorners)};
    aMarks.List[aMarks.Count] = {cEyes, apShader, apPicture, aLayerAlpha};
    return aMarks.Count++;
}

/**
 * Add the panel and the pointing over it, placed in the game's world and seen from where the game rendered this frame.
 *
 * @param aMarks This frame's marks.
 * @param acScene The scene, with its panel.
 * @param acHead The headset where the game rendered this frame from.
 */
void AddPanelMarks(Marks& aMarks, const Scene& acScene, const VRControllers::HeadSpace& acHead) noexcept
{
    ID3D11ShaderResourceView* pPage = s_panelPicture.GetView();
    const auto cAdd = [&](const HeadCorners& acCorners, ID3D11PixelShader* apShader) { AddMark(aMarks, acScene.Eyes, acCorners, apShader, pPage, std::nullopt); };

    HeadCorners panel{};
    for (size_t i = 0; i < panel.size(); ++i)
        panel[i] = acHead.Point(acScene.Panel->Corners[i]);
    cAdd(panel, s_drawer.pPanelShader);
    aMarks.HasPanel = true;

    if (acScene.Pointing.Beam)
    {
        const glm::vec3 cFrom = acHead.Point((*acScene.Pointing.Beam)[0]);
        const glm::vec3 cTo = acHead.Point((*acScene.Pointing.Beam)[1]);
        if (const auto cStrip = EyeGeometry::Strip(cFrom, cTo, kBeamHalf))
        {
            cAdd(*cStrip, s_drawer.pBeamShader);
            aMarks.BeamMiddle = (cFrom + cTo) * 0.5f;
        }
    }
    if (acScene.Pointing.Dot)
        cAdd(EyeGeometry::Facing(acHead.Point(*acScene.Pointing.Dot), kDotHalf), s_drawer.pDotShader);
    for (size_t i = 0; i < acScene.Pointing.Hands.size(); ++i)
    {
        if (!acScene.Pointing.Hands[i])
            continue;
        const glm::vec3 cCentre = acHead.Point(*acScene.Pointing.Hands[i]);
        const HeadCorners cRing = EyeGeometry::Facing(cCentre, kRingHalf);
        cAdd(cRing, s_drawer.pRingShader);
        // A point on the ring itself, which is hollow: most of the way from its centre to its right edge.
        const glm::vec3 cRightEdge = (cRing[1] + cRing[2]) * 0.5f;
        aMarks.RingPoints[i] = cCentre + (cRightEdge - cCentre) * 0.85f;
    }
}

/**
 * Add the banner layers shown this frame, in drawing order.
 * Their corners are already in the headset's space, so each lands at the same place in the view whichever way the head turns.
 *
 * @param aMarks This frame's marks.
 * @param acScene The scene, with its layers.
 */
void AddLayerMarks(Marks& aMarks, const Scene& acScene) noexcept
{
    for (size_t i = 0; i < acScene.Layers.size(); ++i)
    {
        if (const auto& cLayer = acScene.Layers[i])
            aMarks.LayerMarks[i] = AddMark(aMarks, acScene.Eyes, cLayer->Corners, s_drawer.pLayerShader, s_layerPictures[i].GetView(), cLayer->Alpha);
    }
}

/**
 * Work out every mark to draw this frame, in both eyes: the panel and its pointing from where the game rendered the frame, then the banner layers over them.
 *
 * @param acScene What to draw: the panel in the game's world, the layers in the headset's space.
 * @param acHead The headset where the game rendered this frame from; the panel is drawn only with it.
 * @return The marks in drawing order, with the points the checks sample.
 */
Marks MarksFor(const Scene& acScene, const std::optional<VRControllers::HeadSpace>& acHead) noexcept
{
    Marks marks;
    if (acScene.Panel && acHead)
        AddPanelMarks(marks, acScene, *acHead);
    AddLayerMarks(marks, acScene);
    return marks;
}

/**
 * Record the marks into the eye submitted now, and into the right eye too when it shares the image and is drawn at the left eye's Submit.
 *
 * @param acMarks The marks, in drawing order.
 * @param aEye 0 for the left eye, 1 for the right.
 * @param acViewport That eye's part of the eye image.
 * @param acpRightViewport The right eye's part, when it is drawn now too; otherwise null.
 * @return The recorded drawing, to run in one go; null when it could not be recorded, which turns the panel off for the rest of the game.
 */
ID3D11CommandList* RecordMarks(const Marks& acMarks, const int aEye, const D3D11_VIEWPORT& acViewport, const D3D11_VIEWPORT* acpRightViewport) noexcept
{
    for (size_t i = 0; i < acMarks.Count; ++i)
    {
        RecordDraw(acMarks.List[i].Eyes[aEye], acViewport, acMarks.List[i]);
        if (acpRightViewport && !s_drawer.Failed)
            RecordDraw(acMarks.List[i].Eyes[1], *acpRightViewport, acMarks.List[i]);
    }

    ID3D11CommandList* pCommands = nullptr;
    if (s_drawer.Failed || FAILED(s_drawer.pDeferred->FinishCommandList(FALSE, &pCommands)))
    {
        Fail("the panel's drawing could not be recorded");
        return nullptr;
    }
    return pCommands;
}

/**
 * Draw the panel, the pointing over it and the banner layers into the eye image the game submits now, as that eye sees them this frame.
 *
 * @param aEye Which eye the game submits.
 * @param apTarget The eye image.
 * @param apBounds The eye's part of the image, or null for all of it.
 */
void Draw(const vr::EVREye aEye, ID3D11Texture2D* apTarget, const vr::VRTextureBounds_t* apBounds) noexcept
{
    auto scene = TakeScene();
    if (!scene)
        return;

    // Drawn with the left eye already, in the same double-wide image.
    if (aEye == vr::Eye_Right && apTarget == s_bothEyesDrawnInto)
    {
        s_bothEyesDrawnInto = nullptr;
        return;
    }
    s_bothEyesDrawnInto = nullptr;

    // The headset where the game rendered this frame from: the game set its node before drawing, on this thread.
    // Only the panel needs it, being placed in the game's world; the banner layers are placed relative to the headset.
    const std::optional<VRControllers::HeadSpace> cHead = scene->Panel ? VRControllers::HeadSpaceNow() : std::nullopt;

    ID3D11DeviceContext* pImmediate = nullptr;
    s_drawer.pDevice->GetImmediateContext(&pImmediate);
    if (!KeepDrawableParts(*scene, cHead, pImmediate) || !EnsureTargetView(apTarget))
    {
        pImmediate->Release();
        return;
    }

    D3D11_TEXTURE2D_DESC target{};
    apTarget->GetDesc(&target);
    const vr::VRTextureBounds_t cBounds = apBounds ? *apBounds : vr::VRTextureBounds_t{0.f, 0.f, 1.f, 1.f};
    const D3D11_VIEWPORT cViewport = ViewportFor(target, cBounds);
    // A left eye in the left half of the image: the right half is drawn now too.
    // The runtime may take the whole image at the left eye's Submit.
    const bool cBoth = aEye == vr::Eye_Left && std::max(cBounds.uMin, cBounds.uMax) <= 0.5f;
    const D3D11_VIEWPORT cRightViewport = ViewportFor(target, {cBounds.uMin + 0.5f, cBounds.vMin, cBounds.uMax + 0.5f, cBounds.vMax});

    const Marks cMarks = MarksFor(*scene, cHead);
    ID3D11CommandList* pCommands = RecordMarks(cMarks, aEye == vr::Eye_Left ? 0 : 1, cViewport, cBoth ? &cRightViewport : nullptr);
    if (!pCommands)
    {
        pImmediate->Release();
        return;
    }

    const EyeDrawChecks::EyeImage cImage{s_drawer.pDevice, pImmediate, apTarget, target};
    EyeDrawChecks::Checks checks;
    if (aEye == vr::Eye_Left)
        checks = EyeDrawChecks::ChecksFor(cMarks, scene->Eyes, cViewport, s_panelPicture, s_layerPictures);
    EyeDrawChecks::ReadPixelsBefore(checks, cImage);

    // Run with the game's own state put back afterwards.
    pImmediate->ExecuteCommandList(pCommands, TRUE);
    pCommands->Release();
    if (cBoth)
        s_bothEyesDrawnInto = apTarget;

    EyeDrawChecks::FinishChecks(checks, cImage);
    pImmediate->Release();
}

/**
 * Ask the runtime for each eye's projection and place in the head.
 * Asked on the update thread, as the panel or a banner layer starts showing, and kept for the render thread.
 *
 * @return Both eyes' matrices, left then right.
 */
EyeMatrices ReadEyeMatrices() noexcept
{
    EyeMatrices eyes;
    eyes.Projection = {s_drawer.pSystem->GetProjectionMatrix(vr::Eye_Left, kNearClip, kFarClip), s_drawer.pSystem->GetProjectionMatrix(vr::Eye_Right, kNearClip, kFarClip)};
    eyes.EyeToHead = {s_drawer.pSystem->GetEyeToHeadTransform(vr::Eye_Left), s_drawer.pSystem->GetEyeToHeadTransform(vr::Eye_Right)};
    return eyes;
}

/**
 * Stand in for the compositor's Submit: draw the panel and the banner layers into the eye image, then hand it to the runtime as the game asked.
 * Only on the game's main thread, and only Direct3D images; everything is made at the first one.
 *
 * @param apThis The compositor.
 * @param aEye Which eye the game submits.
 * @param apTexture The eye image.
 * @param apBounds The eye's part of the image, or null for all of it.
 * @param aFlags The game's submit options, passed on unchanged.
 * @return What the runtime's own Submit answers.
 */
vr::EVRCompositorError HookSubmit(void* apThis, const vr::EVREye aEye, const vr::Texture_t* apTexture, const vr::VRTextureBounds_t* apBounds, const vr::EVRSubmitFlags aFlags)
{
    if (apTexture && apTexture->eType == vr::TextureType_DirectX && apTexture->handle && GetCurrentThreadId() == s_mainThread && !s_drawer.Failed)
    {
        auto* pTarget = static_cast<ID3D11Texture2D*>(apTexture->handle);
        if (s_drawer.Made || MakeDrawer(pTarget))
            Draw(aEye, pTarget, apBounds);
    }
    return RealSubmit(apThis, aEye, apTexture, apBounds, aFlags);
}

/**
 * Stand in for the game's VR_GetGenericInterface: when it asks for the compositor, put HookSubmit in the compositor's Submit slot.
 * The slot is taken once, on the first compositor the game asks for.
 *
 * @param apVersion The interface the game asks for, such as "IVRCompositor_027".
 * @param apError Set by the runtime when the interface cannot be given.
 * @return The runtime's interface, unchanged.
 */
void* __cdecl HookVR_GetGenericInterface(const char* apVersion, vr::EVRInitError* apError)
{
    void* pInterface = RealVR_GetGenericInterface(apVersion, apError);
    if (!pInterface || !apVersion || std::strncmp(apVersion, kCompositorPrefix, sizeof(kCompositorPrefix) - 1) != 0)
        return pInterface;

    // The game has its compositor, so VR is up and the runtime is known: its log line is written now, for crash reports from here on.
    VRRuntime::IsOpenComposite();

    void** pTable = *static_cast<void***>(pInterface);
    if (pTable[kSubmitSlot] == reinterpret_cast<void*>(&HookSubmit) || s_hooked.exchange(true))
        return pInterface;
    DWORD oldProtection = 0;
    if (VirtualProtect(&pTable[kSubmitSlot], sizeof(void*), PAGE_READWRITE, &oldProtection))
    {
        RealSubmit = reinterpret_cast<TSubmit>(pTable[kSubmitSlot]);
        pTable[kSubmitSlot] = reinterpret_cast<void*>(&HookSubmit);
        VirtualProtect(&pTable[kSubmitSlot], sizeof(void*), oldProtection, &oldProtection);
        spdlog::info("VREyeDraw: on the compositor's Submit ({})", apVersion);
    }
    else
        spdlog::error("VREyeDraw: the compositor's Submit could not be hooked; no panel inside the game, and on OpenComposite no banners either");
    return pInterface;
}

// At load, on the game's main thread: the game's own import of VR_GetGenericInterface goes through HookVR_GetGenericInterface.
TiltedPhoques::Initializer s_eyeDrawHook(
    []()
    {
        s_mainThread = GetCurrentThreadId();
        TP_HOOK_IAT(VR_GetGenericInterface, "openvr_api.dll");
        if (!RealVR_GetGenericInterface)
            spdlog::error("VREyeDraw: the game's import of VR_GetGenericInterface was not found; no panel inside the game, and on OpenComposite no banners either");
    });
} // namespace

namespace VREyeDraw
{
/**
 * Check if the eye images can be drawn into: everything the panel and the banner's layers need was made on the game's device at its first frame, and nothing failed since.
 * After a failure neither shows for the rest of the game: no panel, and on OpenComposite no banners.
 *
 * @return True once the panel and the banner's layers can be shown.
 */
bool IsReady() noexcept
{
    return s_drawer.Made && !s_drawer.Failed;
}

/**
 * Give the picture to draw on the panel, from the next frame on.
 * It is taken by swapping, so the caller gets an older buffer back to fill next time.
 *
 * @param aPixels The whole picture, BGRA premultiplied as the browser paints it; ignored when its size does not match the width and height.
 * @param aWidth Its width in pixels.
 * @param aHeight Its height in pixels.
 */
void SetPicture(std::vector<uint8_t>& aPixels, const uint32_t aWidth, const uint32_t aHeight) noexcept
{
    std::scoped_lock lock(s_lock);
    s_panelPicture.Give(aPixels, aWidth, aHeight);
}

/**
 * Show the panel at a place in the game's world, from the next frame on, until it is hidden.
 *
 * @param acQuad The panel's corners in the game's world.
 */
void Show(const Quad& acQuad) noexcept
{
    if (!IsReady())
        return;
    const EyeMatrices cEyes = ReadEyeMatrices();
    std::scoped_lock lock(s_lock);
    s_eyes = cEyes;
    s_panel = acQuad;
}

/**
 * Stop drawing the panel and the pointing over it; the banner's layers are not touched.
 */
void Hide() noexcept
{
    std::scoped_lock lock(s_lock);
    s_panel.reset();
    s_pointing = {};
}

/**
 * Give what pointing draws over the panel: the beam, the dot and the hand rings.
 *
 * @param acPointing What to draw; an empty one draws none of them.
 */
void SetPointing(const Pointing& acPointing) noexcept
{
    std::scoped_lock lock(s_lock);
    s_pointing = acPointing;
}

/**
 * Change the picture of one of the banner's layers while it shows, from the next frame on; its place and alpha stay.
 * It is taken by swapping, so the caller gets an older buffer back to fill next time.
 *
 * @param aLayer Which layer.
 * @param aPixels The whole picture, BGRA premultiplied like the panel's, top row first; ignored when its size does not match the width and height.
 * @param aWidth Its width in pixels.
 * @param aHeight Its height in pixels.
 */
void SetLayerPicture(const BannerPlacement::Layer aLayer, std::vector<uint8_t>& aPixels, const uint32_t aWidth, const uint32_t aHeight) noexcept
{
    std::scoped_lock lock(s_lock);
    s_layerPictures[BannerPlacement::IndexOf(aLayer)].Give(aPixels, aWidth, aHeight);
}

/**
 * Show one of the banner's layers from the next frame on, until it is hidden: its picture, its place relative to the headset and its alpha, in one handover.
 * A frame draws all of the old or all of the new, never a new picture at an old place or alpha.
 *
 * @param aLayer Which layer.
 * @param aPixels A new picture, BGRA premultiplied, top row first, taken by swapping; empty to keep the picture the layer has.
 * @param aWidth The new picture's width in pixels.
 * @param aHeight Its height in pixels.
 * @param acCorners The layer's corners in the headset's space, in metres, from the top left, clockwise as the player faces it.
 * @param aAlpha How much of it shows, from 0 to 1.
 */
void ShowLayer(const BannerPlacement::Layer aLayer, std::vector<uint8_t>& aPixels, const uint32_t aWidth, const uint32_t aHeight, const EyeGeometry::HeadCorners& acCorners,
               const float aAlpha) noexcept
{
    if (!IsReady())
        return;
    const EyeMatrices cEyes = ReadEyeMatrices();
    const size_t cIndex = BannerPlacement::IndexOf(aLayer);
    std::scoped_lock lock(s_lock);
    if (!aPixels.empty())
        s_layerPictures[cIndex].Give(aPixels, aWidth, aHeight);
    s_layerCorners[cIndex] = acCorners;
    s_layerAlpha[cIndex] = aAlpha;
    s_eyes = cEyes;
}

/**
 * Set how much of one of the banner's layers shows, for its fades; kept while it is hidden.
 *
 * @param aLayer Which layer.
 * @param aAlpha From 0, nothing, to 1, all of it.
 */
void SetLayerAlpha(const BannerPlacement::Layer aLayer, const float aAlpha) noexcept
{
    std::scoped_lock lock(s_lock);
    s_layerAlpha[BannerPlacement::IndexOf(aLayer)] = aAlpha;
}

/**
 * Stop drawing one of the banner's layers; its picture and alpha are kept for its next showing.
 *
 * @param aLayer Which layer.
 */
void HideLayer(const BannerPlacement::Layer aLayer) noexcept
{
    std::scoped_lock lock(s_lock);
    s_layerCorners[BannerPlacement::IndexOf(aLayer)].reset();
}
} // namespace VREyeDraw

#endif
