#include "OpenVRLoader.h"

#include <Windows.h>

namespace loader
{
// The launcher limits DLL lookups to its listed folders and adds Steam's own, which holds an openvr_api.dll too.
// So the game would get Steam's copy and always run on SteamVR, even for a player who put OpenComposite in the game folder.
// Loaded by its path first, the game folder's copy is the one the game's import finds, as when the game runs alone.
// Its own DLLs (OpenComposite's upscalers) come from its folder.
void LoadGameOpenVR(const std::filesystem::path& acGamePath)
{
#ifdef SKYRIMVR
    const auto cPath = acGamePath / L"openvr_api.dll";
    std::error_code error;
    if (std::filesystem::exists(cPath, error))
        LoadLibraryExW(cPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    (void)acGamePath;
#endif
}
} // namespace loader
