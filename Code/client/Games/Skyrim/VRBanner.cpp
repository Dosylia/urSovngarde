#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/VRBanner.h>

#ifdef SKYRIMVR

#include <immersive_launcher/launcher.h>
#include <openvr/openvr.h>
#include <zlib.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VRBanner
{
namespace
{
using namespace std::chrono_literals;

// The overlay's picture: 2048 pixels over about 56 degrees of view, sharper than the headset shows.
constexpr int kWidth = 2048;
constexpr int kHeight = 384;
// 1.6 m wide, 1.5 m in front of the eyes and a little below them: the middle of the view, readable without turning.
constexpr float kWidthMeters = 1.6f;
constexpr float kDistanceMeters = 1.5f;
constexpr float kBelowEyesMeters = 0.05f;
constexpr auto kFadeIn = 800ms;
constexpr auto kFadeOut = 1200ms;
// How dark the band behind the text is at its middle.
constexpr float kBandAlpha = 0.55f;
// The text is written in Skyrim's handwriting (notes and letters), read from the game's own font library.
constexpr std::string_view kFontName = "SkyrimBooks_Handwritten_Bold";
// Letters are filled at twice the size and averaged down, for smooth edges.
constexpr int kSupersample = 2;

// The game has already brought OpenVR up through its own openvr_api.dll; asking that DLL shares the game's session.
vr::IVROverlay* GetOverlay() noexcept
{
    static vr::IVROverlay* s_pOverlay = nullptr;
    if (s_pOverlay)
        return s_pOverlay;
    HMODULE hOpenVR = GetModuleHandleW(L"openvr_api.dll");
    if (!hOpenVR)
        return nullptr;
    using TGetGenericInterface = void*(const char*, vr::EVRInitError*);
    auto* pGetInterface = reinterpret_cast<TGetGenericInterface*>(GetProcAddress(hOpenVR, "VR_GetGenericInterface"));
    if (!pGetInterface)
        return nullptr;
    vr::EVRInitError error = vr::VRInitError_None;
    s_pOverlay = static_cast<vr::IVROverlay*>(pGetInterface(vr::IVROverlay_Version, &error));
    return s_pOverlay;
}

vr::VROverlayHandle_t s_handle = vr::k_ulOverlayHandleInvalid;
bool s_showing = false;
std::chrono::steady_clock::time_point s_shownAt{};
std::chrono::steady_clock::time_point s_fadeOutAt{};

bool EnsureOverlay(vr::IVROverlay* apOverlay) noexcept
{
    if (s_handle != vr::k_ulOverlayHandleInvalid)
        return true;
    const auto cError = apOverlay->CreateOverlay("ursovngarde.banner", "urSovngarde banner", &s_handle);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: the overlay could not be created ({})", static_cast<int>(cError));
        s_handle = vr::k_ulOverlayHandleInvalid;
        return false;
    }
    apOverlay->SetOverlayWidthInMeters(s_handle, kWidthMeters);
    vr::HmdMatrix34_t placement = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, -kBelowEyesMeters}, {0.f, 0.f, 1.f, -kDistanceMeters}}};
    apOverlay->SetOverlayTransformTrackedDeviceRelative(s_handle, vr::k_unTrackedDeviceIndex_Hmd, &placement);
    return true;
}

float Smooth(const float aValue) noexcept
{
    const float cClamped = std::clamp(aValue, 0.f, 1.f);
    return cClamped * cClamped * (3.f - 2.f * cClamped);
}

struct Glyph
{
    float Advance{};
    // In 1/1024 of the letter height, y down from the baseline.
    std::vector<std::vector<glm::vec2>> Contours;
};

struct Font
{
    std::unordered_map<wchar_t, Glyph> Glyphs;
    float Ascent{};
    float Descent{};
};

