#include "pch.h"

#include "antares/script_capture.h"

#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

namespace antares::script_capture {
    bool Save_Argb_Png(const char* path, const uint32_t* pixels, int32_t width, int32_t height, int32_t stride_pixels) {
        if (path == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        std::vector<uint8_t> rgb((size_t)width * (size_t)height * 3);
        for (int32_t y = 0; y < height; ++y) {
            const uint32_t* row = pixels + (size_t)y * (size_t)stride_pixels;
            uint8_t* out = rgb.data() + (size_t)y * (size_t)width * 3;
            for (int32_t x = 0; x < width; ++x) {
                const uint32_t argb = row[x];
                out[x * 3 + 0] = (uint8_t)(argb >> 16);
                out[x * 3 + 1] = (uint8_t)(argb >> 8);
                out[x * 3 + 2] = (uint8_t)argb;
            }
        }
        // Fast compression: the suite writes dozens of 2560x1920 frames per run.
        stbi_write_png_compression_level = 1;
        return stbi_write_png(path, width, height, 3, rgb.data(), width * 3) != 0;
    }
} // namespace antares::script_capture
