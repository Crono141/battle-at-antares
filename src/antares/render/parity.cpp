#include "pch.h"

#include "antares/render/internal.h"
#include "antares/identity.h"
#include "antares/script_capture.h"

#include <string>

// Parity harness (WP1.2, PLAN.md section 6). With ANTARES_PARITY=1 every presented frame compares
// the HD plane with the 640x480 page it was presented with: each HD pixel's bound palette index
// must be the page index of its block, and resolved through the palette it must give the page
// pixel's colour. Run with no HD art (an empty pack) or with the identity pack, where the 4x
// frame should be the page enlarged.
namespace render {

    namespace {

        // Per page pixel, the worst of its SxS HD pixels.
        enum e_parity : uint8_t {
            PARITY_OK,
            PARITY_LITERAL, // full-colour HD pixel: excluded from parity (D20 `none` pixels)
            PARITY_COLOUR,  // bound index right, colour wrong (partial coverage blends)
            PARITY_INDEX,   // bound index differs from the page (or a transparent HD pixel)
            PARITY_KINDS,
        };

        bool parity_known;
        bool parity_on;
        std::mutex parity_mutex;
        // The last checked frame (presenter thread writes, screenshot reads; same thread today).
        int32_t parity_width;
        int32_t parity_height;
        std::vector<uint8_t> parity_map;
        std::vector<uint8_t> parity_page;
        uint32_t parity_colors[256];
        uint32_t parity_counts[PARITY_KINDS];
        // Totals since start.
        uint32_t frames_checked;
        uint32_t frames_index;
        uint32_t frames_colour;
        uint32_t worst_index;

        e_parity Classify(uint32_t pixel, uint8_t value, const uint32_t* colors) {
            if ((pixel & HD_LITERAL) != 0) {
                return PARITY_LITERAL;
            }
            const uint32_t alpha = (pixel >> 16) & 0xFF;
            const bool clear = (pixel & HD_CLEAR) != 0;
            if (alpha < 128 && clear) {
                return PARITY_INDEX;
            }
            const uint8_t bound = alpha >= 128 ? (uint8_t)pixel : (uint8_t)(pixel >> 8);
            if (bound != value) {
                return PARITY_INDEX;
            }
            uint32_t rgb = colors[pixel & 0xFF];
            if (alpha < 255) {
                rgb = Mix(rgb, clear ? 0u : colors[(pixel >> 8) & 0xFF], alpha);
            }
            return rgb == colors[value] ? PARITY_OK : PARITY_COLOUR;
        }

    } // namespace

    bool Parity_Enabled() {
        if (!parity_known) {
            parity_known = true;
            const char* value = antares::Get_Env("PARITY");
            parity_on = value != nullptr && value[0] != '\0' && value[0] != '0';
        }
        return parity_on && enabled;
    }

    void Parity_Check(const uint8_t* page, const video::s_palette_color* palette) {
        if (!Parity_Enabled() || page == nullptr || palette == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> planes(planes_mutex);
        if (page_present.hd.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(parity_mutex);
        const int32_t width = page_present.width;
        const int32_t height = page_present.height;
        parity_width = width;
        parity_height = height;
        parity_map.assign((size_t)width * height, PARITY_OK);
        parity_page.assign(page, page + (size_t)width * height);
        for (int32_t i = 0; i < 256; ++i) {
            parity_colors[i] = ((uint32_t)palette[i].r << 16) | ((uint32_t)palette[i].g << 8) | palette[i].b;
        }
        memset(parity_counts, 0, sizeof(parity_counts));
        const size_t stride = (size_t)width * S;
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                const size_t i = (size_t)y * width + x;
                const uint32_t* block = &page_present.hd[(size_t)y * S * stride + (size_t)x * S];
                uint8_t worst = PARITY_OK;
                for (int32_t sy = 0; sy < S; ++sy) {
                    for (int32_t sx = 0; sx < S; ++sx) {
                        worst = SDL_max(worst, (uint8_t)Classify(block[sy * stride + sx], page[i], parity_colors));
                    }
                }
                parity_map[i] = worst;
                parity_counts[worst]++;
            }
        }
        frames_checked++;
        frames_index += parity_counts[PARITY_INDEX] != 0 ? 1 : 0;
        frames_colour += parity_counts[PARITY_INDEX] == 0 && parity_counts[PARITY_COLOUR] != 0 ? 1 : 0;
        worst_index = SDL_max(worst_index, parity_counts[PARITY_INDEX]);
        if (frames_checked % 300 == 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "parity: %u frames checked, %u with index mismatches (worst %u page pixels), %u with colour-only mismatches",
                frames_checked,
                frames_index,
                worst_index,
                frames_colour);
        }
    }

    bool Parity_Save(const char* screenshot_path) {
        if (!Parity_Enabled() || screenshot_path == nullptr) {
            return false;
        }
        std::lock_guard<std::mutex> lock(parity_mutex);
        if (parity_map.empty()) {
            return false;
        }
        std::string path = screenshot_path;
        const size_t dot = path.rfind(".png");
        path = (dot == std::string::npos ? path : path.substr(0, dot)) + ".parity.png";
        // The page dimmed, with mismatches marked: red index, yellow colour only, blue full colour.
        static const uint32_t marks[PARITY_KINDS] = {0, 0x3070FF, 0xFFD000, 0xFF2020};
        std::vector<uint32_t> argb(parity_map.size());
        for (size_t i = 0; i < parity_map.size(); ++i) {
            const uint32_t rgb = parity_colors[parity_page[i]];
            argb[i] = 0xFF000000u | (parity_map[i] != PARITY_OK ? marks[parity_map[i]] : (rgb >> 2) & 0x3F3F3Fu);
        }
        const bool saved = antares::script_capture::Save_Argb_Png(path.c_str(), argb.data(), parity_width, parity_height, parity_width);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "parity: %s index=%u colour=%u literal=%u%s", path.c_str(), parity_counts[PARITY_INDEX], parity_counts[PARITY_COLOUR], parity_counts[PARITY_LITERAL], saved ? "" : " FAILED");
        return saved;
    }

} // namespace render
