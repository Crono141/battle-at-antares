#include "pch.h"

#include "antares/cosmetic_random.h"

namespace shear {

    uint16_t x_step_8_8;
    uint16_t dst_width;
    uint32_t y_accum_8_8;
    int32_t y_step_8_8;
    uint32_t noise_threshold;
    uint32_t src_width;
    uint32_t src_height;
    uint32_t row_dword_count;
    uint32_t row_dword_counter;
    uint32_t noise_start_index;
    uint32_t jumble_mask;
    uint32_t jumble_direction_toggle;
    int32_t src_row_skip;
    int32_t dst_row_skip;
    uint32_t col_temp;
    uint32_t outline_color;
    uint32_t temp;
    uint8_t* src_bitmap_end;
    uint32_t row_temp;
    uint8_t noise_table[258] = {
        42,
        68,
        35,
        1,
        70,
        25,
        79,
        59,
        63,
        65,
        6,
        46,
        82,
        28,
        62,
        92,
        96,
        43,
        28,
        37,
        92,
        5,
        3,
        54,
        93,
        83,
        22,
        17,
        19,
        96,
        48,
        27,
        72,
        39,
        70,
        13,
        68,
        100,
        36,
        95,
        4,
        12,
        23,
        34,
        74,
        65,
        42,
        12,
        54,
        69,
        48,
        45,
        63,
        58,
        38,
        60,
        24,
        42,
        30,
        79,
        17,
        36,
        91,
        43,
        89,
        7,
        41,
        43,
        65,
        49,
        47,
        6,
        91,
        30,
        71,
        51,
        7,
        2,
        94,
        49,
        30,
        24,
        85,
        55,
        57,
        41,
        67,
        77,
        32,
        9,
        45,
        40,
        27,
        24,
        38,
        39,
        19,
        83,
        30,
        42,
        34,
        16,
        40,
        59,
        5,
        31,
        78,
        7,
        74,
        87,
        22,
        46,
        25,
        73,
        71,
        30,
        78,
        74,
        98,
        13,
        87,
        91,
        62,
        37,
        56,
        68,
        56,
        75,
        32,
        53,
        51,
        51,
        42,
        25,
        67,
        31,
        8,
        92,
        8,
        38,
        58,
        88,
        54,
        84,
        46,
        10,
        10,
        59,
        22,
        89,
        23,
        47,
        7,
        31,
        14,
        69,
        1,
        92,
        63,
        56,
        11,
        60,
        25,
        38,
        49,
        84,
        96,
        42,
        3,
        51,
        92,
        37,
        75,
        21,
        97,
        22,
        49,
        100,
        69,
        85,
        82,
        35,
        54,
        100,
        19,
        39,
        1,
        89,
        28,
        68,
        29,
        94,
        49,
        84,
        8,
        22,
        11,
        18,
        14,
        15,
        10,
        17,
        36,
        52,
        1,
        50,
        20,
        57,
        99,
        4,
        25,
        9,
        45,
        10,
        90,
        3,
        96,
        86,
        94,
        44,
        24,
        88,
        15,
        4,
        49,
        1,
        59,
        19,
        81,
        97,
        99,
        82,
        90,
        99,
        10,
        58,
        73,
        23,
        39,
        93,
        39,
        80,
        91,
        58,
        59,
        92,
        16,
        89,
        57,
        12,
        3,
        35,
        73,
        56,
        29,
        47,
        28,
        79,
    };

    void __cdecl Capture_Screen_Block(uint8_t* src, int32_t width, int32_t height, uint8_t* dst) {
        do {
            memcpy(dst, src, width);
            src += video::g_screen_res_x;
            dst += width;
            height--;
        } while (height != 0);
    }

    void __cdecl Flip_Bitmap_Down_Different(uint8_t* src, int32_t width, int32_t height, uint8_t* dst) {
        uint8_t* d = dst + width - 1;
        int32_t w = width;

        do {
            do {
                *d = *src;
                src++;
                d--;
                w--;
            } while (w != 0);

            d += width * 2;
            height--;
            w = width;
        } while (height != 0);
    }

    void __cdecl Flip_Bitmap_Down_Same(uint8_t* data, uint32_t width, uint32_t height) {
        uint8_t* row_start = data;
        uint8_t* row_end = data + width - 1;

        do {
            uint32_t half_width = width >> 1;
            uint8_t* left = row_start;
            uint8_t* right = row_end;

            do {
                uint8_t swapped_pixel = *right;
                *right = *left;
                *left = swapped_pixel;
                right--;
                left++;
            } while (--half_width != 0);

            row_start += width;
            row_end += width;
        } while (--height != 0);
    }

