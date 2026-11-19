#pragma once

namespace vr
{
class IVRSystem;
class IVROverlay;
} // namespace vr

/**
 * The VR runtime the game talks to through its own openvr_api.dll: SteamVR, or OpenComposite in its place.
 * Every OpenVR part the client uses is fetched here, so what OpenComposite lacks is decided in one place.
 */
namespace VRRuntime
{
/**
 * Ask OpenVR for one of its parts by name, such as the system (headset and controllers) or the overlays (SteamVR's menu panels).
 * It asks the game's own openvr_api.dll, so the part belongs to the game's VR session; another copy of the DLL would not be connected to it.
 *
 * @param acpVersion The part's versioned name, such as vr::IVRSystem_Version.
 * @param apError When given, set to OpenVR's error code.
 * @return The part, or null before the game has started VR or when the runtime does not offer that part.
 */
void* GetOpenVRInterface(const char* acpVersion, int* apError = nullptr) noexcept;

/**
 * Get OpenVR's system part: the headset and controllers as the runtime sees them, with the display and the controllers' buzz.
 *
 * @return The system part, or null before the game has started VR.
 */
vr::IVRSystem* GetOpenVRSystem() noexcept;

/**
 * Get SteamVR's overlays: the panels drawn over the game, which the SteamVR menu tab and the banners use.
 * Only on SteamVR: OpenComposite has no SteamVR menu, and giving one of its overlays a picture closes the game.
 *
 * @return The overlay part, or null unless the runtime is known to be SteamVR.
 */
vr::IVROverlay* GetOpenVROverlay() noexcept;

/**
 * Check if the game runs on OpenComposite instead of SteamVR.
 * OpenComposite turns the game's OpenVR calls into OpenXR, for headsets that run without SteamVR.
 * It is told by its own name in its DLL, in the game folder or installed for the whole PC.
 * SteamVR's files being loaded proves nothing, as OpenComposite can itself run on SteamVR's OpenXR.
 *
 * @return True on OpenComposite; false on SteamVR, and while not known yet, as OpenComposite for the whole PC only shows once VR starts.
 */
bool IsOpenComposite() noexcept;
} // namespace VRRuntime
