#pragma once

#include <windows.h>

#include <cstdint>
#include <span>
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
} // namespace ModuleImage
