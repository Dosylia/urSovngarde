
#include "Services/InputService.h"
#include "Systems/RenderSystemD3D11.h"

#include "World.h"

#include "BSGraphics/BSGraphicsRenderer.h"
#include "BSRandom/BSRandom.h"

#ifdef SKYRIMVR
#include <Games/Skyrim/VRBodySync.h>
#include <PatchCheck.h>
#endif

// shared resource by launcher
extern HICON g_SharedWindowIcon;

namespace BSGraphics
{
namespace
{

static RenderSystemD3D11* g_sRs = nullptr;
static WNDPROC RealWndProc = nullptr;
static RendererWindow* g_RenderWindow = nullptr;

static constexpr char kTogetherWindowName[]{"Skyrim Together"};

} // namespace
RendererWindow* GetMainWindow()
{
    return g_RenderWindow;
}

bool RendererWindow::IsForeground()
{
    return GetForegroundWindow() == hWnd;
}

void (*Renderer_Init)(Renderer*, BSGraphics::RendererInitOSData*, const BSGraphics::ApplicationWindowProperties*, BSGraphics::RendererInitReturn*) = nullptr;

// WNDPROC seems to be part of the renderer
LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (InputService::WndProc(hwnd, uMsg, wParam, lParam) != 0)
        return 0;

    return RealWndProc(hwnd, uMsg, wParam, lParam);
}

void Hook_Renderer_Init(Renderer* self, BSGraphics::RendererInitOSData* aOSData, const BSGraphics::ApplicationWindowProperties* aFBData, BSGraphics::RendererInitReturn* aOut)
{
    // we feed this a shared icon as the resource directory of our former launcher data is already overwritten with the
    // game.
    aOSData->hIcon = g_SharedWindowIcon;
    // Append our window name.
    aOSData->pClassName = kTogetherWindowName;

    RealWndProc = aOSData->pWndProc;
    aOSData->pWndProc = Hook_WndProc;

    Renderer_Init(self, aOSData, aFBData, aOut);

    g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();
    // This how the game does it too
    g_RenderWindow = &self->Data.RenderWindowA[0];

    const BSGraphics::RendererData& renderer = self->Data;

    g_sRs->OnDeviceCreation(renderer.RenderWindowA[0].pSwapChain, renderer.pForwarder, renderer.pContext);
}

void (*StopTimer)(int) = nullptr;

// Insert us at the End
void Hook_StopTimer(int type)
{
    if (g_sRs)
        g_sRs->OnRender();

#ifdef SKYRIMVR
    VRBodySync::OnFrameEnd();
#endif

    StopTimer(type);
}

#ifdef SKYRIMVR
// The game quits at start when FindWindowA finds a window of its own class, and brings that window to the front.
// The two-game test (Tools/VR/two-games.py) runs a second game on one PC, so that call finds none while its marker file exists.
static TiltedPhoques::Initializer s_secondGameInTest(
    []()
    {
        const char* cLocalAppData = std::getenv("LOCALAPPDATA");
        if (!cLocalAppData || !std::filesystem::exists(std::filesystem::path(cLocalAppData) / "SkyrimTogetherVR" / "two-games.on"))
            return;

        auto* pCall = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x5B5969;
        if (!PatchCheck::Is(pCall, {0xFF, 0x15, 0x21, 0x9E, 0xFC, 0x00, 0x48, 0x85, 0xC0}))
        {
            spdlog::error("Two games: the game's check for a running game is not the expected code ({}); a second game cannot start", PatchCheck::Bytes(pCall, 9));
            return;
        }
        TiltedPhoques::Put<uint16_t>(pCall, 0xC031);
        TiltedPhoques::Put<uint32_t>(pCall + 2, 0x90909090);
        spdlog::info("Two games: a second game may start on this PC");
    });
#endif

static TiltedPhoques::Initializer s_viewportHooks(
    []()
    {
        // Off on VR: these addresses are unverified there, and a wrong Renderer_Init hook silently corrupts memory.
        // Without it there is no window patch and no overlay/imgui device hookup.
#ifndef SKYRIMVR
        const VersionDbPtr<void> initWindowLoc(77226);
        // patch dwStyle in BSGraphics::InitWindows
        TiltedPhoques::Put(mem::pointer(initWindowLoc.GetPtr()) + 0x174 + 1, WS_OVERLAPPEDWINDOW);

        const VersionDbPtr<void> windowLoc(68781);
        // TODO: move me to input patches.
        // don't let the game steal the media keys in windowed mode
        TiltedPhoques::Put(
            mem::pointer(windowLoc.GetPtr()) + 0x55 + 2,
            /*strip DISCL_EXCLUSIVE bits and append DISCL_NONEXCLUSIVE*/ 3);

        const VersionDbPtr<void> timerLoc(77246);
        const VersionDbPtr<void> renderInit(77226);

        TiltedPhoques::SwapCall(mem::pointer(timerLoc.GetPtr()) + 9, StopTimer, &Hook_StopTimer);

        Renderer_Init = static_cast<decltype(Renderer_Init)>(renderInit.GetPtr());

        // Once we find a proper way to locate it for different versions, go back to swapcall
        // TiltedPhoques::SwapCall(mem::pointer(initLoc.GetPtr()) + 0xD1A, Renderer_Init, &Hook_Renderer_Init);
        TP_HOOK_IMMEDIATE(&Renderer_Init, &Hook_Renderer_Init);
#else
        // Only the frame end call on VR, for VRBodySync: SE 75461 is the renderer's frame end, and +0x15 is its StopTimer(1) call.
        // Another plugin already swaps this call on VR, so ours chains to it.
        const VersionDbPtr<void> timerLoc(75461);
        uint8_t* pStopTimer = PatchCheck::At(timerLoc.GetPtr(), 0x15);
        if (PatchCheck::IsCall(pStopTimer))
            TiltedPhoques::SwapCall(pStopTimer, StopTimer, &Hook_StopTimer);
        else
            spdlog::error("Renderer: {} is not the frame end's call to StopTimer ({}), leaving it alone; VR body sync does not run",
                          fmt::ptr(pStopTimer), PatchCheck::Bytes(pStopTimer, 5));
#endif
    });
} // namespace BSGraphics
