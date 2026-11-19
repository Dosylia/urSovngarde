#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <Systems/VREyeDraw.h>
#include <Systems/VRPage.h>

#include <Games/Skyrim/VRRuntime.h>
#include <Games/Skyrim/VRControllers.h>
#include <SafeRelease.h>

#include <openvr/openvr.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

#pragma comment(lib, "d3dcompiler")

namespace
{
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
)";

// The shaders in kShaderCode: the vertex shader every mark shares, and the pixel shader that colours each kind of mark.
enum Shader : size_t
{
    kVertexShader,
    kPanelShader,
    kBeamShader,
    kDotShader,
    kRingShader,
    kShaderCount,
};
// Each shader's function in kShaderCode, in the order of Shader.
constexpr const char* kShaderEntries[kShaderCount]{"VsMain", "PsPanel", "PsBeam", "PsDot", "PsRing"};

// In metres: half the dot's width, half the beam's, half the hand ring's.
constexpr float kDotHalf = 0.008f;
constexpr float kBeamHalf = 0.002f;
constexpr float kRingHalf = 0.02f;
// The rings' colour, red, green, blue.
constexpr uint8_t kPaleRgb[3]{235, 235, 235};
// In metres: the nearest and the farthest the eyes' projections draw.
constexpr float kNearClip = 0.05f;
constexpr float kFarClip = 100.f;

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
// A mark's four corners in one eye, as a triangle strip.
using EyeVertices = std::array<Vertex, 4>;
// Four corners in the headset's space, from the top left, clockwise as the player faces them.
using HeadCorners = std::array<glm::vec3, 4>;

/**
 * Everything the panel is drawn with, all on the game's device, made at its first frame submitted from the main thread.
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
    ID3D11InputLayout* pLayout = nullptr;
    ID3D11Buffer* pVertices = nullptr;
    ID3D11SamplerState* pSampler = nullptr;
    ID3D11RasterizerState* pRaster = nullptr;
    // The page's picture, with its smaller levels.
    ID3D11Texture2D* pPicture = nullptr;
    ID3D11ShaderResourceView* pPictureView = nullptr;
    uint32_t PictureWidth = 0;
    uint32_t PictureHeight = 0;
    // The eye image last drawn into, and the view to draw into it.
    ID3D11Texture2D* pTarget = nullptr;
    ID3D11RenderTargetView* pTargetView = nullptr;
    std::atomic<bool> Made{false};
    std::atomic<bool> Failed{false};
};
Drawer s_drawer;

std::mutex s_lock;
std::vector<uint8_t> s_pixels;
// The picture taken over by the render thread, uploaded there without holding the lock.
std::vector<uint8_t> s_renderPixels;
uint32_t s_width = 0;
uint32_t s_height = 0;
bool s_pictureChanged = false;
// The panel in the game's world, while it is shown, and what pointing draws over it.
std::optional<VREyeDraw::Quad> s_panel;
VREyeDraw::Pointing s_pointing;
// Each eye's projection and place in the head.
std::array<vr::HmdMatrix44_t, 2> s_projection{};
std::array<vr::HmdMatrix34_t, 2> s_eyeToHead{};
// The double-wide eye image the right eye was already drawn into with the left, at the left eye's Submit.
ID3D11Texture2D* s_bothEyesDrawnInto = nullptr;
// One check per game that each kind of mark lands in the eye image; it waits for the GPU, so not at every showing.
// A sample can fall under another mark, so a check tries again on later frames until its colour shows, a few times at most.
constexpr int kCheckTries = 20;
// How far each colour of a checked pixel may be from the expected one, for the eye image's rounding.
constexpr int kColourTolerance = 12;
int s_panelTries = 0;
int s_beamTries = 0;
int s_ringTries = 0;

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
 * Give up on the panel for the rest of the game, and log why the first time.
 *
 * @param acpWhat What went wrong, for the log.
 */
