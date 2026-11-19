#include <TiltedOnlinePCH.h>

#include <Helpers/ModuleImage.h>

namespace
{
/**
 * Find a loaded program's main header, after checking it reads as a Windows program.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its main header, or null when the headers do not carry Windows' signatures.
 */
const IMAGE_NT_HEADERS* MainHeaderOf(const HMODULE ahModule) noexcept
{
    if (!ahModule)
        return nullptr;

    // A Windows program starts with the old DOS header, which says where the main header is.
    const auto* pBase = reinterpret_cast<const uint8_t*>(ahModule);
    const auto* pDosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(pBase);
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;

    const auto* pMainHeader = reinterpret_cast<const IMAGE_NT_HEADERS*>(pBase + pDosHeader->e_lfanew);
    if (pMainHeader->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    return pMainHeader;
}

/**
 * Read a loaded program or DLL's file name, without its folder.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its file name, such as "SkyrimVR.exe".
 */
std::string FileNameOf(const HMODULE ahModule) noexcept
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(ahModule, path, MAX_PATH);
    const std::wstring_view cPath(path);
    const std::wstring_view cName = cPath.substr(cPath.find_last_of(L"\\/") + 1);
    // The game's and the plugins' file names are plain letters: each wide character is kept as one byte.
    return std::string(cName.begin(), cName.end());
}
} // namespace

namespace ModuleImage
{
/**
 * Find where a loaded program or DLL starts and ends in memory.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its range; both ends 0 when its headers do not read as a Windows program.
 */
Range RangeOf(const HMODULE ahModule) noexcept
{
    const IMAGE_NT_HEADERS* pMainHeader = MainHeaderOf(ahModule);
    if (!pMainHeader)
        return {};

    const auto begin = reinterpret_cast<uintptr_t>(ahModule);
    return {begin, begin + pMainHeader->OptionalHeader.SizeOfImage};
}

/**
 * List the sections of a loaded program or DLL, such as its code and its data.
 *
 * @param ahModule The loaded program or DLL.
 * @return Its sections, or none when its headers do not read as a Windows program.
 */
std::span<const IMAGE_SECTION_HEADER> SectionsOf(const HMODULE ahModule) noexcept
{
    const IMAGE_NT_HEADERS* pMainHeader = MainHeaderOf(ahModule);
    if (!pMainHeader)
        return {};

    // The section list follows the main header.
    return {IMAGE_FIRST_SECTION(pMainHeader), pMainHeader->FileHeader.NumberOfSections};
}

/**
 * Check if a section holds data written into the file, such as texts and tables, rather than code.
 *
 * @param acSection One section of a loaded program or DLL.
 * @return True for a data section that is never run as code.
 */
bool IsDataSection(const IMAGE_SECTION_HEADER& acSection) noexcept
{
    const bool cHoldsData = (acSection.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) != 0;
    const bool cRunsAsCode = (acSection.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    return cHoldsData && !cRunsAsCode;
}

/**
 * Read a section's bytes as Windows has loaded them.
 *
 * @param ahModule The loaded program or DLL the section belongs to.
 * @param acSection One of its sections.
 * @return The section's bytes in memory.
 */
std::string_view BytesOf(const HMODULE ahModule, const IMAGE_SECTION_HEADER& acSection) noexcept
{
    const auto* pStart = reinterpret_cast<const char*>(ahModule) + acSection.VirtualAddress;
    // In memory a section takes its loaded size; some tools leave that 0 and only give its size in the file.
    const size_t cSize = acSection.Misc.VirtualSize ? acSection.Misc.VirtualSize : acSection.SizeOfRawData;
    return {pStart, cSize};
}

/**
 * Find the program or DLL an address belongs to.
 * The game's own code is in none Windows knows of: the launcher loads the game itself.
 *
 * @param apAddress An address in memory, usually in code.
 * @return The program or DLL, or null when the address is in none.
 */
HMODULE ModuleOf(const void* apAddress) noexcept
{
    HMODULE hModule = nullptr;
    // Asked by address, and without holding the module loaded: it is only looked up.
    if (!apAddress || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(apAddress), &hModule))
        return nullptr;
    return hModule;
}

/**
 * Name the program or DLL an address belongs to, for the log.
 * Only its file name: a full path can hold the Windows user name.
 *
 * @param apAddress An address in memory, usually in code.
 * @return The file name, such as "SkyrimVR.exe", or "no module" when the address is in none.
 */
std::string NameOf(const void* apAddress) noexcept
{
    const HMODULE cModule = ModuleOf(apAddress);
    return cModule ? FileNameOf(cModule) : "no module";
}

/**
 * Write where an address is, for the log, as "module+offset", such as "SkyrimVR.exe+0x5f2a10".
 *
 * @param apAddress An address in memory, usually in code.
 * @return The program or DLL's file name and the offset into it, or the bare address when it is in none.
 */
std::string Where(const void* apAddress) noexcept
{
    const HMODULE cModule = ModuleOf(apAddress);
    if (!cModule)
        return fmt::format("{}", apAddress);
    return fmt::format("{}+{:#x}", FileNameOf(cModule), reinterpret_cast<uintptr_t>(apAddress) - reinterpret_cast<uintptr_t>(cModule));
}
} // namespace ModuleImage