    void __cdecl Flip_Bitmap_Left_Different(uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst) {
        uint8_t* src_ptr = src + (height * width - width);

        do {
            memcpy(dst, src_ptr, width);
            dst += width;
            src_ptr -= width;
            height--;
        } while (height != 0);
    }

    void __cdecl Flip_Bitmap_Left_Same(uint8_t* pixels, uint32_t width, uint32_t height) {
        uint8_t* bottom_row = pixels + (height * width - width);
        height >>= 1;
        uint8_t* top_row = pixels;

        do {
            uint8_t* cur_top = top_row;
            uint8_t* cur_bottom = bottom_row;
            uint32_t w = width;

            do {
                uint8_t swapped_pixel = *cur_bottom;
                *cur_bottom = *cur_top;
                *cur_top = swapped_pixel;

                cur_bottom++;
                cur_top++;
                w--;
            } while (w != 0);

            top_row += width;
            bottom_row -= width;
            height--;
        } while (height != 0);
    }

    void __cdecl Outline_Bitmap_Pixels(uint8_t* src, uint32_t outline_color, uint32_t unused_param, uint8_t* dest, uint32_t width, uint32_t height) {
        shear::outline_color = outline_color;

        uint32_t pixel_count = width * width;
        bitmap::pixel_count = pixel_count;
        temp = 0;

        uint32_t col = width;
        uint8_t out_c = shear::outline_color & 0xFF;

        bool was_solid = false;
        bool was_empty = false;

        uint8_t* s = src;
        uint8_t* d = dest;

        // Horizontal pass
        for (uint32_t i = 0; i < pixel_count; ++i) {
            uint8_t pixel = *s++;

            if (pixel == 0 || pixel == out_c) {
                if (was_solid) {
                    *d = out_c;
                }
                was_solid = false;
                was_empty = true;
            } else {
                if (was_empty) {
                    *(d - 1) = out_c;
                }
                was_empty = false;
                was_solid = true;
            }

            d++;
            col--;
            if (col == 0) {
                col = width;
                was_solid = false;
                was_empty = false;
            }
        }

        pixel_count = width * width;
        bitmap::pixel_count = pixel_count;
        src_bitmap_end = src + pixel_count;

        s = src;
        d = dest;
        was_solid = false;
        was_empty = false;

        // Vertical pass
        for (uint32_t i = 0; i < pixel_count; ++i) {
            uint8_t pixel = *s;

            if (pixel == 0 || pixel == out_c) {
                if (was_solid) {
                    *d = out_c;
                }
                was_solid = false;
                was_empty = true;
            } else {
                if (was_empty) {
                    *(d - width) = out_c;
                }
                was_empty = false;
                was_solid = true;
            }

            s += width;
            d += width;

            if (s >= src_bitmap_end) {
                s -= pixel_count;
                d -= pixel_count;
                s++;
                d++;
                was_solid = false;
                was_empty = false;
            }
        }
    }

    void __cdecl Outline_Bitmap_Pixels_No_Glass(uint8_t* src, uint32_t outline_color, uint32_t unused_param, uint8_t* dest, uint32_t width, uint32_t height) {
        shear::outline_color = outline_color;
        uint32_t pixel_count = width * width;
        bitmap::pixel_count = pixel_count;
        temp = 0;

        uint8_t color = (uint8_t)outline_color;
        uint32_t x_count = width;
        bool was_glass = false;
        bool was_solid = false;

        uint8_t* current_src = src;
        uint8_t* current_dest = dest;

        // Pass 1: Horizontal outline
        for (uint32_t i = 0; i < pixel_count; ++i) {
            uint8_t pixel = *current_src++;

            if (pixel == 0 || pixel == color || pixel >= 0xF0) {
                // Glass pixel
                if (was_solid) {
                    *current_dest = color;
                }
                was_solid = false;
                was_glass = true;
            } else {
                // Solid pixel
                if (was_glass) {
                    *(current_dest - 1) = color;
                }
                was_glass = false;
                was_solid = true;
            }

            current_dest++;
            x_count--;

            if (x_count == 0) {
                x_count = width;
                was_glass = false;
                was_solid = false;
            }
        }

        // Pass 2: Vertical outline (column-major traversal)
        pixel_count = width * width;
        bitmap::pixel_count = pixel_count;
        src_bitmap_end = src + pixel_count;

        current_src = src;
        current_dest = dest;
        was_glass = false;
        was_solid = false;

        for (uint32_t i = 0; i < pixel_count; ++i) {
            uint8_t pixel = *current_src;

            if (pixel == 0 || pixel == color || pixel >= 0xF0) {
                // Glass pixel
                if (was_solid) {
                    *current_dest = color;
                }
                was_solid = false;
                was_glass = true;
            } else {
                // Solid pixel
                if (was_glass) {
                    *(current_dest - width) = color;
                }
                was_glass = false;
                was_solid = true;
            }

            current_src += width;
            current_dest += width;

            // Wrap around to the next column when reaching the end
            if (current_src >= src_bitmap_end) {
                current_src -= pixel_count;
                current_dest -= pixel_count;
                current_src++;
                current_dest++;
                was_glass = false;
                was_solid = false;
            }
        }
    }

