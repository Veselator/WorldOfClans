// ImageIO.h - PNG loading and saving for map layers and sprite sheets.
#pragma once

#include "Types.h"

namespace woc
{
    struct ImageData
    {
        std::vector<u8> pixels;    // always tightly packed, `channels` bytes per texel
        u32 width = 0;
        u32 height = 0;
        u32 channels = 4;

        bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }

        const u8* At(u32 x, u32 y) const
        {
            return pixels.data() + (static_cast<size_t>(y) * width + x) * channels;
        }
        u8* At(u32 x, u32 y)
        {
            return pixels.data() + (static_cast<size_t>(y) * width + x) * channels;
        }
    };

    /// Loads a PNG (or any format stb_image handles) forcing `desiredChannels` if non-zero.
    bool LoadImage(const std::string& path, ImageData& out, u32 desiredChannels = 4);

    /// Writes an 8-bit-per-channel PNG. Used by the map editor when saving height maps.
    bool SaveImagePNG(const std::string& path, const ImageData& image);
}
