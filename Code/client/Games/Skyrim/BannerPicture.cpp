#include <Games/Skyrim/BannerPicture.h>

#include <Windows.h>
#include <zlib.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace BannerPicture
{
namespace
{
// 2048 pixels over about 56 degrees of view in the headset, sharper than it shows.
constexpr int kWidth = 2048;
constexpr int kLineHeight = 384;
constexpr int kCardHeight = 640;
// How dark the band behind a picture is at its middle.
constexpr float kLineBand = 0.55f;
constexpr float kCardBand = 0.72f;
// Letters and lines are filled at twice the size and averaged down, for smooth edges.
constexpr int kSupersample = 2;

// The fonts, by their names in the game's font library (fonts_en.swf).
constexpr std::string_view kHandwriting = "SkyrimBooks_Handwritten_Bold";
constexpr std::string_view kTitleFont = "SkyrimBooks_Gaelic";
constexpr std::string_view kSubtitleFont = "Futura Condensed";

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
    if (acBody.empty())
        return std::nullopt;
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

std::mutex s_fontLock;
std::filesystem::path s_gameData;

// Read once, each font on first use; a mod's loose fonts_en.swf comes first, as it does for the game.
const Font* FindFont(const std::string_view acName) noexcept
{
    std::scoped_lock lock(s_fontLock);
    static std::optional<std::vector<uint8_t>> s_body;
    static std::map<std::string, std::optional<Font>, std::less<>> s_fonts;
    if (const auto cIt = s_fonts.find(acName); cIt != s_fonts.end())
        return cIt->second ? &*cIt->second : nullptr;
    if (!s_body && !s_gameData.empty())
    {
        std::ifstream loose(s_gameData / L"Interface" / L"fonts_en.swf", std::ios::binary);
        std::vector<uint8_t> library{std::istreambuf_iterator<char>(loose), std::istreambuf_iterator<char>()};
        if (library.empty())
            library = ReadFromArchive(s_gameData / L"Skyrim - Interface.bsa", "interface", "fonts_en.swf");
        s_body = FlashBody(library);
    }
    auto& font = s_fonts[std::string(acName)];
    if (s_body)
        font = ReadFont(*s_body, acName);
    return font ? &*font : nullptr;
}

// A blank 32-bit picture for GDI to draw into.
class Canvas
{
public:
    Canvas(const int aWidth, const int aHeight) noexcept
        : m_width(aWidth)
        , m_height(aHeight)
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

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool IsValid() const noexcept { return m_oldBitmap != nullptr; }
    HDC Dc() const noexcept { return m_hdc; }
    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }
    // BGRA; antialiasing greys every channel alike.
    const uint8_t* Bits() const noexcept { return static_cast<const uint8_t*>(m_pBits); }

private:
    int m_width;
    int m_height;
    HDC m_hdc{};
    HBITMAP m_hBitmap{};
    HGDIOBJ m_oldBitmap{};
    void* m_pBits{};
};

// GDI pen and brush of one grey, selected while it lives.
class Ink
{
public:
    Ink(HDC aDc, const uint8_t aShade, const int aPenWidth) noexcept
        : m_dc(aDc)
        , m_brush(CreateSolidBrush(RGB(aShade, aShade, aShade)))
        , m_pen(aPenWidth > 0 ? CreatePen(PS_SOLID, aPenWidth, RGB(aShade, aShade, aShade)) : nullptr)
    {
        m_oldBrush = SelectObject(m_dc, m_brush);
        m_oldPen = SelectObject(m_dc, m_pen ? static_cast<HGDIOBJ>(m_pen) : GetStockObject(NULL_PEN));
    }

    ~Ink()
    {
        SelectObject(m_dc, m_oldBrush);
        SelectObject(m_dc, m_oldPen);
        DeleteObject(m_brush);
        if (m_pen)
            DeleteObject(m_pen);
    }

    Ink(const Ink&) = delete;
    Ink& operator=(const Ink&) = delete;

private:
    HDC m_dc;
    HBRUSH m_brush;
    HPEN m_pen;
    HGDIOBJ m_oldBrush{};
    HGDIOBJ m_oldPen{};
};

struct TextLayout
{
    std::vector<const Glyph*> Glyphs;
    // How far each letter moves the pen, its spacing included.
    std::vector<float> Steps;
    float Width{};
    float Top{};
    float Bottom{};
};

// The letters' advances and how far their outlines reach above and below the baseline; empty when the font lacks a letter.
// aWordGap scales the space between words.
std::optional<TextLayout> Lay(const Font& acFont, const std::wstring& acText, const float aScale, const float aSpacing, const float aWordGap) noexcept
{
    TextLayout layout;
    layout.Top = 1e9f;
    layout.Bottom = -1e9f;
    for (const wchar_t cLetter : acText)
    {
        const auto cIt = acFont.Glyphs.find(cLetter);
        if (cIt == acFont.Glyphs.end())
            return std::nullopt;
        layout.Glyphs.push_back(&cIt->second);
        const float cStep = cLetter == L' ' ? (cIt->second.Advance * aScale + aSpacing) * aWordGap : cIt->second.Advance * aScale + aSpacing;
        layout.Steps.push_back(cStep);
        layout.Width += cStep;
        for (const auto& cContour : cIt->second.Contours)
            for (const glm::vec2& cPoint : cContour)
            {
                layout.Top = std::min(layout.Top, cPoint.y * aScale);
                layout.Bottom = std::max(layout.Bottom, cPoint.y * aScale);
            }
    }
    layout.Width -= aSpacing;
    if (layout.Top > layout.Bottom)
        layout.Top = layout.Bottom = 0.f;
    return layout;
}

// Writes a line of text centred on (aCenterX, aCenterY) in picture pixels, its ink centred too.
// aHeight is the font's size in pixels; aSpacing is added after each letter; aWordGap scales the space between words. Georgia writes what the font cannot.
// Returns how wide it came out.
float Write(const Canvas& acCanvas, const Font* apFont, const std::wstring& acText, const float aHeight, const float aCenterX, const float aCenterY,
            const float aSpacing, const uint8_t aShade, const bool aBold, const float aWordGap = 1.f) noexcept
{
    const float cScale = aHeight / 1024.f;
    std::optional<TextLayout> layout;
    if (apFont)
        layout = Lay(*apFont, acText, cScale, aSpacing, aWordGap);

    if (!layout)
    {
        // Georgia, the nearest Windows face to the game's book letters, at the same size.
        HFONT hFont = CreateFontW(-static_cast<int>(aHeight * kSupersample), 0, 0, 0, aBold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_ROMAN, L"Georgia");
        HGDIOBJ oldFont = SelectObject(acCanvas.Dc(), hFont);
        SetBkMode(acCanvas.Dc(), TRANSPARENT);
        SetTextColor(acCanvas.Dc(), RGB(aShade, aShade, aShade));
        SetTextCharacterExtra(acCanvas.Dc(), static_cast<int>(aSpacing * kSupersample));
        SIZE size{};
        GetTextExtentPoint32W(acCanvas.Dc(), acText.c_str(), static_cast<int>(acText.size()), &size);
        TEXTMETRICW metrics{};
        GetTextMetricsW(acCanvas.Dc(), &metrics);
        const int cX = static_cast<int>(aCenterX * kSupersample) - size.cx / 2;
        // Capitals stand between the baseline and the cap height, about 70% of the ascent.
        const int cBaseline = static_cast<int>(aCenterY * kSupersample + metrics.tmAscent * 0.35f);
        TextOutW(acCanvas.Dc(), cX, cBaseline - metrics.tmAscent, acText.c_str(), static_cast<int>(acText.size()));
        SelectObject(acCanvas.Dc(), oldFont);
        DeleteObject(hFont);
        return static_cast<float>(size.cx) / kSupersample;
    }

    Ink ink(acCanvas.Dc(), aShade, 0);
    SetPolyFillMode(acCanvas.Dc(), ALTERNATE);
    float x = aCenterX - layout->Width * 0.5f;
    const float cBaseline = aCenterY - (layout->Top + layout->Bottom) * 0.5f;
    std::vector<POINT> points;
    std::vector<INT> counts;
    for (size_t i = 0; i < layout->Glyphs.size(); ++i)
    {
        const Glyph* pGlyph = layout->Glyphs[i];
        points.clear();
        counts.clear();
        for (const auto& cContour : pGlyph->Contours)
        {
            for (const glm::vec2& cPoint : cContour)
                points.push_back({std::lround((x + cPoint.x * cScale) * kSupersample), std::lround((cBaseline + cPoint.y * cScale) * kSupersample)});
            counts.push_back(static_cast<INT>(cContour.size()));
        }
        if (!counts.empty())
            PolyPolygon(acCanvas.Dc(), points.data(), counts.data(), static_cast<int>(counts.size()));
        x += layout->Steps[i];
    }
    return layout->Width;
}

// How wide a line of text comes out, without writing it.
float Measure(const Font* apFont, const std::wstring& acText, const float aHeight, const float aSpacing, const float aWordGap = 1.f) noexcept
{
    if (apFont)
        if (const auto cLayout = Lay(*apFont, acText, aHeight / 1024.f, aSpacing, aWordGap))
            return cLayout->Width;
    // Georgia's capitals are about 0.72 of the size wide.
    return static_cast<float>(acText.size()) * (aHeight * 0.72f + aSpacing);
}

POINT At(const float aX, const float aY) noexcept
{
    return {std::lround(aX * kSupersample), std::lround(aY * kSupersample)};
}

// A thin rule with a small diamond at each end.
void Rule(const Canvas& acCanvas, const float aLeft, const float aRight, const float aY, const float aThickness, const uint8_t aShade) noexcept
{
    Ink ink(acCanvas.Dc(), aShade, 0);
    const POINT cLine[] = {At(aLeft, aY - aThickness * 0.5f), At(aRight, aY - aThickness * 0.5f), At(aRight, aY + aThickness * 0.5f), At(aLeft, aY + aThickness * 0.5f)};
    Polygon(acCanvas.Dc(), cLine, 4);
    constexpr float cDiamond = 6.f;
    for (const float cX : {aLeft, aRight})
    {
        const POINT cTip[] = {At(cX - cDiamond, aY), At(cX, aY - cDiamond), At(cX + cDiamond, aY), At(cX, aY + cDiamond)};
        Polygon(acCanvas.Dc(), cTip, 4);
    }
}

// The emblem over the title: a ring around a diamond, with a short stroke and a bead above.
void Emblem(const Canvas& acCanvas, const float aX, const float aY, const uint8_t aShade) noexcept
{
    constexpr float cRing = 30.f;
    constexpr float cDiamond = 19.f;
    {
        Ink ring(acCanvas.Dc(), aShade, 3 * kSupersample);
        SelectObject(acCanvas.Dc(), GetStockObject(NULL_BRUSH));
        const POINT cTopLeft = At(aX - cRing, aY - cRing);
        const POINT cBottomRight = At(aX + cRing, aY + cRing);
        Ellipse(acCanvas.Dc(), cTopLeft.x, cTopLeft.y, cBottomRight.x, cBottomRight.y);
        const POINT cDiamondPoints[] = {At(aX, aY - cDiamond), At(aX + cDiamond, aY), At(aX, aY + cDiamond), At(aX - cDiamond, aY)};
        Polygon(acCanvas.Dc(), cDiamondPoints, 4);
        const POINT cStroke[] = {At(aX, aY - cRing - 4.f), At(aX, aY - cRing - 22.f)};
        Polyline(acCanvas.Dc(), cStroke, 2);
    }
    Ink bead(acCanvas.Dc(), aShade, 0);
    const POINT cBeadTopLeft = At(aX - 5.f, aY - cRing - 34.f);
    const POINT cBeadBottomRight = At(aX + 5.f, aY - cRing - 24.f);
    Ellipse(acCanvas.Dc(), cBeadTopLeft.x, cBeadTopLeft.y, cBeadBottomRight.x, cBeadBottomRight.y);
    // A small diamond at the ring's heart.
    const POINT cHeart[] = {At(aX, aY - 6.f), At(aX + 6.f, aY), At(aX, aY + 6.f), At(aX - 6.f, aY)};
    Polygon(acCanvas.Dc(), cHeart, 4);
}

// How much of each picture pixel the drawing covers, from 0 to 1: the supersampled canvas averaged down.
std::vector<float> Cover(const Canvas& acCanvas, const int aWidth, const int aHeight) noexcept
{
    GdiFlush();
    std::vector<float> cover(static_cast<size_t>(aWidth) * aHeight);
    constexpr float cFull = 255.f * kSupersample * kSupersample;
    for (int y = 0; y < aHeight; ++y)
        for (int x = 0; x < aWidth; ++x)
        {
            uint32_t sum = 0;
            for (int dy = 0; dy < kSupersample; ++dy)
                for (int dx = 0; dx < kSupersample; ++dx)
                    sum += acCanvas.Bits()[((static_cast<size_t>(y) * kSupersample + dy) * acCanvas.Width() + x * kSupersample + dx) * 4 + 2];
            cover[static_cast<size_t>(y) * aWidth + x] = sum / cFull;
        }
    return cover;
}

// The colour over a dark band that fades out towards the picture's edges.
Picture Colour(const std::vector<float>& acCover, const int aWidth, const int aHeight, const uint32_t aRgb, const float aBand) noexcept
{
    Picture picture{aWidth, aHeight, std::vector<uint8_t>(static_cast<size_t>(aWidth) * aHeight * 4, 0)};
    const float cRed = static_cast<float>((aRgb >> 16) & 0xFF);
    const float cGreen = static_cast<float>((aRgb >> 8) & 0xFF);
    const float cBlue = static_cast<float>(aRgb & 0xFF);
    for (int y = 0; y < aHeight; ++y)
    {
        const float cAcross = 1.f - std::abs(static_cast<float>(y) - aHeight * 0.5f) / (aHeight * 0.5f);
        for (int x = 0; x < aWidth; ++x)
        {
            const size_t cAt = (static_cast<size_t>(y) * aWidth + x) * 4;
            const float cCover = acCover[static_cast<size_t>(y) * aWidth + x];
            const float cAlong = std::min(static_cast<float>(std::min(x, aWidth - 1 - x)) / (aWidth * 0.3f), 1.f);
            const float cBand = aBand * Smooth(cAcross * 1.6f) * Smooth(cAlong);
            const float cAlpha = cCover + cBand * (1.f - cCover);
            // The colour that, at this alpha over the game, shows the letter's colour where it covers and black elsewhere.
            const float cShare = cAlpha > 0.f ? cCover / cAlpha : 0.f;
            picture.Rgba[cAt + 0] = static_cast<uint8_t>(cRed * cShare);
            picture.Rgba[cAt + 1] = static_cast<uint8_t>(cGreen * cShare);
            picture.Rgba[cAt + 2] = static_cast<uint8_t>(cBlue * cShare);
            picture.Rgba[cAt + 3] = static_cast<uint8_t>(cAlpha * 255.f);
        }
    }
    return picture;
}

void PutBigEndian(std::vector<uint8_t>& aOut, const uint32_t aValue) noexcept
{
    for (int shift = 24; shift >= 0; shift -= 8)
        aOut.push_back(static_cast<uint8_t>(aValue >> shift));
}

void PutChunk(std::vector<uint8_t>& aOut, const char* acpType, const std::vector<uint8_t>& acData) noexcept
{
    PutBigEndian(aOut, static_cast<uint32_t>(acData.size()));
    const size_t cTypeAt = aOut.size();
    aOut.insert(aOut.end(), acpType, acpType + 4);
    aOut.insert(aOut.end(), acData.begin(), acData.end());
    PutBigEndian(aOut, static_cast<uint32_t>(crc32(0, aOut.data() + cTypeAt, static_cast<uInt>(aOut.size() - cTypeAt))));
}
} // namespace