    void __cdecl Interlace_Scale_Off_Page_ASM(uint8_t* page, uint8_t* lut) {
        row_temp = 240;
        uint8_t* ptr = page;

        do {
            for (uint32_t column = 0; column < 640; ++column) {
                ptr[column] = lut[ptr[column]];
            }

            ptr += 1280;
            row_temp--;
        } while (row_temp != 0);
    }

    void __cdecl Mask_Bitmap_Pixels(uint8_t* src, int32_t src_row_skip, int32_t dst_row_skip, uint8_t* dst, int32_t width, int32_t height) {
        shear::src_row_skip = src_row_skip;
        shear::dst_row_skip = dst_row_skip;

        uint32_t row_count = (uint32_t)height;

        do {
            uint32_t col_count = (uint32_t)width;

            do {
                if (*src != 0) {
                    *dst = 0;
                }
                ++src;
                ++dst;
            } while (--col_count != 0);

            src += shear::src_row_skip;
            dst += shear::dst_row_skip;
        } while (--row_count != 0);
    }

    void __cdecl Mask_Bitmap_Pixels_Inverse(uint8_t* src, int32_t src_row_skip, int32_t dst_row_skip, uint8_t* dst, int32_t width, int32_t height) {
        shear::src_row_skip = src_row_skip;
        shear::dst_row_skip = dst_row_skip;

        uint32_t row_count = (uint32_t)height;

        do {
            uint32_t col_count = (uint32_t)width;

            do {
                if (*src == 0) {
                    *dst = 0;
                }
                ++src;
                ++dst;
            } while (--col_count != 0);

            src += shear::src_row_skip;
            dst += shear::dst_row_skip;
        } while (--row_count != 0);
    }

    void __cdecl Mask_Brighten_Bitmap_Pixels(uint8_t* src, int32_t src_row_skip, int32_t dst_row_skip, uint8_t* dst, int32_t width, int32_t height) {
        shear::src_row_skip = src_row_skip;
        shear::dst_row_skip = dst_row_skip;

        uint32_t row_count = (uint32_t)height;

        do {
            uint32_t col_count = (uint32_t)width;

            do {
                uint8_t src_pixel = *src++;
                if (src_pixel != 0 && *dst != 0) {
                    uint32_t color_offset =
                        (((uint32_t)(uint8_t)(src_pixel - bitmap::brighten_start_color)) << 8) + *dst;
                    *dst = bitmap::brighten_palette_seg[color_offset];
                }
                ++dst;
            } while (--col_count != 0);

            src += shear::src_row_skip;
            dst += shear::dst_row_skip;
        } while (--row_count != 0);
    }

    void __cdecl Mask_Bitmap_Colors_Pixels(uint32_t* pixels, uint32_t min_color, uint32_t max_color, uint32_t unused_param, uint32_t width, uint32_t height) {
        col_temp = min_color;
        row_temp = max_color;

        uint32_t count = width * height >> 2;

        uint8_t min_c = min_color;
        uint8_t max_c = max_color;

        do {
            uint32_t val = *pixels;

            if ((uint8_t)val >= min_c && (uint8_t)val <= max_c) {
                val &= 0xFFFFFF00;
            }
            val = (val >> 8) | (val << 24);

            if ((uint8_t)val >= min_c && (uint8_t)val <= max_c) {
                val &= 0xFFFFFF00;
            }
            val = (val >> 8) | (val << 24);

            if ((uint8_t)val >= min_c && (uint8_t)val <= max_c) {
                val &= 0xFFFFFF00;
            }
            val = (val >> 8) | (val << 24);

            if ((uint8_t)val >= min_c && (uint8_t)val <= max_c) {
                val &= 0xFFFFFF00;
            }
            val = (val >> 8) | (val << 24);

            *pixels++ = val;
            count--;
        } while (count != 0);
    }

