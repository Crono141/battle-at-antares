#pragma once

// Colour helpers shared by the render modules (the plane pixel format is in twin_internal.h).

namespace render {

    constexpr int32_t S = SCALE;

    inline uint32_t Mix(uint32_t over, uint32_t under, uint32_t alpha) {
        const uint32_t inv = 255u - alpha;
        const uint32_t r = (((over >> 16) & 0xFF) * alpha + ((under >> 16) & 0xFF) * inv) / 255u;
        const uint32_t g = (((over >> 8) & 0xFF) * alpha + ((under >> 8) & 0xFF) * inv) / 255u;
        const uint32_t b = ((over & 0xFF) * alpha + (under & 0xFF) * inv) / 255u;
        return (r << 16) | (g << 8) | b;
    }

    // Colour of a palette index in the screen's palette before fading (6-bit VGA values).
    inline uint32_t Base_Rgb(uint8_t index) {
        const s_palette_entry& entry = video::current_palette[index];
        return ((uint32_t)((entry.r & 0x3F) * 255 / 63) << 16) | ((uint32_t)((entry.g & 0x3F) * 255 / 63) << 8) |
            (uint32_t)((entry.b & 0x3F) * 255 / 63);
    }

} // namespace render
