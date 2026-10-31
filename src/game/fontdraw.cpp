#include "pch.h"

namespace fontdraw {

    void __cdecl Print_Character_No_Alias_ASM(uint8_t* dest, const uint8_t* src, const uint8_t* palette, int32_t height) {
        for (int row = 0; row < height; row++) {
            uint8_t* line_start = dest;
            while (true) {
                uint8_t glyph_byte = *src++;

                if (glyph_byte == 0x80) {
                    break;
                }

                if (glyph_byte < 0x80) {
                    if (glyph_byte != 0) {
                        *dest = palette[glyph_byte - 1];
                        render::Stamp_Run(dest, 1);
                    }
                    dest++;
                } else {
                    dest += (glyph_byte & 0x7F);
                }
            }
            dest = line_start + video::g_screen_res_x;
        }
    }

} // namespace fontdraw