// A file in an uncompressed archive of version 105 (Skyrim SE and VR); empty when it is not there.
std::vector<uint8_t> ReadFromArchive(const std::filesystem::path& acArchive, const std::string_view acFolder, const std::string_view acFile) noexcept
{
    std::ifstream archive(acArchive, std::ios::binary);
    struct Header
    {
        char Magic[4];
        uint32_t Version, FoldersAt, Flags, FolderCount, FileCount, FolderNamesSize, FileNamesSize, FileFlags;
    } header{};
    archive.read(reinterpret_cast<char*>(&header), sizeof(header));
    constexpr uint32_t cNames = 0x1 | 0x2;
    if (!archive || std::memcmp(header.Magic, "BSA", 4) != 0 || header.Version != 105 || (header.Flags & cNames) != cNames)
        return {};

    struct FolderRecord
    {
        uint64_t Hash;
        uint32_t Count, Padding;
        uint64_t At;
    };
    struct FileRecord
    {
        uint64_t Hash;
        uint32_t Size, At;
    };
    std::vector<FolderRecord> folders(header.FolderCount);
    archive.seekg(header.FoldersAt);
    archive.read(reinterpret_cast<char*>(folders.data()), static_cast<std::streamsize>(folders.size() * sizeof(FolderRecord)));

    std::vector<std::pair<std::string, FileRecord>> files;
    for (const FolderRecord& cFolder : folders)
    {
        const int cNameLength = archive.get();
        if (cNameLength <= 0)
            return {};
        std::string name(static_cast<size_t>(cNameLength), '\0');
        archive.read(name.data(), cNameLength);
        name.resize(std::strlen(name.c_str()));
        for (uint32_t i = 0; i < cFolder.Count && archive; ++i)
        {
            FileRecord record{};
            archive.read(reinterpret_cast<char*>(&record), sizeof(record));
            files.emplace_back(name, record);
        }
    }

    for (const auto& [cFolder, cRecord] : files)
    {
        std::string name;
        if (!std::getline(archive, name, '\0'))
            return {};
        if (cFolder != acFolder || name != acFile)
            continue;
        const bool cCompressed = ((header.Flags & 0x4) != 0) != ((cRecord.Size & 0x40000000) != 0);
        if (cCompressed)
            return {};
        std::vector<uint8_t> bytes(cRecord.Size & 0x3FFFFFFF);
        archive.seekg(cRecord.At);
        archive.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!archive)
            return {};
        if ((header.Flags & 0x100) && !bytes.empty())
            bytes.erase(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), bytes[0] + 1u));
        return bytes;
    }
    return {};
}

// The tags of a Flash file; the game's font libraries are zlib-compressed.
std::vector<uint8_t> FlashBody(const std::vector<uint8_t>& acFile) noexcept
{
    if (acFile.size() < 8 || acFile[1] != 'W' || acFile[2] != 'S')
        return {};
    if (acFile[0] == 'F')
        return {acFile.begin() + 8, acFile.end()};
    uint32_t size = 0;
    std::memcpy(&size, &acFile[4], sizeof(size));
    if (acFile[0] != 'C' || size < 8)
        return {};
    std::vector<uint8_t> body(size - 8);
    uLongf written = static_cast<uLongf>(body.size());
    if (uncompress(body.data(), &written, &acFile[8], static_cast<uLong>(acFile.size() - 8)) != Z_OK)
        return {};
    body.resize(written);
    return body;
}

class BitReader
{
public:
    BitReader(const uint8_t* apData, const size_t aSize) noexcept
        : m_pData(apData)
        , m_size(aSize)
    {
    }

    uint32_t Unsigned(const uint32_t aBits) noexcept
    {
        uint32_t value = 0;
        for (uint32_t i = 0; i < aBits; ++i, ++m_bit)
        {
            const size_t cByte = m_bit / 8;
            value = (value << 1) | (cByte < m_size ? (m_pData[cByte] >> (7 - m_bit % 8)) & 1u : 0u);
        }
        return value;
    }

    float Signed(const uint32_t aBits) noexcept
    {
        const int64_t cValue = Unsigned(aBits);
        return static_cast<float>(aBits && (cValue >> (aBits - 1)) ? cValue - (int64_t{1} << aBits) : cValue);
    }

    size_t BytesRead() const noexcept { return (m_bit + 7) / 8; }

private:
    const uint8_t* m_pData;
    size_t m_size;
    size_t m_bit{0};
};