void Fail(const char* acpWhat) noexcept
{
    if (!s_drawer.Failed.exchange(true))
        spdlog::error("VREyeDraw: {}; no panel inside the game this session", acpWhat);
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
    return cColour("Panel", VRPage::kPanelRgb) + cColour("Gold", VRPage::kGoldRgb) + cColour("Pale", kPaleRgb) + kShaderCode;
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
 * Make the objects the panel is drawn with on the game's device: shaders, the corners' layout and buffer, the sampler, the rasterizer.
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
    return made;
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
 * Make the picture's texture on the game's device, at a new size.
 *
 * @param aWidth The picture's width in pixels.
 * @param aHeight Its height in pixels.
 * @return True when it was made; otherwise the panel is off for the rest of the game.
 */
bool MakePictureTexture(const uint32_t aWidth, const uint32_t aHeight) noexcept
{
    SafeRelease(s_drawer.pPictureView);
    SafeRelease(s_drawer.pPicture);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = aWidth;
    desc.Height = aHeight;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(s_drawer.pDevice->CreateTexture2D(&desc, nullptr, &s_drawer.pPicture)) ||
        FAILED(s_drawer.pDevice->CreateShaderResourceView(s_drawer.pPicture, nullptr, &s_drawer.pPictureView)))
    {
        Fail("the panel's picture could not be made on the game's device");
        return false;
    }
    s_drawer.PictureWidth = aWidth;
    s_drawer.PictureHeight = aHeight;
    return true;
}

/**
 * Put the newest picture on the game's device, with its smaller levels: the page lands on far fewer eye pixels than it has, and thin text would shimmer.
 *
 * @param apImmediate The game's device context, which runs the upload.
 * @return True when there is a picture to draw.
 */
