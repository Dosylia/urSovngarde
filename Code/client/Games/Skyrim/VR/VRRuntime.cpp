#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VR/VRRuntime.h>

#ifdef SKYRIMVR

#include <openvr/openvr.h>
#include <immersive_launcher/launcher.h>
#include <Helpers/ModuleImage.h>

#include <atomic>
#include <optional>
#include <string_view>

namespace
{
/**
 * Find the game's openvr_api.dll, through which it talks to its VR runtime.
 *
 * @return The loaded DLL, or null before the game has loaded it.
 */
HMODULE GetOpenVR() noexcept
{
    return GetModuleHandleW(L"openvr_api.dll");
}

/**
 * Check if a loaded DLL is OpenComposite, by its own name in its data.
 * OpenComposite's texts (its log lines, its error box's title) name it; SteamVR's files never hold that name.
 *
 * @param ahModule The loaded DLL.
 * @return True when one of its data sections contains "OpenComposite".
 */
bool HoldsOpenCompositeName(const HMODULE ahModule) noexcept
{
    // Look through the DLL's data (its texts and tables), not its code, for OpenComposite's name.
    for (const IMAGE_SECTION_HEADER& section : ModuleImage::SectionsOf(ahModule))
    {
        if (ModuleImage::IsDataSection(section) && ModuleImage::BytesOf(ahModule, section).find("OpenComposite") != std::string_view::npos)
            return true;
    }
    return false;
}

/**
 * The VR runtime the game uses.
 */
enum class Runtime
{
    kUnknown,       // not known yet
    kSteamVR,       // Valve's SteamVR
    kOpenComposite, // OpenComposite, which talks to the headset without SteamVR
};

/**
 * Where OpenComposite sits, when the game runs on it.
 */
enum class Install
{
    kNone,       // the game runs on SteamVR
    kGameFolder, // OpenComposite is the game's own openvr_api.dll
    kWholePc,    // OpenComposite replaces SteamVR's runtime file for every game on this PC
};

/**
 * What the game was found running on.
 */
struct RuntimeFound
{
    Runtime Kind = Runtime::kUnknown;
    Install Where = Install::kNone;
};

/**
 * Look at the game's VR files now to tell which runtime it uses.
 * OpenComposite in the game folder is the game's own openvr_api.dll, loaded before any of our code runs.
 * OpenComposite for the whole PC replaces SteamVR's runtime file instead, which openvr_api.dll loads only when VR starts.
 *
 * @param ahOpenVR The game's openvr_api.dll.
 * @return The runtime and where OpenComposite sits; nothing before VR starts, as the answer cannot be told yet.
 */
std::optional<RuntimeFound> LookUpRuntime(const HMODULE ahOpenVR) noexcept
{
    if (HoldsOpenCompositeName(ahOpenVR))
        return RuntimeFound{Runtime::kOpenComposite, Install::kGameFolder};

    // SteamVR's runtime file is held while it is read, so a failed VR start cannot unload it under the scan.
    HMODULE hSteamVrRuntime = nullptr;
    if (!GetModuleHandleExW(0, L"vrclient_x64.dll", &hSteamVrRuntime) || !hSteamVrRuntime)
        return std::nullopt;
    const bool cReplacedByOpenComposite = HoldsOpenCompositeName(hSteamVrRuntime);
    FreeLibrary(hSteamVrRuntime);

    if (cReplacedByOpenComposite)
        return RuntimeFound{Runtime::kOpenComposite, Install::kWholePc};
    return RuntimeFound{Runtime::kSteamVR, Install::kNone};
}

/**
 * Write once in the log which runtime the game uses and where its openvr_api.dll came from, for crash reports.
 * It says only "the game folder's" or "another folder's", never the path, which can hold the Windows user name.
 * Tools that read crash reports match this line's wording: keep it as it is.
 *
 * @param acFound The runtime found.
 * @param ahOpenVR The game's openvr_api.dll.
 */
void LogRuntime(const RuntimeFound& acFound, const HMODULE ahOpenVR) noexcept
{
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(ahOpenVR, path, MAX_PATH);
    const auto* pContext = launcher::GetLaunchContext();
    std::error_code error;
    const bool cFromGameFolder = pContext && std::filesystem::equivalent(std::filesystem::path(path).parent_path(), pContext->gamePath, error);
    const char* cpFrom = cFromGameFolder ? "the game folder's" : "another folder's";

    if (acFound.Kind == Runtime::kOpenComposite)
        spdlog::info("VRRuntime: the game runs on OpenComposite ({} openvr_api.dll){}, not SteamVR; the SteamVR menu and the banners are off", cpFrom,
                     acFound.Where == Install::kWholePc ? ", installed for the whole PC" : "");
    else
        spdlog::info("VRRuntime: the game runs on SteamVR ({} openvr_api.dll)", cpFrom);
}

/**
 * Tell which VR runtime the game uses, remembered once known.
 *
 * @return SteamVR or OpenComposite; unknown until it can be told, as OpenComposite for the whole PC only shows once VR starts.
 */
Runtime RuntimeInUse() noexcept
{
    static std::atomic<Runtime> s_runtime = Runtime::kUnknown;
    if (const Runtime cKnown = s_runtime.load(); cKnown != Runtime::kUnknown)
        return cKnown;

    const HMODULE hOpenVR = GetOpenVR();
    if (!hOpenVR)
        return Runtime::kUnknown;

    const auto cFound = LookUpRuntime(hOpenVR);
    if (!cFound)
        return Runtime::kUnknown;

    // Two threads can find it at the same moment: only the first one to record it writes the log line.
    Runtime expected = Runtime::kUnknown;
    if (s_runtime.compare_exchange_strong(expected, cFound->Kind))
        LogRuntime(*cFound, hOpenVR);
    return cFound->Kind;
}
} // namespace

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
void* GetOpenVRInterface(const char* acpVersion, int* apError) noexcept
{
    const HMODULE hOpenVR = GetOpenVR();
    if (!hOpenVR)
        return nullptr;

    // OpenVR hands out all its parts through this one function of openvr_api.dll.
    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    auto* pGetInterface = reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface"));
    if (!pGetInterface)
        return nullptr;

    vr::EVRInitError error = vr::VRInitError_None;
    void* pInterface = pGetInterface(acpVersion, &error);
    if (apError)
        *apError = static_cast<int>(error);
    return pInterface;
}

