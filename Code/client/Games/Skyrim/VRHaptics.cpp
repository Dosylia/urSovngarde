#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRHaptics.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>

#include <chrono>

namespace
{
using namespace std::chrono_literals;

// The game has already brought OpenVR up through its own openvr_api.dll, and asking that DLL shares the game's session.
// A second copy of the DLL would not be connected to SteamVR.
vr::IVRSystem* GetVRSystem() noexcept
{
    static vr::IVRSystem* s_pSystem = nullptr;
    static bool s_tried = false;

    if (s_pSystem)
        return s_pSystem;

    // OpenVR can come up after the first frames, so a failure is retried for a minute before giving up.
    static std::chrono::steady_clock::time_point s_giveUpAt{};
    const auto now = std::chrono::steady_clock::now();
    if (!s_tried)
    {
        s_tried = true;
        s_giveUpAt = now + 60s;
    }
    if (now >= s_giveUpAt)
        return nullptr;

    HMODULE hOpenVR = GetModuleHandleW(L"openvr_api.dll");
    if (!hOpenVR)
        return nullptr;

    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    auto* pGetInterface = reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface"));
    if (!pGetInterface)
        return nullptr;

    vr::EVRInitError error = vr::VRInitError_None;
    s_pSystem = static_cast<vr::IVRSystem*>(pGetInterface(vr::IVRSystem_Version, &error));
    return s_pSystem;
}
} // namespace

namespace VRHaptics
{
bool Pulse(const bool aRightHand, uint16_t aDurationMicroSec) noexcept
{
    vr::IVRSystem* pSystem = GetVRSystem();
    if (!pSystem)
        return false;

    const vr::TrackedDeviceIndex_t device =
        pSystem->GetTrackedDeviceIndexForControllerRole(aRightHand ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand);
    if (device == vr::k_unTrackedDeviceIndexInvalid)
        return false;

    // OpenVR caps a single call at just under 4 ms and silently truncates anything longer, so it is clamped here.
    if (aDurationMicroSec > 3999)
        aDurationMicroSec = 3999;

    pSystem->TriggerHapticPulse(device, 0, aDurationMicroSec);
    return true;
}

Grips GripsHeld() noexcept
{
    // The grip button, or Axis2, which Oculus Touch controllers report for the grip.
    constexpr uint64_t cGripMask = (1ull << vr::k_EButton_Grip) | (1ull << vr::k_EButton_Axis2);

    Grips grips{};

    if (vr::IVRSystem* pSystem = GetVRSystem())
    {
        for (const auto cRole : {vr::TrackedControllerRole_LeftHand, vr::TrackedControllerRole_RightHand})
        {
            const vr::TrackedDeviceIndex_t cDevice = pSystem->GetTrackedDeviceIndexForControllerRole(cRole);
            vr::VRControllerState_t state{};
            if (cDevice != vr::k_unTrackedDeviceIndexInvalid && pSystem->GetControllerState(cDevice, &state, sizeof(state)) && (state.ulButtonPressed & cGripMask) != 0)
                ++grips.OpenVR;
        }
    }

    // BSInputDeviceManager keeps its devices in a list at 0x60; slots 3 to 8 are the VR controllers, two per headset type.
    // Only a BSOpenVRControllerDevice (known by its vtable) is read: its last poll's state sits at 0xC0.
    POINTER_SKYRIMSE(const uint8_t*, s_inputDeviceManager, 516574, 516574);
    static const uintptr_t s_controllerVtable = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x17E87C0;
    if (const uint8_t* pManager = *s_inputDeviceManager.Get())
    {
        for (int slot = 3; slot <= 8; ++slot)
        {
            const uint8_t* pDevice = *reinterpret_cast<const uint8_t* const*>(pManager + 0x60 + slot * sizeof(void*));
            if (!pDevice || *reinterpret_cast<const uintptr_t*>(pDevice) != s_controllerVtable)
                continue;
            const auto& cState = *reinterpret_cast<const vr::VRControllerState_t*>(pDevice + 0xC0);
            if ((cState.ulButtonPressed & cGripMask) != 0)
                ++grips.Game;
        }
    }

    return grips;
}

} // namespace VRHaptics

#else

namespace VRHaptics
{
bool Pulse(bool, uint16_t) noexcept
{
    return false;
}

Grips GripsHeld() noexcept
{
    return {};
}
} // namespace VRHaptics

#endif
