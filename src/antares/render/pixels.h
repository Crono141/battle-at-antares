#pragma once

// The HD pixel format and its colour operations (shared by the render modules).

namespace render {

    // ------------------------------------------------------------------ HD pixels
    //
    // Palette form: foreground index (bits 0-7) over background index (bits 8-15) with
    // coverage (bits 16-23); HD_CLEAR marks a transparent background (inside images).
    // Literal form: HD_LITERAL | RGB.

    constexpr int32_t S = SCALE;
    constexpr uint32_t HD_LITERAL = 0x80000000u;
    constexpr uint32_t HD_CLEAR = 0x01000000u;
    constexpr uint16_t SHADOW_NONE = 0x100;

    inline uint32_t Indexed(uint8_t index) {
        return index | (255u << 16);
    }

    inline uint32_t Literal(uint32_t rgb) {
        return HD_LITERAL | (rgb & 0xFFFFFFu);
    }

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

    inline uint32_t Base_Color(uint32_t pixel) {
        if ((pixel & HD_LITERAL) != 0) {
            return pixel & 0xFFFFFFu;
        }
        const uint32_t alpha = (pixel >> 16) & 0xFF;
        const uint32_t fg = Base_Rgb((uint8_t)pixel);
        if (alpha >= 255) {
            return fg;
        }
        const uint32_t bg = (pixel & HD_CLEAR) != 0 ? 0u : Base_Rgb((uint8_t)(pixel >> 8));
        return Mix(fg, bg, alpha);
    }

    // Palette index `index` with coverage `alpha` over `dest`.
    inline uint32_t Over_Index(uint32_t dest, uint8_t index, uint32_t alpha) {
        if (alpha >= 255) {
            return Indexed(index);
        }
        if ((dest & HD_LITERAL) != 0) {
            return Literal(Mix(Base_Rgb(index), dest, alpha));
        }
        // One level of blending is kept: the destination's dominant colour becomes the
        // background, or its own background when it is mostly uncovered.
        if (((dest >> 16) & 0xFF) >= 128) {
            return index | ((dest & 0xFF) << 8) | (alpha << 16);
        }
        return index | (dest & 0xFF00u) | (alpha << 16) | (dest & HD_CLEAR);
    }

    inline uint32_t Over_Rgb(uint32_t dest, uint32_t rgb, uint32_t alpha) {
        if (alpha >= 255) {
            return Literal(rgb);
        }
        return Literal(Mix(rgb, Base_Color(dest), alpha));
    }

    inline uint32_t Luma_Gray(uint32_t rgb) {
        const uint32_t luma = (((rgb >> 16) & 0xFF) * 77u + ((rgb >> 8) & 0xFF) * 150u + (rgb & 0xFF) * 29u) >> 8;
        return (luma << 16) | (luma << 8) | luma;
    }

    inline uint32_t Chroma(uint32_t rgb) {
        const uint32_t r = (rgb >> 16) & 0xFF;
        const uint32_t g = (rgb >> 8) & 0xFF;
        const uint32_t b = rgb & 0xFF;
        return SDL_max(r, SDL_max(g, b)) - SDL_min(r, SDL_min(g, b));
    }

    // `rgb` seen through glass of level `level`, in true colour.
    inline uint32_t Glass_Rgb(uint32_t rgb, int32_t level) {
        const auto& glass = video::glass_colors[level & 15];
        if (glass.intensity == 0) {
            return rgb;
        }
        const uint32_t tint = ((uint32_t)((glass.r & 0x3F) * 255 / 63) << 16) |
            ((uint32_t)((glass.g & 0x3F) * 255 / 63) << 8) | (uint32_t)((glass.b & 0x3F) * 255 / 63);
        const uint32_t strength = glass.intensity >= 100 ? 255u : (uint32_t)glass.intensity * 255u / 100u;
        // The game's glass tables land on the palette's tinted ramps, so saturated details
        // (red lights in the colony art) lose their colour under the glass rather than
        // showing through darkened. Desaturate by the glass strength first.
        return Mix(tint, Mix(Luma_Gray(rgb), rgb, strength), strength);
    }

    // Glass of level `level` with coverage `alpha` over `dest`. Palette-based pixels go through
    // the game's own glass table (exactly what the original pixel gets, so e.g. bright lights
    // under dark glass turn dark as in the original); full-colour pixels and partial
    // coverage are tinted in true colour, desaturated by the glass strength.
    inline uint32_t Glass_Tint(uint32_t dest, int32_t level, uint32_t alpha) {
        if (alpha >= 255 && (dest & HD_LITERAL) == 0) {
            const uint8_t* table = video::glass_remap_colors[level & 15];
            const uint32_t cover = (dest >> 16) & 0xFF;
            if (cover >= 255 || (dest & HD_CLEAR) != 0) {
                // HD detail the 1x page never had (a red light inside the art of one page
                // pixel) keeps its colour through the table, while the original only shows
                // the glass's own hue there. Much more colour than the glass gives grey: keep
                // just the detail's brightness, tinted.
                const uint32_t gray = Luma_Gray(Base_Rgb((uint8_t)dest));
                const uint32_t through_glass = Glass_Rgb(gray, level);
                if (cover >= 255 && Chroma(Base_Rgb(table[dest & 0xFF])) > Chroma(through_glass) + 48) {
                    return Literal(through_glass);
                }
                return (dest & 0xFFFF0000u) | ((uint32_t)table[(dest >> 8) & 0xFF] << 8) | table[dest & 0xFF];
            }
            // An edge blended over another colour: that colour never reached the original's
            // glass table (the page pixel is the edge's), and the table can send it somewhere
            // bright, e.g. a red light under the build list's frame. Only its brightness is
            // kept, tinted in true colour.
            return Literal(Mix(Base_Rgb(table[dest & 0xFF]), Glass_Rgb(Luma_Gray(Base_Rgb((uint8_t)(dest >> 8))), level), cover));
        }
        const uint32_t under = Base_Color(dest);
        const uint32_t tinted = Glass_Rgb(under, level);
        return Literal(alpha >= 255 ? tinted : Mix(tinted, under, alpha));
    }

} // namespace render