    void __cdecl Enlarge_Bitmap(uint8_t* src_ptr, uint32_t scale_x, uint32_t scale_y, uint8_t* dst_ptr, uint32_t src_width, uint32_t src_height, uint32_t dst_width, uint32_t dst_height) {
        shear::src_width = scale_x;
        shear::src_height = scale_y;

        uint64_t src_size = (uint64_t)src_width * src_height;
        uint32_t src_offset = (uint32_t)src_size + ((uint32_t)(src_size >> 32) << 16) - 1;
        src_ptr += src_offset;

        x_step_8_8 = (uint16_t)(0x6400 / (uint16_t)scale_x);

        uint32_t y_step_temp = (uint16_t)(0x6400 / (uint16_t)scale_y);
        y_step_8_8 = (src_offset & 0xFFFF0000) | y_step_temp;

        uint64_t dst_size = (uint64_t)dst_width * dst_height;
        uint32_t dst_offset = (uint32_t)dst_size + ((uint32_t)(dst_size >> 32) << 16) - 1;
        dst_ptr += dst_offset;

        shear::dst_width = (uint16_t)dst_width;
        y_accum_8_8 = 0;

        uint32_t x_step = x_step_8_8;

        uint32_t y = dst_height;
        do {
            uint8_t* row_src_ptr = src_ptr;
            uint32_t x_accum = 0;

            uint32_t x = dst_width;
            do {
                *dst_ptr = *src_ptr;
                dst_ptr--;

                x_accum += x_step;
                uint32_t step_int = x_accum >> 8;
                src_ptr -= step_int;
                x_accum &= 0xFF;

                x--;
            } while (x != 0);

            src_ptr = row_src_ptr;
            y_accum_8_8 += y_step_8_8;

            if (((y_accum_8_8 >> 8) & 0xFF) != 0) {
                src_ptr -= src_width;
                y_accum_8_8 &= 0xFFFF00FF;
            }

            y--;
        } while (y != 0);
    }

    void __cdecl Reduce_Bitmap(
        uint8_t* src_ptr, uint32_t src_width, uint32_t src_height, uint8_t* dst_ptr, uint32_t src_pitch, uint32_t unused_param, uint32_t dst_width, uint32_t dst_height) {
        shear::src_width = src_width;
        shear::src_height = src_height;

        // The original inline assembly carries unspecified upper register bits here,
        // but only the low 16 bits participate in the stepping logic we use below.
        uint32_t eax_reg = 0;

        uint16_t x_step = 0x6400 / (src_width & 0xFFFF);
        x_step_8_8 = x_step;

        uint16_t y_step = 0x6400 / (src_height & 0xFFFF);
        eax_reg = (eax_reg & 0xFFFF0000) | y_step;
        y_step_8_8 = eax_reg;

        shear::dst_width = dst_width & 0xFFFF;
        y_accum_8_8 = 0;

        uint32_t edx_reg = x_step;

        do {
            uint8_t* esi_reg = src_ptr;
            uint32_t ecx_reg = dst_width;
            uint32_t ebx_reg = 0;

            do {
                *dst_ptr++ = *esi_reg;
                ebx_reg += edx_reg;

                uint32_t eax_temp = ebx_reg;
                eax_temp >>= 8;
                esi_reg += eax_temp;

                ebx_reg &= 0xFF;

                ecx_reg--;
            } while (ecx_reg != 0);

            ecx_reg = y_accum_8_8;
            ecx_reg += y_step_8_8;

            eax_reg = src_pitch;

            // Emulates an 8-bit `MUL CH` bug where upper 16 bits of EAX are unchanged
            uint8_t ch = (ecx_reg >> 8) & 0xFF;
            uint8_t al = eax_reg & 0xFF;
            uint16_t ax = al * ch;

            eax_reg = (eax_reg & 0xFFFF0000) | ax;

            src_ptr += eax_reg;

            // Emulates `XOR CH, CH` which clears bits 8-15
            ecx_reg &= 0xFFFF00FF;

            y_accum_8_8 = ecx_reg;

            dst_height--;
        } while (dst_height != 0);
    }

