// FontAtlas.h - rasterises Consolas into a single-channel texture atlas.
//
// The game's typeface is fixed (Consolas, as specified), so the atlas is baked once at
// start-up straight from the system font through GDI. Latin and Cyrillic ranges are
// included because the interface text is authored in Russian.
#pragma once

#include "../Core/Types.h"
#include "../Core/Math.h"
#include <unordered_map>

namespace woc
{
    struct Glyph
    {
        Vec2 uvMin;
        Vec2 uvMax;
        f32 width = 0.0f;
        f32 height = 0.0f;
        f32 bearingX = 0.0f;
        f32 bearingY = 0.0f;   // distance from the baseline up to the glyph top
        f32 advance = 0.0f;
    };

    class FontAtlas
    {
    public:
        /// Rasterises the atlas. Returns false if the font could not be created.
        bool Build(const std::string& fontName, i32 pixelHeight, bool bold);

        const Glyph* Find(u32 codepoint) const;

        const std::vector<u8>& Pixels() const { return m_pixels; }
        u32 Width() const { return m_width; }
        u32 Height() const { return m_height; }
        f32 LineHeight() const { return m_lineHeight; }
        f32 Ascent() const { return m_ascent; }
        /// Consolas is monospaced, so a single advance describes the whole face.
        f32 CellWidth() const { return m_cellWidth; }

        /// Width in pixels of a UTF-8 string at scale 1.
        f32 MeasureWidth(const std::string& utf8) const;

        /// Decodes the next code point, advancing `index`.
        static u32 DecodeUtf8(const std::string& text, size_t& index);

    private:
        std::unordered_map<u32, Glyph> m_glyphs;
        std::vector<u8> m_pixels;
        u32 m_width = 0;
        u32 m_height = 0;
        f32 m_lineHeight = 0.0f;
        f32 m_ascent = 0.0f;
        f32 m_cellWidth = 0.0f;
    };
}
