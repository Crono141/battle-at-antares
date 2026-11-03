#include "pch.h"

#include "antares/render/art_format.h"

#include <algorithm>
#include <cstring>

namespace render::art {

    int32_t Png_Colour_Type(const uint8_t* bytes, size_t size) {
        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        // Signature, then the IHDR chunk: length (4), "IHDR", width (4), height (4), bit depth,
        // colour type.
        if (bytes == nullptr || size < 26 || memcmp(bytes, signature, 8) != 0 || memcmp(bytes + 12, "IHDR", 4) != 0) {
            return -1;
        }
        return bytes[25];
    }

    bool Is_Full_Colour(const uint8_t* bytes, size_t size) {
        const int32_t type = Png_Colour_Type(bytes, size);
        if (type < 0) {
            return true; // WebP (or anything else SDL can read as colour)
        }
        return type != 0 && type != 4;
    }

    int32_t Infer_Scale(int32_t image_width, int32_t image_height, int32_t base_width, int32_t base_height, bool full_colour) {
        if (image_width <= 0 || image_height <= 0 || base_width <= 0 || base_height <= 0) {
            return 0;
        }
        if (image_width % base_width != 0 || image_height % base_height != 0) {
            return 0;
        }
        const int32_t scale = image_width / base_width;
        if (image_height / base_height != scale) {
            return 0;
        }
        if (scale == 1 || scale == 2 || scale == 4 || (scale == 3 && full_colour)) {
            return scale;
        }
        return 0;
    }

    int32_t Scaled_Size(int32_t size, int32_t scale, int32_t target) {
        if (scale <= 0) {
            return 0;
        }
        return (size * target + scale / 2) / scale;
    }

    std::vector<uint8_t> Resample_Nearest(const uint8_t* pixels, int32_t width, int32_t height, int32_t channels, int32_t to_width, int32_t to_height) {
        std::vector<uint8_t> out((size_t)to_width * to_height * channels);
        if (width <= 0 || height <= 0) {
            return out;
        }
        for (int32_t y = 0; y < to_height; ++y) {
            // The source pixel under the centre of the target pixel.
            const int32_t sy = (int32_t)std::min<int64_t>((((int64_t)y * 2 + 1) * height) / ((int64_t)to_height * 2), (int64_t)height - 1);
            for (int32_t x = 0; x < to_width; ++x) {
                const int32_t sx = (int32_t)std::min<int64_t>((((int64_t)x * 2 + 1) * width) / ((int64_t)to_width * 2), (int64_t)width - 1);
                memcpy(&out[((size_t)y * to_width + x) * channels], &pixels[((size_t)sy * width + sx) * channels], (size_t)channels);
            }
        }
        return out;
    }

    std::vector<uint8_t> Resample_Bilinear_Rgba(const uint8_t* rgba, int32_t width, int32_t height, int32_t to_width, int32_t to_height) {
        std::vector<uint8_t> out((size_t)to_width * to_height * 4);
        if (width <= 0 || height <= 0) {
            return out;
        }
        auto clamp = [](double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; };
        for (int32_t y = 0; y < to_height; ++y) {
            const double fy = clamp((y + 0.5) * height / to_height - 0.5, 0.0, height - 1.0);
            const int32_t y0 = (int32_t)fy;
            const int32_t y1 = y0 + 1 < height ? y0 + 1 : y0;
            const double wy = fy - y0;
            for (int32_t x = 0; x < to_width; ++x) {
                const double fx = clamp((x + 0.5) * width / to_width - 0.5, 0.0, width - 1.0);
                const int32_t x0 = (int32_t)fx;
                const int32_t x1 = x0 + 1 < width ? x0 + 1 : x0;
                const double wx = fx - x0;
                const uint8_t* p[4] = {&rgba[((size_t)y0 * width + x0) * 4], &rgba[((size_t)y0 * width + x1) * 4],
                    &rgba[((size_t)y1 * width + x0) * 4], &rgba[((size_t)y1 * width + x1) * 4]};
                const double w[4] = {(1 - wx) * (1 - wy), wx * (1 - wy), (1 - wx) * wy, wx * wy};
                double alpha = 0;
                double colour[3] = {0, 0, 0};
                for (int32_t k = 0; k < 4; ++k) {
                    const double a = p[k][3] * w[k];
                    alpha += a;
                    for (int32_t c = 0; c < 3; ++c) {
                        colour[c] += p[k][c] * a;
                    }
                }
                uint8_t* o = &out[((size_t)y * to_width + x) * 4];
                for (int32_t c = 0; c < 3; ++c) {
                    o[c] = alpha > 0 ? (uint8_t)clamp(colour[c] / alpha + 0.5, 0.0, 255.0) : 0;
                }
                o[3] = (uint8_t)clamp(alpha + 0.5, 0.0, 255.0);
            }
        }
        return out;
    }

    s_mask_meaning Mask_Meaning(uint8_t value, bool glassed) {
        if (value == 0) {
            return {MASK_NONE, 0};
        }
        if (value >= 0xF0 && glassed) {
            return {MASK_GLASS, (uint8_t)(value - 0xF0)};
        }
        return {MASK_SHADE, value};
    }

} // namespace render::art
