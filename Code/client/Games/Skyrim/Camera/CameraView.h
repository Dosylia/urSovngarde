#pragma once

struct NiPoint3;

// What the game's own camera shows on a flat screen.
namespace CameraView
{
// Whether a body around this point shows on screen, the point projected through the game's camera.
// aMargin is the share of the screen added around its edges, for the parts of the body away from the point.
// A point behind the camera is not shown.
bool ShowsBody(const NiPoint3& acPoint, float aMargin) noexcept;
} // namespace CameraView