// A letter's outlines; each curve becomes eight straight pieces.
std::vector<std::vector<glm::vec2>> ReadShape(const uint8_t* apData, const size_t aSize) noexcept
{
    BitReader bits(apData, aSize);
    const uint32_t cFillBits = bits.Unsigned(4);
    const uint32_t cLineBits = bits.Unsigned(4);
    std::vector<std::vector<glm::vec2>> contours;
    glm::vec2 at{};
    while (true)
    {
        if (bits.Unsigned(1) == 0)
        {
            const uint32_t cFlags = bits.Unsigned(5);
            if (cFlags == 0 || (cFlags & 16))
                break;
            if (cFlags & 1)
            {
                const uint32_t cBits = bits.Unsigned(5);
                at.x = bits.Signed(cBits);
                at.y = bits.Signed(cBits);
                contours.emplace_back(1, at);
            }
            bits.Unsigned((cFlags & 2 ? cFillBits : 0) + (cFlags & 4 ? cFillBits : 0) + (cFlags & 8 ? cLineBits : 0));
            continue;
        }

        if (contours.empty())
            contours.emplace_back(1, at);
        const bool cStraight = bits.Unsigned(1);
        const uint32_t cBits = bits.Unsigned(4) + 2;
        if (cStraight)
        {
            if (bits.Unsigned(1))
            {
                at.x += bits.Signed(cBits);
                at.y += bits.Signed(cBits);
            }
            else if (bits.Unsigned(1))
                at.y += bits.Signed(cBits);
            else
                at.x += bits.Signed(cBits);
            contours.back().push_back(at);
            continue;
        }

        glm::vec2 control = at;
        control.x += bits.Signed(cBits);
        control.y += bits.Signed(cBits);
        glm::vec2 anchor = control;
        anchor.x += bits.Signed(cBits);
        anchor.y += bits.Signed(cBits);
        for (int i = 1; i <= 8; ++i)
        {
            const float t = i / 8.f;
            contours.back().push_back((1.f - t) * (1.f - t) * at + 2.f * (1.f - t) * t * control + t * t * anchor);
        }
        at = anchor;
    }
    std::erase_if(contours, [](const auto& acContour) { return acContour.size() < 3; });
    return contours;
}

// One font of a font library; DefineFont3 stores 20 units for each 1/1024 of the letter height.
std::optional<Font> ReadFont(const std::vector<uint8_t>& acBody, const std::string_view acName) noexcept
{
    BitReader frame(acBody.data(), acBody.size());
    const uint32_t cRectBits = frame.Unsigned(5);
    frame.Unsigned(cRectBits * 2);
    frame.Unsigned(cRectBits * 2);
    size_t at = frame.BytesRead() + 4;

    while (at + 2 <= acBody.size())
    {
        uint16_t codeAndLength = 0;
        std::memcpy(&codeAndLength, &acBody[at], 2);
        at += 2;
        uint32_t length = codeAndLength & 0x3F;
        if (length == 0x3F && at + 4 <= acBody.size())
        {
            std::memcpy(&length, &acBody[at], 4);
            at += 4;
        }
        if ((codeAndLength >> 6) == 0 || at + length > acBody.size())
            break;
        const uint8_t* pTag = &acBody[at];
        at += length;
        if ((codeAndLength >> 6) != 75 || length < 5 || 5u + pTag[4] > length ||
            std::string_view(reinterpret_cast<const char*>(pTag + 5), strnlen(reinterpret_cast<const char*>(pTag + 5), pTag[4])) != acName)
            continue;

        const auto read = [pTag, length](const size_t aAt, const size_t aBytes) -> uint32_t
        {
            uint32_t value = 0;
            if (aAt + aBytes <= length)
                std::memcpy(&value, pTag + aAt, aBytes);
            return value;
        };
        const bool cWideOffsets = pTag[2] & 0x08;
        const bool cWideCodes = pTag[2] & 0x04;
        if (!(pTag[2] & 0x80))
            return std::nullopt;
        const size_t cTable = 7u + pTag[4];
        const uint32_t cCount = read(cTable - 2, 2);
        const auto offset = [&](const size_t aIndex)
        {
            return cTable + (cWideOffsets ? read(cTable + aIndex * 4, 4) : read(cTable + aIndex * 2, 2));
        };
        const size_t cCodes = offset(cCount);
        const size_t cLayout = cCodes + cCount * (cWideCodes ? 2 : 1);
        if (cLayout + 6 + cCount * 2 > length)
            return std::nullopt;

        Font font;
        font.Ascent = read(cLayout, 2) / 20.f;
        font.Descent = read(cLayout + 2, 2) / 20.f;
        for (uint32_t i = 0; i < cCount; ++i)
        {
            const size_t cShape = offset(i);
            const size_t cEnd = i + 1 < cCount ? offset(i + 1) : cCodes;
            Glyph glyph;
            glyph.Advance = static_cast<int16_t>(read(cLayout + 6 + i * 2, 2)) / 20.f;
            glyph.Contours = ReadShape(pTag + cShape, cEnd > cShape && cEnd <= length ? cEnd - cShape : 0);
            for (auto& contour : glyph.Contours)
                for (glm::vec2& point : contour)
                    point /= 20.f;
            font.Glyphs[static_cast<wchar_t>(cWideCodes ? read(cCodes + i * 2, 2) : read(cCodes + i, 1))] = std::move(glyph);
        }
        return font;
    }
    return std::nullopt;
}