    void __cdecl Vanish_Bitmap_Pixels(uint8_t* src, uint32_t noise_threshold, uint32_t unused_param, uint8_t* dst, uint32_t width, uint32_t height) {
        shear::noise_threshold = noise_threshold;
        uint8_t threshold = shear::noise_threshold;

        // The original loads 1000 into EAX but does not push it. Random_ instead
        // reads Vanish_Bitmap_Pixels's saved incoming ESI, which holds the source pointer.
        const uint32_t random_max =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src));
        uint32_t rand_val = antares::cosmetic_random::Random(random_max);
        noise_start_index = (rand_val - 1) & 0xFFFE;

        uint32_t total_pixels = width * height;
        uint32_t dword_count = total_pixels >> 2;
        if ((total_pixels & 3) != 0) {
            dword_count++;
        }

        row_dword_count = width >> 2;
        row_dword_counter = row_dword_count;

        uint32_t noise_idx = noise_start_index & 0xFE;

        for (uint32_t ecx = dword_count; ecx > 0; ecx--) {
            uint32_t pixels;
            struct_::Copy_Structure(&pixels, src, sizeof(pixels));
            src += sizeof(pixels);

            uint32_t noise;
            struct_::Copy_Structure(&noise, noise_table + noise_idx, sizeof(noise));
            noise_idx = (noise_idx + sizeof(noise)) & 0xFF;

            for (uint32_t byte_index = 0; byte_index < sizeof(pixels); ++byte_index) {
                if ((uint8_t)noise < threshold) {
                    pixels &= 0xFFFFFF00;
                }
                pixels = (pixels >> 8) | (pixels << 24);
                noise = (noise >> 8) | (noise << 24);
            }

            struct_::Copy_Structure(dst, &pixels, sizeof(pixels));
            dst += sizeof(pixels);

            row_dword_counter--;
            const bool carry_from_final_ror = (pixels & 0x80000000u) != 0;
            if (carry_from_final_ror || row_dword_counter == 0) {
                row_dword_counter = row_dword_count;
                noise_idx = (noise_idx + ecx * ecx) & 0xFE;
            }
        }
    }

    void __cdecl Jumble_Bitmap_Pixels(uint8_t* src_buf, uint32_t jumble_mask, uint32_t noise_threshold, uint8_t* dst_buf, uint32_t width, uint32_t height) {
        shear::noise_threshold = noise_threshold;
        shear::jumble_mask = jumble_mask;

        // The original loads 1000 into EAX, but Random_ reads the saved ESI stack slot instead.
        const uint32_t random_max = 0xFFFF0000u | (height & 0xFFFFu);
        shear::noise_start_index = (game_random::Random(random_max) - 1) & 0xFFFF;

        uint32_t pixel_count = (width * height) - shear::jumble_mask - shear::jumble_mask;

        src_buf += shear::jumble_mask;
        dst_buf += shear::jumble_mask;

        uint32_t noise_idx = shear::noise_start_index & 0xFF;
        uint8_t threshold = shear::noise_threshold;

        for (uint32_t i = 0; i < pixel_count; ++i) {
            uint8_t src_pixel = *src_buf++;
            uint32_t noise_val = shear::noise_table[noise_idx];

            if (noise_val < threshold) {
                noise_val &= shear::jumble_mask;
                noise_val = noise_val + 1;

                int32_t offset = (int32_t)noise_val;
                if (shear::jumble_direction_toggle != 0) {
                    offset = -offset;
                }

                uint8_t adj_pixel = src_buf[offset - 1];

                *dst_buf = adj_pixel;
                dst_buf[offset] = src_pixel;

                shear::jumble_direction_toggle = ~shear::jumble_direction_toggle;
            }

            noise_idx++;
            noise_idx &= 0xFF;
            dst_buf++;
        }
    }

    void __cdecl Gray_Scale_Fill_Pixels(
        uint8_t* pixels,
        uint8_t* gray_scale_table,
        int32_t y_step_8_8,
        uint32_t pixels_per_row,
        uint32_t row_count) {
        shear::y_step_8_8 = y_step_8_8;
        bitmap::bitmap_temp1 = gray_scale_table;
        bitmap::bitmap_temp2 = pixels_per_row;

        gray_scale_table = bitmap::bitmap_temp1;

        do {
            uint32_t remaining = bitmap::bitmap_temp2;

            do {
                const uint32_t index = *pixels;
                *pixels++ = gray_scale_table[index];
            } while (--remaining != 0);
        } while (--row_count != 0);
    }

} // namespace shear