#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRHaptics.h>
#include <Games/Skyrim/VRRuntime.h>
#include <Games/Skyrim/VRControllers.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>

#include <chrono>

namespace
{
using namespace std::chrono_literals;

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

    s_pSystem = VRRuntime::GetOpenVRSystem();
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
    Grips grips{};

    if (vr::IVRSystem* pSystem = GetVRSystem())
    {
        for (const auto cRole : {vr::TrackedControllerRole_LeftHand, vr::TrackedControllerRole_RightHand})
        {
            const vr::TrackedDeviceIndex_t cDevice = pSystem->GetTrackedDeviceIndexForControllerRole(cRole);
            vr::VRControllerState_t state{};
            if (cDevice != vr::k_unTrackedDeviceIndexInvalid && pSystem->GetControllerState(cDevice, &state, sizeof(state)) && VRControllers::IsGripPressed(state.ulButtonPressed))
                ++grips.OpenVR;
        }
    }

    grips.Game = VRControllers::GripsHeld();

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