// Read once; a mod's loose fonts_en.swf comes first, as it does for the game.
const Font* Handwriting() noexcept
{
    static const std::optional<Font> s_font = []() -> std::optional<Font>
    {
        const auto* pContext = launcher::GetLaunchContext();
        if (!pContext)
            return std::nullopt;
        const auto cData = pContext->gamePath / L"Data";
        std::ifstream loose(cData / L"Interface" / L"fonts_en.swf", std::ios::binary);
        std::vector<uint8_t> library{std::istreambuf_iterator<char>(loose), std::istreambuf_iterator<char>()};
        if (library.empty())
            library = ReadFromArchive(cData / L"Skyrim - Interface.bsa", "interface", "fonts_en.swf");
        auto font = ReadFont(FlashBody(library), kFontName);
        if (font)
            spdlog::info("VRBanner: written in Skyrim's handwriting ({} letters)", font->Glyphs.size());
        else
            spdlog::warn("VRBanner: Skyrim's handwriting could not be read from fonts_en.swf; written in Georgia");
        return font;
    }();
    return s_font ? &*s_font : nullptr;
}

// A blank 32-bit picture for GDI to draw into.
class Canvas
{
public:
    Canvas(const int aWidth, const int aHeight) noexcept
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = aWidth;
        info.bmiHeader.biHeight = -aHeight; // top-down rows
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        m_hdc = CreateCompatibleDC(nullptr);
        m_hBitmap = m_hdc ? CreateDIBSection(m_hdc, &info, DIB_RGB_COLORS, &m_pBits, nullptr, 0) : nullptr;
        if (m_hBitmap && m_pBits)
        {
            m_oldBitmap = SelectObject(m_hdc, m_hBitmap);
            std::memset(m_pBits, 0, static_cast<size_t>(aWidth) * aHeight * 4);
        }
    }

    ~Canvas()
    {
        if (m_oldBitmap)
            SelectObject(m_hdc, m_oldBitmap);
        if (m_hBitmap)
            DeleteObject(m_hBitmap);
        if (m_hdc)
            DeleteDC(m_hdc);
    }

    bool IsValid() const noexcept { return m_oldBitmap != nullptr; }
    HDC Dc() const noexcept { return m_hdc; }
    // BGRA; antialiasing greys every channel alike.
    const uint8_t* Bits() const noexcept { return static_cast<const uint8_t*>(m_pBits); }

private:
    HDC m_hdc{};
    HBITMAP m_hBitmap{};
    HGDIOBJ m_oldBitmap{};
    void* m_pBits{};
};

