#include "pch.h"

#include "antares/render/internal.h"

namespace render {

#define HD_MAX_GLYPH_FONTS 64

    namespace {

        // HD glyphs for one font size of one font file: hd/<FILE>/<font>/<code>.png, LA with
        // L = colour role (1..8) and A = coverage, at SCALE x the glyph cell.
        struct s_hd_glyph_font {
            std::string directory;
            bool present;
            uint8_t state[256]; // 0 = not tried, 1 = loaded, 2 = missing
            s_hd_frame glyphs[256];
        };

        s_hd_glyph_font* glyph_fonts[HD_MAX_GLYPH_FONTS];
        int32_t glyph_font_count;
        std::unordered_map<std::string, int32_t> glyph_font_by_key;
        uint32_t stats_glyph_missing_art;

        int32_t Find_Glyph_Font(const char* font_file, int16_t font_index) {
            char key[96];
            snprintf(key, sizeof(key), "%s/%d", font_file, (int)font_index);
            for (char* c = key; *c != '\0'; ++c) {
                if (*c >= 'a' && *c <= 'z') {
                    *c = (char)(*c - 'a' + 'A');
                }
            }
            const auto found = glyph_font_by_key.find(key);
            if (found != glyph_font_by_key.end()) {
                return found->second;
            }
            int32_t slot = -1;
            std::string file_key(key, strchr(key, '/') - key);
            if (glyph_font_count < HD_MAX_GLYPH_FONTS &&
                vfs::File_Exists(("hd/" + file_key + "/font.json").c_str()) != 0) {
                s_hd_glyph_font* font = new s_hd_glyph_font();
                font->directory = std::string("hd/") + key;
                font->present = true;
                glyph_fonts[glyph_font_count] = font;
                slot = glyph_font_count++;
            }
            glyph_font_by_key.emplace(key, slot);
            return slot;
        }

    } // namespace

    const s_hd_frame* Glyph_Art(const char* font_file, int16_t font_index, uint8_t code) {
        const int32_t slot = Find_Glyph_Font(font_file, font_index);
        if (slot < 0) {
            if (stats_enabled) {
                Stats_Missing(std::string("font:") + font_file + "/" + std::to_string(font_index));
            }
            return nullptr;
        }
        s_hd_glyph_font* font = glyph_fonts[slot];
        if (font->state[code] == 0) {
            char name[16];
            snprintf(name, sizeof(name), "/%03d.png", (int)code);
            font->state[code] = Load_Frame_Png(font->directory + name, &font->glyphs[code]) ? 1 : 2;
        }
        if (font->state[code] != 1) {
            stats_glyph_missing_art++;
            // Spaces have no pixels and so no art: not worth reporting.
            if (stats_enabled && code != ' ') {
                Stats_Missing(std::string("glyph:") + font_file + "/" + std::to_string(font_index) + "/" + std::to_string(code));
            }
            return nullptr;
        }
        return &font->glyphs[code];
    }

} // namespace render
