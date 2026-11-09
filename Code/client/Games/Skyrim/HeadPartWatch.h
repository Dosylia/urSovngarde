#pragma once

#ifdef SKYRIMVR
// TEMPORARY: watches the shader properties of the player's head parts for one freed while a part still holds it.
namespace HeadPartWatch
{
//! Once a frame, on the main thread, from the main loop.
void Update() noexcept;
} // namespace HeadPartWatch
#endif