// How much of each pixel the letters cover, from 0 to 1.
// GDI draws the text white on black; how white each pixel came out is how much of the letter covers it.
std::vector<float> CoverInGeorgia(const std::wstring& acText, const int aTextHeight) noexcept
{
    Canvas canvas(kWidth, kHeight);
    if (!canvas.IsValid())
        return {};

    HFONT hFont = CreateFontW(-aTextHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              VARIABLE_PITCH | FF_ROMAN, L"Georgia");
    HGDIOBJ oldFont = SelectObject(canvas.Dc(), hFont);
    SetBkMode(canvas.Dc(), TRANSPARENT);
    SetTextColor(canvas.Dc(), RGB(255, 255, 255));
    SetTextCharacterExtra(canvas.Dc(), aTextHeight / 8);
    RECT area{0, 0, kWidth, kHeight};
    DrawTextW(canvas.Dc(), acText.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(canvas.Dc(), oldFont);
    DeleteObject(hFont);

    std::vector<float> cover(static_cast<size_t>(kWidth) * kHeight);
    for (size_t i = 0; i < cover.size(); ++i)
        cover[i] = canvas.Bits()[i * 4 + 2] / 255.f;
    return cover;
}

// The same layout as the DrawTextW call above; empty when the font lacks a letter of the text.
std::vector<float> CoverInHandwriting(const Font& acFont, const std::wstring& acText, const int aTextHeight) noexcept
{
    const float cScale = aTextHeight / 1024.f;
    const float cSpacing = static_cast<float>(aTextHeight / 8);
    std::vector<const Glyph*> glyphs;
    float width = 0.f;
    for (const wchar_t cLetter : acText)
    {
        const auto cIt = acFont.Glyphs.find(cLetter);
        if (cIt == acFont.Glyphs.end())
        {
            spdlog::info("VRBanner: Skyrim's handwriting has no letter U+{:04X}; this one is written in Georgia", static_cast<uint32_t>(cLetter));
            return {};
        }
        glyphs.push_back(&cIt->second);
        width += cIt->second.Advance * cScale + cSpacing;
    }

    Canvas canvas(kWidth * kSupersample, kHeight * kSupersample);
    if (!canvas.IsValid())
        return {};
    SelectObject(canvas.Dc(), GetStockObject(WHITE_BRUSH));
    SelectObject(canvas.Dc(), GetStockObject(NULL_PEN));
    SetPolyFillMode(canvas.Dc(), ALTERNATE);

    float x = (kWidth - width) * 0.5f;
    const float cBaseline = (kHeight - (acFont.Ascent + acFont.Descent) * cScale) * 0.5f + acFont.Ascent * cScale;
    std::vector<POINT> points;
    std::vector<INT> counts;
    for (const Glyph* pGlyph : glyphs)
    {
        points.clear();
        counts.clear();
        for (const auto& cContour : pGlyph->Contours)
        {
            for (const glm::vec2& cPoint : cContour)
                points.push_back({std::lround((x + cPoint.x * cScale) * kSupersample), std::lround((cBaseline + cPoint.y * cScale) * kSupersample)});
            counts.push_back(static_cast<INT>(cContour.size()));
        }
        if (!counts.empty())
            PolyPolygon(canvas.Dc(), points.data(), counts.data(), static_cast<int>(counts.size()));
        x += pGlyph->Advance * cScale + cSpacing;
    }
    GdiFlush();

    std::vector<float> cover(static_cast<size_t>(kWidth) * kHeight);
    constexpr float cFull = 255.f * kSupersample * kSupersample;
    for (int y = 0; y < kHeight; ++y)
        for (int column = 0; column < kWidth; ++column)
        {
            uint32_t sum = 0;
            for (int dy = 0; dy < kSupersample; ++dy)
                for (int dx = 0; dx < kSupersample; ++dx)
                    sum += canvas.Bits()[((static_cast<size_t>(y) * kSupersample + dy) * kWidth * kSupersample + column * kSupersample + dx) * 4 + 2];
            cover[static_cast<size_t>(y) * kWidth + column] = sum / cFull;
        }
    return cover;
}

// The picture is the letter's colour over a dark band that fades out towards its edges.
std::vector<uint8_t> Render(const std::wstring& acText, const uint32_t aRgb, const int aTextHeight) noexcept
{
    std::vector<uint8_t> rgba(static_cast<size_t>(kWidth) * kHeight * 4, 0);

    const int cTextHeight = aTextHeight * kWidth / 1024;
    std::vector<float> cover;
    if (const Font* pFont = Handwriting())
        cover = CoverInHandwriting(*pFont, acText, cTextHeight);
    if (cover.empty())
        cover = CoverInGeorgia(acText, cTextHeight);
    if (cover.empty())
        return rgba;

    const float cRed = static_cast<float>((aRgb >> 16) & 0xFF);
    const float cGreen = static_cast<float>((aRgb >> 8) & 0xFF);
    const float cBlue = static_cast<float>(aRgb & 0xFF);
    for (int y = 0; y < kHeight; ++y)
    {
        const float cAcross = 1.f - std::abs(static_cast<float>(y) - kHeight * 0.5f) / (kHeight * 0.5f);
        for (int x = 0; x < kWidth; ++x)
        {
            const size_t cAt = (static_cast<size_t>(y) * kWidth + x) * 4;
            const float cCover = cover[static_cast<size_t>(y) * kWidth + x];
            const float cAlong = std::min(static_cast<float>(std::min(x, kWidth - 1 - x)) / (kWidth * 0.3f), 1.f);
            const float cBand = kBandAlpha * Smooth(cAcross * 1.6f) * Smooth(cAlong);
            const float cAlpha = cCover + cBand * (1.f - cCover);
            // The colour that, at this alpha over the game, shows the letter's colour where it covers and black elsewhere.
            const float cShare = cAlpha > 0.f ? cCover / cAlpha : 0.f;
            rgba[cAt + 0] = static_cast<uint8_t>(cRed * cShare);
            rgba[cAt + 1] = static_cast<uint8_t>(cGreen * cShare);
            rgba[cAt + 2] = static_cast<uint8_t>(cBlue * cShare);
            rgba[cAt + 3] = static_cast<uint8_t>(cAlpha * 255.f);
        }
    }
    return rgba;
}

std::wstring Widen(const std::string& acText) noexcept
{
    const int cLength = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    if (cLength <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(cLength - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, wide.data(), cLength);
    return wide;
}
} // namespace

void Show(const std::string& acText, const uint32_t aRgb, const int aTextHeight, const float aHoldSeconds) noexcept
{
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay || !EnsureOverlay(pOverlay))
        return;

    std::vector<uint8_t> pixels = Render(Widen(acText), aRgb, aTextHeight);
    const auto cError = pOverlay->SetOverlayRaw(s_handle, pixels.data(), kWidth, kHeight, 4);
    if (cError != vr::VROverlayError_None)
    {
        spdlog::warn("VRBanner: '{}' could not be drawn ({})", acText, static_cast<int>(cError));
        return;
    }
    const auto cNow = std::chrono::steady_clock::now();
    s_shownAt = cNow;
    s_fadeOutAt = cNow + kFadeIn + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(aHoldSeconds));
    s_showing = true;
    pOverlay->SetOverlayAlpha(s_handle, 0.f);
    pOverlay->ShowOverlay(s_handle);
    spdlog::info("VRBanner: '{}' shown for {:.1f} s", acText, aHoldSeconds);
}

