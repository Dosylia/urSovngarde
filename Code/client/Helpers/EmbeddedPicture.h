#pragma once

#include <cstdint>
#include <optional>
#include <vector>

/**
 * Pictures built into urSovngarde.exe as PNG files, read as pixels when needed.
 */
namespace EmbeddedPicture
{
/**
 * A picture as rows of pixels from its top left, each pixel red, green, blue and alpha.
 */
struct Picture
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<uint8_t> Rgba;
};

/**
 * Read a PNG picture built into urSovngarde.exe.
 * Only the pictures listed in kBuiltInPictures can be read: they are copied as the program starts, before the game is loaded.
 *
 * @param acpName The picture's name among the exe's resources, such as L"URSOVNGARDE_ICON".
 * @return Its pixels, or nothing when the exe holds no such picture or Windows cannot read it.
 */
std::optional<Picture> Load(const wchar_t* acpName) noexcept;
} // namespace EmbeddedPicture