bool UploadPicture(ID3D11DeviceContext* apImmediate) noexcept
{
    uint32_t width = 0;
    uint32_t height = 0;
    bool changed = false;
    {
        // A new picture is taken over by swapping, so the lock is held only for that.
        std::scoped_lock lock(s_lock);
        if (s_pictureChanged)
        {
            s_renderPixels.swap(s_pixels);
            s_pictureChanged = false;
            changed = true;
        }
        width = s_width;
        height = s_height;
    }
    if (s_renderPixels.empty())
        return false;
    if (!s_drawer.pPicture || s_drawer.PictureWidth != width || s_drawer.PictureHeight != height)
    {
        if (!MakePictureTexture(width, height))
            return false;
        changed = true;
    }
    if (changed)
    {
        apImmediate->UpdateSubresource(s_drawer.pPicture, 0, nullptr, s_renderPixels.data(), width * 4, 0);
        apImmediate->GenerateMips(s_drawer.pPictureView);
    }
    return true;
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
 * Turn a point seen from the headset into where one eye's projection puts it (its clip space).
 * clip = projection * (eye from head) * head point; OpenVR's matrices multiply column vectors.
 *
 * @param acProjection The eye's projection.
 * @param acEyeToHead Where the eye is in the head.
 * @param acHead The point relative to the headset, in metres.
 * @return The point in the eye's clip space: x, y, z, w.
 */
std::array<float, 4> ToClip(const vr::HmdMatrix44_t& acProjection, const vr::HmdMatrix34_t& acEyeToHead, const glm::vec3& acHead) noexcept
{
    // The eye-to-head transform is rigid: its inverse is the transposed rotation, applied after taking its translation off.
    float relative[3];
    for (int i = 0; i < 3; ++i)
        relative[i] = acHead[i] - acEyeToHead.m[i][3];
    float eye[4]{0.f, 0.f, 0.f, 1.f};
    for (int i = 0; i < 3; ++i)
        eye[i] = acEyeToHead.m[0][i] * relative[0] + acEyeToHead.m[1][i] * relative[1] + acEyeToHead.m[2][i] * relative[2];
    std::array<float, 4> clip{};
    for (int i = 0; i < 4; ++i)
        clip[i] = acProjection.m[i][0] * eye[0] + acProjection.m[i][1] * eye[1] + acProjection.m[i][2] * eye[2] + acProjection.m[i][3] * eye[3];
    return clip;
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
        const auto cClip = ToClip(acProjection, acEyeToHead, acCorners[kOrder[i]]);
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
 * Read one pixel of the eye image back from the GPU; it waits for the GPU to get there.
 *
 * @param apImmediate The game's device context.
 * @param apTarget The eye image.
 * @param acTarget Its description.
 * @param aX The pixel, across.
 * @param aY The pixel, down.
 * @param aOut Filled with the pixel's four bytes, in the eye image's own order.
 * @return True when it was read; never for a multisampled image.
 */
bool ReadPixel(ID3D11DeviceContext* apImmediate, ID3D11Texture2D* apTarget, const D3D11_TEXTURE2D_DESC& acTarget, const UINT aX, const UINT aY, uint8_t (&aOut)[4]) noexcept
{
    D3D11_TEXTURE2D_DESC staging = acTarget;
    staging.Width = staging.Height = 1;
    staging.MipLevels = staging.ArraySize = 1;
    staging.SampleDesc.Count = 1;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.MiscFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* pStaging = nullptr;
    if (acTarget.SampleDesc.Count != 1 || FAILED(s_drawer.pDevice->CreateTexture2D(&staging, nullptr, &pStaging)))
        return false;
    const D3D11_BOX cBox{aX, aY, 0, aX + 1, aY + 1, 1};
    apImmediate->CopySubresourceRegion(pStaging, 0, 0, 0, 0, apTarget, 0, &cBox);
    D3D11_MAPPED_SUBRESOURCE pixel{};
    const bool cRead = SUCCEEDED(apImmediate->Map(pStaging, 0, D3D11_MAP_READ, 0, &pixel));
    if (cRead)
    {
        std::memcpy(aOut, pixel.pData, 4);
        apImmediate->Unmap(pStaging, 0);
    }
    pStaging->Release();
    return cRead;
}

/**
 * Find the eye image pixel a point in an eye's clip space lands on.
 *
 * @param acClip The point in the eye's clip space.
 * @param acViewport The eye's part of the eye image.
 * @return The pixel, across and down, or nothing when the point is behind the eye or outside its part of the image.
 */
std::optional<std::pair<UINT, UINT>> PixelOf(const std::array<float, 4>& acClip, const D3D11_VIEWPORT& acViewport) noexcept
{
    if (!(acClip[3] > 0.f))
        return std::nullopt;
    const float cX = acViewport.TopLeftX + (acClip[0] / acClip[3] * 0.5f + 0.5f) * acViewport.Width;
    const float cY = acViewport.TopLeftY + (0.5f - acClip[1] / acClip[3] * 0.5f) * acViewport.Height;
    if (!(cX >= acViewport.TopLeftX && cX < acViewport.TopLeftX + acViewport.Width && cY >= acViewport.TopLeftY && cY < acViewport.TopLeftY + acViewport.Height))
        return std::nullopt;
    return std::make_pair(static_cast<UINT>(cX), static_cast<UINT>(cY));
}

/**
 * Make a square facing the eyes around a point: the dot and the rings.
 *
 * @param acCentre The square's centre in the headset's space.
 * @param aHalf Half its width, in metres.
 * @return Its corners in the headset's space.
 */
HeadCorners Facing(const glm::vec3& acCentre, const float aHalf) noexcept
{
    const glm::vec3 cView = glm::length2(acCentre) > 1e-8f ? glm::normalize(acCentre) : glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 right = glm::cross(cView, glm::vec3(0.f, 1.f, 0.f));
    right = glm::length2(right) > 1e-8f ? glm::normalize(right) : glm::vec3(1.f, 0.f, 0.f);
    const glm::vec3 cUp = glm::cross(right, cView);
    return {acCentre - right * aHalf + cUp * aHalf, acCentre + right * aHalf + cUp * aHalf, acCentre + right * aHalf - cUp * aHalf, acCentre - right * aHalf - cUp * aHalf};
}

/**
 * Make a thin strip from one point to another, turned to face the eyes: the beam.
 *
 * @param acFrom One end in the headset's space.
 * @param acTo The other end in the headset's space.
 * @param aHalf Half the strip's width, in metres.
 * @return Its corners in the headset's space, or nothing when the beam points straight at the eyes or has no length.
 */
std::optional<HeadCorners> Strip(const glm::vec3& acFrom, const glm::vec3& acTo, const float aHalf) noexcept
{
    glm::vec3 side = glm::cross(acTo - acFrom, (acFrom + acTo) * 0.5f);
    if (!(glm::length2(side) > 1e-12f))
        return std::nullopt;
    side = glm::normalize(side) * aHalf;
    return HeadCorners{acFrom + side, acTo + side, acTo - side, acFrom - side};
}

/**
 * Record drawing one mark into an eye's part of the eye image, to run later with the rest.
 *
 * @param acVertices The mark's corners in that eye.
 * @param acViewport The eye's part of the eye image.
 * @param apShader The pixel shader that colours the mark.
 */
void RecordDraw(const EyeVertices& acVertices, const D3D11_VIEWPORT& acViewport, ID3D11PixelShader* apShader) noexcept
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

    const UINT cStride = sizeof(Vertex);
    const UINT cOffset = 0;
    // No blend or depth state: the defaults draw the panel opaque, over everything the game drew, so a menu is never behind a wall.
    pContext->OMSetRenderTargets(1, &s_drawer.pTargetView, nullptr);
    pContext->RSSetViewports(1, &acViewport);
    pContext->RSSetState(s_drawer.pRaster);
    pContext->IASetInputLayout(s_drawer.pLayout);
    pContext->IASetVertexBuffers(0, 1, &s_drawer.pVertices, &cStride, &cOffset);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->VSSetShader(s_drawer.pVertexShader, nullptr, 0);
    pContext->PSSetShader(apShader, nullptr, 0);
    pContext->PSSetShaderResources(0, 1, &s_drawer.pPictureView);
    pContext->PSSetSamplers(0, 1, &s_drawer.pSampler);
    pContext->Draw(4, 0);
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
 * What the update thread last gave to draw, copied for one frame: the panel, the pointing over it, and the eyes to draw them for.
 */
struct Scene
{
    VREyeDraw::Quad Panel;
    VREyeDraw::Pointing Pointing;
    std::array<vr::HmdMatrix44_t, 2> Projection{};
    std::array<vr::HmdMatrix34_t, 2> EyeToHead{};
};

/**
 * Copy what the update thread last gave to draw, under the lock shared with it.
 *
 * @return The scene, or nothing while the panel is hidden.
 */
std::optional<Scene> TakeScene() noexcept
{
    std::scoped_lock lock(s_lock);
    if (!s_panel)
        return std::nullopt;
    return Scene{*s_panel, s_pointing, s_projection, s_eyeToHead};
}

/**
 * One mark in both eyes, with the shader that colours it.
 */
struct Mark
{
    std::array<EyeVertices, 2> Eyes{};
    ID3D11PixelShader* pShader = nullptr;
};

/**
 * Everything to draw this frame, and the points the once-per-game checks sample.
 */
struct Marks
{
    // The panel, then the beam, the dot and the rings over it, in that order: nothing has depth, the last drawn is seen.
    std::array<Mark, 5> List{};
    size_t Count = 0;
    // The beam's middle in the headset's space, when the beam is drawn.
    std::optional<glm::vec3> BeamMiddle;
    // A point on each hand's ring in the headset's space, when that ring is drawn.
    std::array<std::optional<glm::vec3>, 2> RingPoints;
};

/**
 * Work out every mark to draw this frame, in both eyes, from where the game rendered the frame.
 *
 * @param acScene The panel and the pointing over it, in the game's world.
 * @param acHead The headset where the game rendered this frame from.
 * @return The marks in drawing order, with the points the checks sample.
 */
Marks MarksFor(const Scene& acScene, const VRControllers::HeadSpace& acHead) noexcept
{
    Marks marks;
    const auto cAdd = [&](const HeadCorners& acCorners, ID3D11PixelShader* apShader)
    {
        const std::array<EyeVertices, 2> cEyes{VerticesFor(acScene.Projection[0], acScene.EyeToHead[0], acCorners), VerticesFor(acScene.Projection[1], acScene.EyeToHead[1], acCorners)};
        marks.List[marks.Count++] = {cEyes, apShader};
    };

    HeadCorners panel{};
    for (size_t i = 0; i < panel.size(); ++i)
        panel[i] = acHead.Point(acScene.Panel.Corners[i]);
    cAdd(panel, s_drawer.pPanelShader);

    if (acScene.Pointing.Beam)
    {
        const glm::vec3 cFrom = acHead.Point((*acScene.Pointing.Beam)[0]);
        const glm::vec3 cTo = acHead.Point((*acScene.Pointing.Beam)[1]);
        if (const auto cStrip = Strip(cFrom, cTo, kBeamHalf))
        {
            cAdd(*cStrip, s_drawer.pBeamShader);
            marks.BeamMiddle = (cFrom + cTo) * 0.5f;
        }
    }
    if (acScene.Pointing.Dot)
        cAdd(Facing(acHead.Point(*acScene.Pointing.Dot), kDotHalf), s_drawer.pDotShader);
    for (size_t i = 0; i < acScene.Pointing.Hands.size(); ++i)
    {
        if (!acScene.Pointing.Hands[i])
            continue;
        const glm::vec3 cCentre = acHead.Point(*acScene.Pointing.Hands[i]);
        const HeadCorners cRing = Facing(cCentre, kRingHalf);
        cAdd(cRing, s_drawer.pRingShader);
        // A point on the ring itself, which is hollow: most of the way from its centre to its right edge.
        const glm::vec3 cRightEdge = (cRing[1] + cRing[2]) * 0.5f;
        marks.RingPoints[i] = cCentre + (cRightEdge - cCentre) * 0.85f;
    }
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
        RecordDraw(acMarks.List[i].Eyes[aEye], acViewport, acMarks.List[i].pShader);
        if (acpRightViewport && !s_drawer.Failed)
            RecordDraw(acMarks.List[i].Eyes[1], *acpRightViewport, acMarks.List[i].pShader);
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
    // The tries counter of that kind of mark.
    int* pTries = nullptr;
    uint8_t Before[4]{};
    // Whether the pixel could be read before the drawing.
    bool Read = false;
};

/**
 * This frame's checks: at most one for each kind of mark.
 */
struct Checks
{
    std::array<Check, 3> List{};
    size_t Count = 0;
};

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
 * Work out the colour the middle of the picture should have in the eye image: the page's pixel over the dark panel, as the panel's shader lays it.
 *
 * @return Red, green, blue; black before a picture is on the device.
 */
std::array<uint32_t, 3> PictureCentreOverPanel() noexcept
{
    std::array<uint32_t, 3> colour{};
    const size_t cIndex = (static_cast<size_t>(s_drawer.PictureHeight / 2) * s_drawer.PictureWidth + s_drawer.PictureWidth / 2) * 4;
    if (cIndex + 3 < s_renderPixels.size())
    {
        for (int c = 0; c < 3; ++c)
            colour[c] = VRPage::OverPanel(&s_renderPixels[cIndex], c);
    }
    return colour;
}

/**
 * Choose this frame's checks for the left eye: for each kind of mark still unchecked, a pixel where it shows and the colour it should have there.
 *
 * @param acScene The scene drawn this frame, for the left eye's projection.
 * @param acMarks The marks drawn this frame.
 * @param acViewport The left eye's part of the eye image.
 * @return The checks; none for a mark the left eye does not see.
 */
Checks ChecksFor(const Scene& acScene, const Marks& acMarks, const D3D11_VIEWPORT& acViewport) noexcept
{
    Checks checks;
    const auto cAdd = [&](const char* acpWhat, const char* acpColourName, const std::optional<std::pair<UINT, UINT>>& acPixel, const std::array<uint32_t, 3>& acExpected, int& aTries)
    {
        if (!acPixel)
            return;
        Check& check = checks.List[checks.Count++];
        check.pWhat = acpWhat;
        check.pColourName = acpColourName;
        check.Pixel = *acPixel;
        check.Expected = acExpected;
        check.pTries = &aTries;
    };
    const auto cLeftEyePixel = [&](const glm::vec3& acHeadPoint) { return PixelOf(ToClip(acScene.Projection[0], acScene.EyeToHead[0], acHeadPoint), acViewport); };

    if (IsCheckPending(s_panelTries))
        cAdd("panel", "picture centre", PixelOf(ClipCentreOf(acMarks.List[0].Eyes[0]), acViewport), PictureCentreOverPanel(), s_panelTries);
    if (IsCheckPending(s_beamTries) && acMarks.BeamMiddle)
        cAdd("beam", "beam", cLeftEyePixel(*acMarks.BeamMiddle), RgbOf(VRPage::kGoldRgb), s_beamTries);
    // The first ring the left eye sees: a hand held low can be out of view.
    for (const auto& cPoint : acMarks.RingPoints)
    {
        if (!IsCheckPending(s_ringTries) || checks.Count == checks.List.size())
            break;
        if (!cPoint)
            continue;
        const auto cPixel = cLeftEyePixel(*cPoint);
        if (!cPixel)
            continue;
        cAdd("hand ring", "ring", cPixel, RgbOf(kPaleRgb), s_ringTries);
        break;
    }
    return checks;
}

/**
 * Read each check's pixel before the drawing, to log it next to the pixel after.
 *
 * @param aChecks The checks; each one's pixel before is filled in.
 * @param apImmediate The game's device context.
 * @param apTarget The eye image.
 * @param acTarget Its description.
 */
void ReadPixelsBefore(Checks& aChecks, ID3D11DeviceContext* apImmediate, ID3D11Texture2D* apTarget, const D3D11_TEXTURE2D_DESC& acTarget) noexcept
{
    for (size_t i = 0; i < aChecks.Count; ++i)
    {
        Check& check = aChecks.List[i];
        check.Read = ReadPixel(apImmediate, apTarget, acTarget, check.Pixel.first, check.Pixel.second, check.Before);
    }
}

/**
 * Read each check's pixel again after the drawing: the expected colour ends the check, and so does its last try; either way it is logged.
 *
 * @param acChecks The checks, with their pixels before.
 * @param apImmediate The game's device context.
 * @param apTarget The eye image.
 * @param acTarget Its description.
 */
void FinishChecks(const Checks& acChecks, ID3D11DeviceContext* apImmediate, ID3D11Texture2D* apTarget, const D3D11_TEXTURE2D_DESC& acTarget) noexcept
{
    for (size_t i = 0; i < acChecks.Count; ++i)
    {
        const Check& cCheck = acChecks.List[i];
        uint8_t after[4]{};
        if (!cCheck.Read || !ReadPixel(apImmediate, apTarget, acTarget, cCheck.Pixel.first, cCheck.Pixel.second, after))
            continue;
        // Its colour, within what the eye image's rounding allows, ends the check; so does the last try, which is logged as it is.
        bool matches = true;
        for (int c = 0; c < 3; ++c)
            matches = matches && std::abs(static_cast<int>(after[c]) - static_cast<int>(cCheck.Expected[c])) <= kColourTolerance;
        *cCheck.pTries = matches ? kCheckTries : *cCheck.pTries + 1;
        if (!matches && IsCheckPending(*cCheck.pTries))
            continue;
        spdlog::info("VREyeDraw: {} drawn into the left eye at {},{}; pixel before {},{},{}, after {},{},{}, {} {},{},{}", cCheck.pWhat, cCheck.Pixel.first,
                     cCheck.Pixel.second, cCheck.Before[0], cCheck.Before[1], cCheck.Before[2], after[0], after[1], after[2], cCheck.pColourName, cCheck.Expected[0],
                     cCheck.Expected[1], cCheck.Expected[2]);
    }
}

/**
 * Draw the panel and the pointing over it into the eye image the game submits now, as that eye sees them this frame.
 *
 * @param aEye Which eye the game submits.
 * @param apTarget The eye image.
 * @param apBounds The eye's part of the image, or null for all of it.
 */
void Draw(const vr::EVREye aEye, ID3D11Texture2D* apTarget, const vr::VRTextureBounds_t* apBounds) noexcept
{
    const auto cScene = TakeScene();
    if (!cScene)
        return;

    // Drawn with the left eye already, in the same double-wide image.
    if (aEye == vr::Eye_Right && apTarget == s_bothEyesDrawnInto)
    {
        s_bothEyesDrawnInto = nullptr;
        return;
    }
    s_bothEyesDrawnInto = nullptr;

    // The headset where the game rendered this frame from: the game set its node before drawing, on this thread.
    const auto cHead = VRControllers::HeadSpaceNow();
    if (!cHead)
        return;

    ID3D11DeviceContext* pImmediate = nullptr;
    s_drawer.pDevice->GetImmediateContext(&pImmediate);
    if (!UploadPicture(pImmediate) || !EnsureTargetView(apTarget))
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

    const Marks cMarks = MarksFor(*cScene, *cHead);
    ID3D11CommandList* pCommands = RecordMarks(cMarks, aEye == vr::Eye_Left ? 0 : 1, cViewport, cBoth ? &cRightViewport : nullptr);
    if (!pCommands)
    {
        pImmediate->Release();
        return;
    }

    Checks checks;
    if (aEye == vr::Eye_Left)
        checks = ChecksFor(*cScene, cMarks, cViewport);
    ReadPixelsBefore(checks, pImmediate, apTarget, target);

    // Run with the game's own state put back afterwards.
    pImmediate->ExecuteCommandList(pCommands, TRUE);
    pCommands->Release();
    if (cBoth)
        s_bothEyesDrawnInto = apTarget;

    FinishChecks(checks, pImmediate, apTarget, target);
    pImmediate->Release();
}

/**
 * Stand in for the compositor's Submit: draw the panel into the eye image, then hand it to the runtime as the game asked.
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
        spdlog::error("VREyeDraw: the compositor's Submit could not be hooked; no panel inside the game");
    return pInterface;
}

// At load, on the game's main thread: the game's own import of VR_GetGenericInterface goes through HookVR_GetGenericInterface.
TiltedPhoques::Initializer s_eyeDrawHook(
    []()
    {
        s_mainThread = GetCurrentThreadId();
        TP_HOOK_IAT(VR_GetGenericInterface, "openvr_api.dll");
        if (!RealVR_GetGenericInterface)
            spdlog::error("VREyeDraw: the game's import of VR_GetGenericInterface was not found; no panel inside the game");
    });
} // namespace

namespace VREyeDraw
{
/**
 * Check if the panel can be drawn: everything it needs was made on the game's device at its first frame, and nothing failed since.
 *
 * @return True once the panel can be shown.
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
    if (aPixels.size() != static_cast<size_t>(aWidth) * aHeight * 4)
        return;
    std::scoped_lock lock(s_lock);
    s_pixels.swap(aPixels);
    s_width = aWidth;
    s_height = aHeight;
    s_pictureChanged = true;
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
    // The eyes' projections and places in the head are asked of the runtime here, on the update thread, and kept for the render thread.
    const std::array<vr::HmdMatrix44_t, 2> cProjection{s_drawer.pSystem->GetProjectionMatrix(vr::Eye_Left, kNearClip, kFarClip),
                                                       s_drawer.pSystem->GetProjectionMatrix(vr::Eye_Right, kNearClip, kFarClip)};
    const std::array<vr::HmdMatrix34_t, 2> cEyeToHead{s_drawer.pSystem->GetEyeToHeadTransform(vr::Eye_Left), s_drawer.pSystem->GetEyeToHeadTransform(vr::Eye_Right)};
    std::scoped_lock lock(s_lock);
    s_projection = cProjection;
    s_eyeToHead = cEyeToHead;
    s_panel = acQuad;
}

/**
 * Stop drawing the panel and everything over it.
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
} // namespace VREyeDraw

#endif