void SetGameData(const std::filesystem::path& acData) noexcept
{
    std::scoped_lock lock(s_fontLock);
    s_gameData = acData;
}

Picture Line(const std::wstring& acText, const uint32_t aRgb, const int aTextHeight) noexcept
{
    Canvas canvas(kWidth * kSupersample, kLineHeight * kSupersample);
    if (!canvas.IsValid())
        return {};
    const float cHeight = static_cast<float>(aTextHeight * kWidth / 1024);
    Write(canvas, FindFont(kHandwriting), acText, cHeight, kWidth * 0.5f, kLineHeight * 0.5f, cHeight / 8.f, 255, false);
    return Colour(Cover(canvas, kWidth, kLineHeight), kWidth, kLineHeight, aRgb, kLineBand);
}

Picture DeathCard(const std::wstring& acTitle, const std::wstring& acSubtitle, const uint32_t aRgb) noexcept
{
    Canvas canvas(kWidth * kSupersample, kCardHeight * kSupersample);
    if (!canvas.IsValid())
        return {};

    const bool cHasSubtitle = !acSubtitle.empty();
    // Without the subtitle the rest moves down, to stay in the middle of the card.
    const float cShift = cHasSubtitle ? 0.f : 45.f;
    constexpr float cCenter = kWidth * 0.5f;

    constexpr float cTitleHeight = 230.f;
    // The title's letters are spread apart; its words stay closer than that would put them.
    constexpr float cTitleWordGap = 0.6f;
    const float cTitleWidth = Measure(FindFont(kTitleFont), acTitle, cTitleHeight, cTitleHeight * 0.08f, cTitleWordGap);
    const float cRuleHalf = std::clamp(cTitleWidth * 0.5f + 140.f, 480.f, 900.f);

    Emblem(canvas, cCenter, 82.f + cShift, 235);
    // The top rule leaves the emblem room.
    Rule(canvas, cCenter - cRuleHalf, cCenter - 52.f, 138.f + cShift, 2.5f, 215);
    Rule(canvas, cCenter + 52.f, cCenter + cRuleHalf, 138.f + cShift, 2.5f, 215);
    Write(canvas, FindFont(kTitleFont), acTitle, cTitleHeight, cCenter, 272.f + cShift, cTitleHeight * 0.08f, 255, false, cTitleWordGap);
    Rule(canvas, cCenter - cRuleHalf, cCenter + cRuleHalf, 404.f + cShift, 2.5f, 215);

    if (cHasSubtitle)
    {
        // Spaced capitals, made smaller (then tighter) until the line fits between the rules' ends.
        const Font* pFont = FindFont(kSubtitleFont);
        const float cRoom = cRuleHalf * 2.f - 40.f;
        float height = 56.f;
        float spacing = height * 0.32f;
        while (Measure(pFont, acSubtitle, height, spacing) > cRoom && height > 32.f)
        {
            height -= 2.f;
            spacing = height * 0.32f;
        }
        while (Measure(pFont, acSubtitle, height, spacing) > cRoom && spacing > 0.f)
            spacing = std::max(0.f, spacing - 1.f);
        Write(canvas, pFont, acSubtitle, height, cCenter, 462.f, spacing, 235, false);
        Rule(canvas, cCenter - cRuleHalf * 0.8f, cCenter + cRuleHalf * 0.8f, 520.f, 2.f, 190);
    }

    return Colour(Cover(canvas, kWidth, kCardHeight), kWidth, kCardHeight, aRgb, kCardBand);
}