void Hide() noexcept
{
    if (s_showing)
        s_fadeOutAt = std::min(s_fadeOutAt, std::chrono::steady_clock::now());
}

void Update() noexcept
{
    if (!s_showing)
        return;
    vr::IVROverlay* pOverlay = GetOverlay();
    if (!pOverlay)
        return;

    const auto cNow = std::chrono::steady_clock::now();
    float alpha = 1.f;
    if (cNow < s_shownAt + kFadeIn)
        alpha = std::chrono::duration<float>(cNow - s_shownAt) / std::chrono::duration<float>(kFadeIn);
    else if (cNow >= s_fadeOutAt + kFadeOut)
    {
        pOverlay->HideOverlay(s_handle);
        s_showing = false;
        return;
    }
    else if (cNow >= s_fadeOutAt)
        alpha = 1.f - std::chrono::duration<float>(cNow - s_fadeOutAt) / std::chrono::duration<float>(kFadeOut);
    pOverlay->SetOverlayAlpha(s_handle, std::clamp(alpha, 0.f, 1.f));
}
} // namespace VRBanner

#else

namespace VRBanner
{
void Show(const std::string&, uint32_t, int, float) noexcept
{
}

void Hide() noexcept
{
}

void Update() noexcept
{
}
} // namespace VRBanner

#endif
