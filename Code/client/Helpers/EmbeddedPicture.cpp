#include <TiltedOnlinePCH.h>

#include <Helpers/EmbeddedPicture.h>
#include <Helpers/SafeRelease.h>

#include <wincodec.h>

#include <map>
#include <string>
#include <string_view>

#pragma comment(lib, "ole32")
#pragma comment(lib, "windowscodecs")

// The linker's mark for the start of the program this code is built into: urSovngarde.exe.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
// The pictures built into urSovngarde.exe that the mod reads, by their names among the exe's resources.
constexpr const wchar_t* kBuiltInPictures[]{L"URSOVNGARDE_ICON"};

/**
 * Copy the bytes of a file built into urSovngarde.exe.
 *
 * @param acpName The file's name among the exe's resources.
 * @return A copy of its bytes; empty when the exe holds no such file.
 */
std::vector<uint8_t> CopyResource(const wchar_t* acpName) noexcept
{
    // RT_RCDATA, a file built in as it is, written in the wide form FindResourceW takes.
    const LPCWSTR cpFileType = MAKEINTRESOURCEW(10);
    // Found from the linker's mark, not by asking Windows, which answers with the game's file once the game runs.
    const auto cExe = reinterpret_cast<HMODULE>(&__ImageBase);
    const HRSRC cResource = FindResourceW(cExe, acpName, cpFileType);
    const HGLOBAL cLoaded = cResource ? LoadResource(cExe, cResource) : nullptr;
    const auto* pBytes = cLoaded ? static_cast<const uint8_t*>(LockResource(cLoaded)) : nullptr;
    if (!pBytes)
        return {};
    return {pBytes, pBytes + SizeofResource(cExe, cResource)};
}

/**
 * Copy every picture listed in kBuiltInPictures, while urSovngarde.exe's built-in files can still be found.
 * Loading the game lays the game's headers over the exe's own in memory, and from then on Windows finds only the game's built-in files.
 *
 * @return Each picture's PNG bytes, by name; a picture the exe does not hold is left out.
 */
std::map<std::wstring, std::vector<uint8_t>> CopyBuiltInPictures() noexcept
{
    std::map<std::wstring, std::vector<uint8_t>> pictures;
    for (const wchar_t* pName : kBuiltInPictures)
    {
        if (auto bytes = CopyResource(pName); !bytes.empty())
            pictures.emplace(pName, std::move(bytes));
    }
    return pictures;
}

// The built-in pictures, copied as the program starts: the exe's own start-up runs before the launcher loads the game.
const std::map<std::wstring, std::vector<uint8_t>> s_builtInPictures = CopyBuiltInPictures();

/**
 * Decode a PNG file into pixels, with Windows' own picture reader (WIC).
 *
 * @param acBytes The PNG file's bytes.
 * @return Its pixels, or nothing when Windows cannot read them as a picture.
 */
std::optional<EmbeddedPicture::Picture> DecodePng(const std::vector<uint8_t>& acBytes) noexcept
{
    // The picture reader is a COM service, so COM must be on for this thread.
    // A thread that already has COM in another mode keeps it: the reader works in either.
    const HRESULT cComStarted = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IWICImagingFactory* pFactory = nullptr;
    IWICStream* pStream = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;

    // The file's first picture, turned into red, green, blue and alpha bytes.
    auto* pFileBytes = const_cast<BYTE*>(acBytes.data());
    bool read = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))) &&
                SUCCEEDED(pFactory->CreateStream(&pStream)) && SUCCEEDED(pStream->InitializeFromMemory(pFileBytes, static_cast<DWORD>(acBytes.size()))) &&
                SUCCEEDED(pFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder)) &&
                SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) && SUCCEEDED(pFactory->CreateFormatConverter(&pConverter)) &&
                SUCCEEDED(pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    UINT width = 0;
    UINT height = 0;
    read = read && SUCCEEDED(pConverter->GetSize(&width, &height)) && width > 0 && height > 0;

    std::optional<EmbeddedPicture::Picture> picture;
    if (read)
    {
        EmbeddedPicture::Picture decoded;
        decoded.Width = width;
        decoded.Height = height;
        decoded.Rgba.resize(static_cast<size_t>(width) * height * 4);
        if (SUCCEEDED(pConverter->CopyPixels(nullptr, width * 4, static_cast<UINT>(decoded.Rgba.size()), decoded.Rgba.data())))
            picture = std::move(decoded);
    }

    SafeRelease(pConverter);
    SafeRelease(pFrame);
    SafeRelease(pDecoder);
    SafeRelease(pStream);
    SafeRelease(pFactory);
    // Each start of COM that succeeded is matched by a stop, so the thread is left as it was.
    if (SUCCEEDED(cComStarted))
        CoUninitialize();
    return picture;
}
} // namespace

namespace EmbeddedPicture
{
/**
 * Read a PNG picture built into urSovngarde.exe.
 * Only the pictures listed in kBuiltInPictures can be read: they are copied as the program starts, before the game is loaded.
 *
 * @param acpName The picture's name among the exe's resources, such as L"URSOVNGARDE_ICON".
 * @return Its pixels, or nothing when the exe holds no such picture or Windows cannot read it.
 */
std::optional<Picture> Load(const wchar_t* acpName) noexcept
{
    // Resource names are plain letters: each wide character is kept as one byte for the log.
    const std::wstring_view cWideName(acpName);
    const std::string cName(cWideName.begin(), cWideName.end());

    const auto cFound = s_builtInPictures.find(std::wstring(cWideName));
    if (cFound == s_builtInPictures.end())
    {
        spdlog::warn("EmbeddedPicture: urSovngarde.exe holds no picture named {}", cName);
        return std::nullopt;
    }
    auto picture = DecodePng(cFound->second);
    if (!picture)
        spdlog::warn("EmbeddedPicture: Windows could not read the picture {} ({} bytes)", cName, cFound->second.size());
    return picture;
}
} // namespace EmbeddedPicture