std::string FontReport() noexcept
{
    std::string report;
    for (const std::string_view cName : {kHandwriting, kTitleFont, kSubtitleFont})
    {
        const Font* pFont = FindFont(cName);
        report += std::string(report.empty() ? "" : ", ") + std::string(cName) + (pFont ? " (" + std::to_string(pFont->Glyphs.size()) + " letters)" : " (missing: Georgia instead)");
    }
    return report;
}

bool WritePng(const std::filesystem::path& acPath, const Picture& acPicture) noexcept
{
    if (acPicture.Rgba.size() != static_cast<size_t>(acPicture.Width) * acPicture.Height * 4)
        return false;

    // Rows of RGB over a dark grey ground.
    constexpr float cGround[3] = {38.f, 38.f, 42.f};
    std::vector<uint8_t> rows;
    rows.reserve(static_cast<size_t>(acPicture.Height) * (1 + acPicture.Width * 3));
    for (int y = 0; y < acPicture.Height; ++y)
    {
        rows.push_back(0); // no filter
        for (int x = 0; x < acPicture.Width; ++x)
        {
            const size_t cAt = (static_cast<size_t>(y) * acPicture.Width + x) * 4;
            const float cAlpha = acPicture.Rgba[cAt + 3] / 255.f;
            for (int c = 0; c < 3; ++c)
                rows.push_back(static_cast<uint8_t>(std::clamp(acPicture.Rgba[cAt + c] * cAlpha + cGround[c] * (1.f - cAlpha), 0.f, 255.f)));
        }
    }
    uLongf packedSize = compressBound(static_cast<uLong>(rows.size()));
    std::vector<uint8_t> packed(packedSize);
    if (compress2(packed.data(), &packedSize, rows.data(), static_cast<uLong>(rows.size()), 6) != Z_OK)
        return false;
    packed.resize(packedSize);

    std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> header;
    PutBigEndian(header, static_cast<uint32_t>(acPicture.Width));
    PutBigEndian(header, static_cast<uint32_t>(acPicture.Height));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8 bits, RGB
    PutChunk(file, "IHDR", header);
    PutChunk(file, "IDAT", packed);
    PutChunk(file, "IEND", {});

    std::ofstream out(acPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}
} // namespace BannerPicture
