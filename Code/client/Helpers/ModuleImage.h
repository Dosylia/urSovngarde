#pragma once

#include <windows.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

/**
 * Reads a program or DLL as Windows has loaded it in memory: where it starts and ends, and its sections.
 * A section is one part of the file, such as its code or its data (texts, tables).
 */
namespace ModuleImage
{
/**
 * Where a loaded program or DLL lies in memory: Begin is its first address, End the first address after it.
 */
struct Range
{
    uintptr_t Begin = 0;
    uintptr_t End = 0;
};

/**
 * Find where a loaded program or DLL starts and ends in memory.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its range; both ends 0 when its headers do not read as a Windows program.
 */
Range RangeOf(HMODULE ahModule) noexcept;

/**
 * List the sections of a loaded program or DLL, such as its code and its data.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its sections, or none when its headers do not read as a Windows program.
 */
std::span<const IMAGE_SECTION_HEADER> SectionsOf(HMODULE ahModule) noexcept;

/**
 * Check if a section holds data written into the file, such as texts and tables, rather than code.
 *
 * @param acSection One section of a loaded program or DLL.
 * @return True for a data section that is never run as code.
 */
bool IsDataSection(const IMAGE_SECTION_HEADER& acSection) noexcept;

/**
 * Read a section's bytes as Windows has loaded them.
 *
 * @param ahModule The loaded program or DLL the section belongs to.
 * @param acSection One of its sections.
 * @return The section's bytes in memory.
 */
std::string_view BytesOf(HMODULE ahModule, const IMAGE_SECTION_HEADER& acSection) noexcept;

/**
 * Find the program or DLL an address belongs to.
 * The game's own code is in none Windows knows of: the launcher loads the game itself.
 *
 * @param apAddress An address in memory, usually in code.
 * @return The program or DLL, or null when the address is in none.
 */
HMODULE ModuleOf(const void* apAddress) noexcept;

/**
 * Name the program or DLL an address belongs to, for the log.
 * Only its file name: a full path can hold the Windows user name.
 *
 * @param apAddress An address in memory, usually in code.
 * @return The file name, such as "SkyrimVR.exe", or "no module" when the address is in none.
 */
std::string NameOf(const void* apAddress) noexcept;

/**
 * Write where an address is, for the log, as "module+offset", such as "SkyrimVR.exe+0x5f2a10".
 *
 * @param apAddress An address in memory, usually in code.
 * @return The program or DLL's file name and the offset into it, or the bare address when it is in none.
 */
std::string Where(const void* apAddress) noexcept;
} // namespace ModuleImage
