#include <TiltedOnlinePCH.h>

#include <Camera/CameraView.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <FiniteVector.h>

namespace CameraView
{
bool ShowsBody(const NiPoint3& acPoint, const float aMargin) noexcept
{
    // The same projection the name tags use: x and y from 0 to 1 across the screen, z below 0 behind the camera.
    // A point the game does not project (behind the camera) is not shown.
    NiPoint3 screen{};
    if (!HUDMenuUtils::WorldPtToScreenPt3(acPoint, screen))
        return false;
    if (!IsFiniteVector(screen) || screen.z < 0.f)
        return false;
    return screen.x > -aMargin && screen.x < 1.f + aMargin && screen.y > -aMargin && screen.y < 1.f + aMargin;
}
} // namespace CameraView
