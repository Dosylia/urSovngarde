#include <BSGraphics/BSGraphicsRenderer.h>
#include <PatchCheck.h>

struct BSInputDeviceManager;

void (*BSInputDeviceManager_PollInputDevices)(BSInputDeviceManager*, float) = nullptr;

void Hook_BSInputDeviceManager_PollInputDevices(BSInputDeviceManager* inputDeviceMgr, float afDelta)
{
    // Null on VR, where Hook_Renderer_Init is off.
    auto* pWindow = BSGraphics::GetMainWindow();
    if (pWindow && !pWindow->IsForeground())
        return;

    BSInputDeviceManager_PollInputDevices(inputDeviceMgr, afDelta);
}

// Off on VR on purpose, though the address is known (SE 67315, VR 0xc519e0).
// The mirror window is rarely focused in a headset, so the focus check would stop the controllers.
#ifndef SKYRIMVR
// Hooked at the first update, once SKSE plugins have hooked the poll at input load.
// A plugin that checks the poll's first bytes before hooking it (DevBench's keyboard) refuses ours, so ours goes on top of theirs.
// Their injected keys then reach the game only while its window is in front, as real keys do.
void InstallInputFocusCheck() noexcept
{
    static bool s_tried = false;
    if (s_tried)
        return;
    s_tried = true;

    const VersionDbPtr<uint8_t> cPoll(68617);
    uint8_t* pPoll = cPoll.Get();
    const bool cOwn = PatchCheck::Is(pPoll, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18});
    // Another plugin's hook: a relative jump (E9) or an indirect one (FF 25), which MinHook carries into its trampoline.
    const bool cHooked = PatchCheck::Is(pPoll, {0xE9}) || PatchCheck::Is(pPoll, {0xFF, 0x25});
    if (!cOwn && !cHooked)
    {
        spdlog::error("Input: the device poll at {} is not the expected code ({}); keys are read with the window in the background too", fmt::ptr(pPoll), PatchCheck::Bytes(pPoll, 10));
        return;
    }

    // Read before ours goes on: afterwards the first bytes are our own jump.
    const std::string cOtherHook = cHooked ? fmt::format(" (on top of another plugin's hook: {})", PatchCheck::Bytes(pPoll, 6)) : std::string();
    uint8_t firstBytes[5]{};
    std::memcpy(firstBytes, pPoll, sizeof(firstBytes));
    BSInputDeviceManager_PollInputDevices = reinterpret_cast<decltype(BSInputDeviceManager_PollInputDevices)>(pPoll);
    TP_HOOK_IMMEDIATE(&BSInputDeviceManager_PollInputDevices, &Hook_BSInputDeviceManager_PollInputDevices);
    // MinHook writes a 5-byte jump at the start, so the hook is live only if those bytes changed.
    if (reinterpret_cast<uint8_t*>(BSInputDeviceManager_PollInputDevices) == pPoll || std::memcmp(firstBytes, pPoll, sizeof(firstBytes)) == 0)
    {
        // Our hook is not live: the poll is called directly again.
        BSInputDeviceManager_PollInputDevices = reinterpret_cast<decltype(BSInputDeviceManager_PollInputDevices)>(pPoll);
        spdlog::error("Input: the device poll at {} could not be hooked; keys are read with the window in the background too", fmt::ptr(pPoll));
        return;
    }
    spdlog::info("Input: keys are read only while the game window is in front{}", cOtherHook);
}
#endif
