#pragma once

#include <filesystem>

namespace loader
{
// The game folder's openvr_api.dll, loaded by its path before the game's imports are resolved, so the game gets that one.
void LoadGameOpenVR(const std::filesystem::path& acGamePath);
} // namespace loader