/**
 * Get OpenVR's system part: the headset and controllers as the runtime sees them, with the display and the controllers' buzz.
 *
 * @return The system part, or null before the game has started VR.
 */
vr::IVRSystem* GetOpenVRSystem() noexcept
{
    return static_cast<vr::IVRSystem*>(GetOpenVRInterface(vr::IVRSystem_Version));
}

/**
 * Get SteamVR's overlays: the panels drawn over the game, which the SteamVR menu tab and the banners use.
 * Only on SteamVR: OpenComposite has no SteamVR menu, and giving one of its overlays a picture closes the game.
 *
 * @return The overlay part, or null unless the runtime is known to be SteamVR.
 */
vr::IVROverlay* GetOpenVROverlay() noexcept
{
    if (RuntimeInUse() == Runtime::kOpenComposite)
        return nullptr;
    auto* pOverlay = static_cast<vr::IVROverlay*>(GetOpenVRInterface(vr::IVROverlay_Version));
    // An interface means VR is up, so SteamVR has loaded its runtime file by now.
    // VR up without that file is not SteamVR, whatever it is: its overlays are not used.
    if (!pOverlay || RuntimeInUse() != Runtime::kSteamVR)
        return nullptr;
    return pOverlay;
}

/**
 * Check if the game runs on OpenComposite instead of SteamVR.
 * OpenComposite turns the game's OpenVR calls into OpenXR, for headsets that run without SteamVR.
 * It is told by its own name in its DLL, in the game folder or installed for the whole PC.
 * SteamVR's files being loaded proves nothing, as OpenComposite can itself run on SteamVR's OpenXR.
 *
 * @return True on OpenComposite; false on SteamVR, and while not known yet, as OpenComposite for the whole PC only shows once VR starts.
 */
bool IsOpenComposite() noexcept
{
    return RuntimeInUse() == Runtime::kOpenComposite;
}
} // namespace VRRuntime

#endif
