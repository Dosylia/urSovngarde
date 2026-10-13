#include <Havok/bhkCharacterController.h>

bool bhkCharacterController::UpdateStepTiming(float aMovementDeltaTime) noexcept
{
    // The same physics timestep the native rigid-body controller movement path uses.
    // VR 0x141ec8278, SE 0x141e083a8 (SE id 512261), in VRAddressOverrides under AE id 389089.
    //
    // If it fails to resolve, zero is passed and UpdateDeltaTime falls back to the movement delta, then to the stored step.
    POINTER_SKYRIMSE(float, s_physicsDeltaTime, 389089, 389089);
    const float* pPhysicsDeltaTime = s_physicsDeltaTime.Get();
    return stepInfo.UpdateDeltaTime(pPhysicsDeltaTime ? *pPhysicsDeltaTime : 0.f, aMovementDeltaTime);
}
