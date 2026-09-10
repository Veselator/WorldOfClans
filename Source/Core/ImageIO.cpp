#include "ImageIO.h"
#include "Log.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace woc
{
    bool LoadImage(const std::string& path, ImageData& out, u32 desiredChannels)
    {
        int width = 0, height = 0, channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, static_cast<int>(desiredChannels));
        if (!data)
        {
            WOC_LOG_ERROR("Failed to load image '", path, "': ", stbi_failure_reason());
            return false;
        }

        out.width = static_cast<u32>(width);
        out.height = static_cast<u32>(height);
        out.channels = desiredChannels != 0 ? desiredChannels : static_cast<u32>(channels);

        const size_t byteCount = static_cast<size_t>(out.width) * out.height * out.channels;
        out.pixels.assign(data, data + byteCount);
        stbi_image_free(data);

        WOC_LOG_TRACE("Loaded image ", path, " (", out.width, "x", out.height, ", ", out.channels, " ch)");
        return true;
    }

    bool SaveImagePNG(const std::string& path, const ImageData& image)
    {
        if (!image.IsValid()) return false;
        const int stride = static_cast<int>(image.width * image.channels);
        const int result = stbi_write_png(path.c_str(), static_cast<int>(image.width),
                                          static_cast<int>(image.height), static_cast<int>(image.channels),
                                          image.pixels.data(), stride);
        if (result == 0) WOC_LOG_ERROR("Failed to write image ", path);
        return result != 0;
    }
}
