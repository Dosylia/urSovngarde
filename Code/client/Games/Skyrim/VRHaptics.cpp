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

bool GripHeld(const bool aRightHand) noexcept
{
    vr::IVRSystem* pSystem = GetVRSystem();
    if (!pSystem)
        return false;

    const vr::TrackedDeviceIndex_t device =
        pSystem->GetTrackedDeviceIndexForControllerRole(aRightHand ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand);
    if (device == vr::k_unTrackedDeviceIndexInvalid)
        return false;

    vr::VRControllerState_t state{};
    if (!pSystem->GetControllerState(device, &state, sizeof(state)))
        return false;
    return (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) != 0;
}

} // namespace VRHaptics

#else

namespace VRHaptics
{
bool Pulse(bool, uint16_t) noexcept
{
    return false;
}

bool GripHeld(bool) noexcept
{
    return false;
}
} // namespace VRHaptics

#endif
