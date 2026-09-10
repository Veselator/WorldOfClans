#include "FontAtlas.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace woc
{
    namespace
    {
        struct CodepointRange { u32 first, last; };

        // Basic Latin plus the Cyrillic block the interface actually uses.
        constexpr CodepointRange kRanges[] = {
            { 0x0020, 0x007E },
            { 0x00A0, 0x00FF },
            { 0x0400, 0x045F },
            { 0x2010, 0x2015 },   // dashes
            { 0x2018, 0x201F },   // quotes
            { 0x2190, 0x2193 },   // arrows used by the UI
            { 0x2022, 0x2022 },   // bullet
        };
    }

    u32 FontAtlas::DecodeUtf8(const std::string& text, size_t& index)
    {
        if (index >= text.size()) return 0;

        const auto byte = static_cast<u8>(text[index]);
        if (byte < 0x80) { ++index; return byte; }

        u32 codepoint = 0;
        int extra = 0;
        if ((byte & 0xE0) == 0xC0) { codepoint = byte & 0x1Fu; extra = 1; }
        else if ((byte & 0xF0) == 0xE0) { codepoint = byte & 0x0Fu; extra = 2; }
        else if ((byte & 0xF8) == 0xF0) { codepoint = byte & 0x07u; extra = 3; }
        else { ++index; return '?'; }

        ++index;
        for (int i = 0; i < extra && index < text.size(); ++i, ++index)
        {
            codepoint = (codepoint << 6) | (static_cast<u8>(text[index]) & 0x3Fu);
        }
        return codepoint;
    }

    const Glyph* FontAtlas::Find(u32 codepoint) const
    {
        const auto it = m_glyphs.find(codepoint);
        if (it != m_glyphs.end()) return &it->second;
        const auto fallback = m_glyphs.find('?');
        return fallback != m_glyphs.end() ? &fallback->second : nullptr;
    }

    f32 FontAtlas::MeasureWidth(const std::string& utf8) const
    {
        f32 width = 0.0f;
        size_t index = 0;
        while (index < utf8.size())
        {
            const u32 codepoint = DecodeUtf8(utf8, index);
            if (codepoint == '\n') continue;
            if (const Glyph* glyph = Find(codepoint)) width += glyph->advance;
        }
        return width;
    }

#ifdef _WIN32
    bool FontAtlas::Build(const std::string& fontName, i32 pixelHeight, bool bold)
    {
        const std::wstring wideName(fontName.begin(), fontName.end());

        HDC screenDC = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screenDC);
        ReleaseDC(nullptr, screenDC);
        if (!dc) return false;

        HFONT font = CreateFontW(
            -pixelHeight, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            FIXED_PITCH | FF_MODERN, wideName.c_str());
        if (!font) { DeleteDC(dc); return false; }

        HGDIOBJ previous = SelectObject(dc, font);

        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        m_ascent = static_cast<f32>(metrics.tmAscent);
        m_lineHeight = static_cast<f32>(metrics.tmHeight + metrics.tmExternalLeading);
        m_cellWidth = static_cast<f32>(metrics.tmAveCharWidth);

        // A square atlas sized from the glyph count keeps everything in one texture.
        u32 glyphCount = 0;
        for (const CodepointRange& range : kRanges) glyphCount += range.last - range.first + 1;

        const u32 cell = static_cast<u32>(pixelHeight) + 4;
        u32 columns = 1;
        while (columns * columns < glyphCount) ++columns;
        m_width = 1;
        while (m_width < columns * cell) m_width <<= 1;
        m_height = m_width;

        m_pixels.assign(static_cast<size_t>(m_width) * m_height, 0);
        m_glyphs.clear();

        const MAT2 identity{ {0,1},{0,0},{0,0},{0,1} };
        u32 penX = 1, penY = 1, rowHeight = 0;
        std::vector<u8> buffer;

        for (const CodepointRange& range : kRanges)
        {
            for (u32 codepoint = range.first; codepoint <= range.last; ++codepoint)
            {
                GLYPHMETRICS gm{};
                const DWORD size = GetGlyphOutlineW(dc, codepoint, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &identity);
                if (size == GDI_ERROR) continue;

                Glyph glyph{};
                glyph.advance = static_cast<f32>(gm.gmCellIncX);
                glyph.bearingX = static_cast<f32>(gm.gmptGlyphOrigin.x);
                glyph.bearingY = static_cast<f32>(gm.gmptGlyphOrigin.y);
                glyph.width = static_cast<f32>(gm.gmBlackBoxX);
                glyph.height = static_cast<f32>(gm.gmBlackBoxY);

                if (size == 0 || gm.gmBlackBoxX == 0 || gm.gmBlackBoxY == 0)
                {
                    // Whitespace: no pixels, but the advance still matters.
                    glyph.width = glyph.height = 0.0f;
                    m_glyphs.emplace(codepoint, glyph);
                    continue;
                }

                buffer.resize(size);
                if (GetGlyphOutlineW(dc, codepoint, GGO_GRAY8_BITMAP, &gm, size, buffer.data(), &identity) == GDI_ERROR)
                    continue;

                const u32 bw = gm.gmBlackBoxX;
                const u32 bh = gm.gmBlackBoxY;
                const u32 pitch = (bw + 3) & ~3u;   // GDI pads rows to 4 bytes

                if (penX + bw + 1 >= m_width)
                {
                    penX = 1;
                    penY += rowHeight + 1;
                    rowHeight = 0;
                }
                if (penY + bh + 1 >= m_height)
                {
                    WOC_LOG_WARN("Font atlas overflow; some glyphs were dropped");
                    break;
                }

                for (u32 y = 0; y < bh; ++y)
                {
                    for (u32 x = 0; x < bw; ++x)
                    {
                        // GGO_GRAY8_BITMAP produces coverage in 0..64.
                        const u32 value = std::min<u32>(buffer[y * pitch + x] * 255u / 64u, 255u);
                        m_pixels[static_cast<size_t>(penY + y) * m_width + (penX + x)] = static_cast<u8>(value);
                    }
                }

                glyph.uvMin = { static_cast<f32>(penX) / m_width, static_cast<f32>(penY) / m_height };
                glyph.uvMax = { static_cast<f32>(penX + bw) / m_width, static_cast<f32>(penY + bh) / m_height };
                m_glyphs.emplace(codepoint, glyph);

                penX += bw + 1;
                rowHeight = std::max(rowHeight, bh);
            }
        }

        SelectObject(dc, previous);
        DeleteObject(font);
        DeleteDC(dc);

        WOC_LOG_INFO("Font atlas '", fontName, "' ", m_width, "x", m_height,
                     " with ", m_glyphs.size(), " glyphs, line height ", m_lineHeight);
        return !m_glyphs.empty();
    }
#else
    bool FontAtlas::Build(const std::string&, i32, bool) { return false; }
#endif
}
