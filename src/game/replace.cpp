#include "pch.h"

namespace replace {
    namespace {
        // A recoloured off-screen bitmap (e.g. the colony build screen's ship, colours moved into
        // the colony palette) keeps its HD image in step; recoloured loaded sprites are covered by
        // render::Note_Recolor.
        void Recolor_Hd_Bitmap(s_animation_header* anim, int32_t first, int32_t count, const uint8_t* map) {
            if ((anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_BITMAP) {
                render::Bitmap_Recolor(anim, first, count, map);
            }
        }
    } // namespace

    uint8_t graphics_buffer[1023];
    uint8_t* palette_color_list;

    void __cdecl Replace_Color_All(s_animation_header* anim, uint8_t color) {
        render::twin::Scope twin_scope("Replace_Color_All", anim, 0, 0, 0, 0);
        animate::animation_header = anim;
        if (anim->frame_count <= 0) {
            return;
        }

        for (int16_t frame = 0; frame < animate::animation_header->frame_count; frame++) {
            uint8_t* frame_data = Animation_Frame_Data_(animate::animation_header, frame);
            uint8_t flags = animate::animation_header->flags & ANIMATION_FLAG_DRAW_MODE_MASK;

            switch (flags) {
                case ANIMATION_DRAW_MODE_ANIMATED_SPRITE: {
                    int32_t current_y = *(uint16_t*)(frame_data + 2);
                    int32_t word_offset = 2;
                    int32_t height = animate::animation_header->height;

                    while (current_y < height) {
                        uint8_t* ptr = frame_data + word_offset * 2;
                        word_offset += 2;

                        int16_t count = *(int16_t*)ptr;
                        int16_t skip_count = *(int16_t*)(ptr + 2);

                        if (count == 0) {
                            current_y += skip_count;
                        } else {
                            if (count > 0) {
                                uint8_t* pixels = frame_data + word_offset * 2;
                                for (int32_t i = 0; i < count; i++) {
                                    if (pixels[i] != 0) {
                                        pixels[i] = color;
                                    }
                                }
                            }
                            word_offset += (count + 1) / 2;
                        }
                    }
                    break;
                }
                case ANIMATION_DRAW_MODE_BITMAP: {
                    int32_t size = animate::animation_header->height * animate::animation_header->width;
                    for (int32_t i = 0; i < size; i++) {
                        if (frame_data[i] != 0) {
                            frame_data[i] = color;
                        }
                    }
                    break;
                }
                case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND: {
                    int16_t chunks = 5;
                    int32_t offset = 0;

                    while (chunks > 0) {
                        while (true) {
                            uint8_t* ptr = frame_data + offset;
                            offset += 8;

                            int16_t chunk_header_word0 = *(int16_t*)ptr;
                            int16_t quad_count = *(int16_t*)(ptr + 4);

                            if (chunk_header_word0 == 0 && quad_count == 0) {
                                break;
                            }

                            int16_t count = (int16_t)((uint16_t)quad_count << 2);
                            if (count > 0) {
                                for (int32_t i = 0; i < count; i++) {
                                    if (frame_data[offset + i] != 0) {
                                        frame_data[offset + i] = color;
                                    }
                                }
                            }
                            offset += count;
                        }
                        chunks--;
                    }
                    break;
                }
                case ANIMATION_DRAW_MODE_REFRESH_MASK:
                    Exit_With_Message();
                    break;
            }
        }
        render::Recolor_All(anim, color);
    }

    void __cdecl Replace_Color(s_animation_header* anim, int16_t old_color, uint8_t new_color) {
        render::twin::Scope twin_scope("Replace_Color", anim, 0, 0, 0, 0);
        animate::animation_header = anim;

        if (anim->frame_count > 0) {
            for (int16_t frame_idx = 0; frame_idx < animate::animation_header->frame_count; ++frame_idx) {
                s_animation_header* current_anim = animate::animation_header;
                uint8_t* frame_data = Animation_Frame_Data_(current_anim, frame_idx);

                switch (current_anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
                    case ANIMATION_DRAW_MODE_ANIMATED_SPRITE: {
                        int32_t y = *(uint16_t*)(frame_data + 2);
                        int32_t data_offset_words = 2;
                        int32_t height = current_anim->height;

                        while (y < height) {
                            int16_t pixel_count = *(int16_t*)(frame_data + data_offset_words * 2);
                            int16_t skip_count = *(int16_t*)(frame_data + data_offset_words * 2 + 2);
                            data_offset_words += 2;

                            if (pixel_count == 0) {
                                y += skip_count;
                            } else {
                                int32_t byte_offset = data_offset_words * 2;
                                if (pixel_count > 0) {
                                    for (int32_t i = 0; i < pixel_count; ++i) {
                                        if ((int16_t) * (int8_t*)(frame_data + byte_offset) == old_color) {
                                            frame_data[byte_offset] = (uint8_t)new_color;
                                        }
                                        byte_offset++;
                                    }
                                }
                                data_offset_words += (pixel_count + 1) / 2;
                            }
                        }
                        break;
                    }
                    case ANIMATION_DRAW_MODE_BITMAP: {
                        int32_t size = (int32_t)current_anim->height * current_anim->width;
                        if (size > 0) {
                            for (int32_t i = 0; i < size; ++i) {
                                if (frame_data[i] == (uint8_t)old_color) {
                                    frame_data[i] = (uint8_t)new_color;
                                }
                            }
                        }
                        break;
                    }
                    case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND: {
                        int16_t loop_count = 5;
                        int32_t offset = 0;
                        while (loop_count != 0) {
                            while (true) {
                                int16_t chunk_header_word0 = *(int16_t*)(frame_data + offset);
                                int16_t chunk_quad_count = *(int16_t*)(frame_data + offset + 4);
                                offset += 8;

                                if (chunk_quad_count == 0 && chunk_header_word0 == 0) {
                                    break;
                                }

                                uint16_t chunk_byte_count_bits = (uint16_t)chunk_quad_count << 2;
                                int16_t chunk_byte_count16 = (int16_t)chunk_byte_count_bits;
                                int32_t chunk_byte_count = chunk_byte_count16;
                                if (chunk_byte_count > 0) {
                                    for (int32_t i = 0; i < chunk_byte_count; ++i) {
                                        if ((int16_t) * (int8_t*)(frame_data + offset + i) == old_color) {
                                            frame_data[offset + i] = (uint8_t)new_color;
                                        }
                                    }
                                }
                                offset += chunk_byte_count;
                            }
                            loop_count--;
                        }
                        break;
                    }
                    case ANIMATION_DRAW_MODE_REFRESH_MASK: {
                        Exit_With_Message();
                        break;
                    }
                }
            }
        }
        Recolor_Hd_Bitmap(anim, old_color, 1, &new_color);
        render::Recolor_One(anim, old_color, new_color);
    }

    void __cdecl Establish_Color_Gradient(int16_t scale, int16_t invert) {
        palette_color_list = graphics_buffer;

        for (int32_t i = 0; i < 256; ++i) {
            uint8_t r = video::current_palette[i].r;
            uint8_t g = video::current_palette[i].g;
            uint8_t b = video::current_palette[i].b;

            int32_t intensity = r + g + (b >> 1);
            uint8_t color = (uint8_t)((intensity * scale) / 158);

            palette_color_list[i] = color;

            if (invert == 1) {
                palette_color_list[i] = (uint8_t)(scale - color - 1);
            }
        }
    }

    void __cdecl Exit_With_Message() {
        exitm::Exit_With_Message("Cannot color replace refresh mask.");
    }

    void __cdecl Replace_Color_Range(s_animation_header* anim, uint8_t* palette_map, int16_t start_color, int16_t num_colors) {
        render::twin::Scope twin_scope("Replace_Color_Range", anim, 0, 0, 0, 0);
        render::Note_Recolor(anim, start_color, num_colors, palette_map);
        int32_t end_color = num_colors + start_color - 1;
        animate::animation_header = anim;

        if (animate::animation_header->frame_count > 0) {
            for (int16_t frame = 0; frame < animate::animation_header->frame_count; frame++) {
                uint8_t* frame_data = Animation_Frame_Data_(animate::animation_header, frame);

                switch (animate::animation_header->flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
                    case ANIMATION_DRAW_MODE_ANIMATED_SPRITE: {
                        int32_t line_y = ((uint16_t*)frame_data)[1];
                        int32_t height = animate::animation_header->height;
                        int32_t word_offset = 2;

                        while (line_y < height) {
                            int16_t* run = (int16_t*)(frame_data + word_offset * 2);
                            word_offset += 2;

                            int16_t count = run[0];
                            if (count == 0) {
                                line_y += run[1];
                            } else {
                                uint8_t* data = frame_data + word_offset * 2;
                                for (int32_t i = 0; i < count; i++) {
                                    uint8_t pixel = data[i];
                                    if (pixel != 0 && pixel >= start_color && pixel <= end_color) {
                                        data[i] = palette_map[pixel - start_color];
                                    }
                                }
                                word_offset += (count + 1) / 2;
                            }
                        }
                        break;
                    }
                    case ANIMATION_DRAW_MODE_BITMAP: {
                        int32_t area = animate::animation_header->width * animate::animation_header->height;
                        if (area > 0) {
                            for (int32_t i = 0; i < area; i++) {
                                uint8_t pixel = frame_data[i];
                                if (pixel != 0 && pixel >= start_color && pixel <= end_color) {
                                    frame_data[i] = palette_map[pixel - start_color];
                                }
                            }
                        }
                        break;
                    }
                    case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND: {
                        int16_t blocks = 5;
                        int32_t offset = 0;
                        do {
                            while (true) {
                                int16_t quad_count = *(int16_t*)(frame_data + offset + 4);
                                int16_t chunk_header_word0 = *(int16_t*)(frame_data + offset);
                                offset += 8;

                                if (quad_count == 0 && chunk_header_word0 == 0) {
                                    break;
                                }

                                uint16_t shifted = (uint16_t)quad_count << 2;
                                int16_t byte_count16 = (int16_t)shifted;
                                int32_t byte_count = byte_count16;
                                if (byte_count > 0) {
                                    for (int32_t i = 0; i < byte_count; i++) {
                                        uint8_t pixel = frame_data[offset + i];
                                        if (pixel != 0 && pixel >= start_color && pixel <= end_color) {
                                            frame_data[offset + i] = palette_map[pixel - start_color];
                                        }
                                    }
                                }
                                offset += byte_count;
                            }
                            blocks--;
                        } while (blocks != 0);
                        break;
                    }
                    case ANIMATION_DRAW_MODE_REFRESH_MASK: {
                        Exit_With_Message();
                        break;
                    }
                }
            }
        }
        Recolor_Hd_Bitmap(anim, start_color, num_colors, palette_map);
    }

    void __cdecl Shift_Palette_Range(s_animation_header* anim, int32_t shift) {
        render::twin::Scope twin_scope("Shift_Palette_Range", anim, 0, 0, 0, 0);
        animate::animation_header = anim;

        animate::palette_header = Animation_Palette_Header_(anim);

        if (animate::palette_header->count + shift > 256) {
            exitm::Exit_With_Message("Shift_Palette_Range() exceeds 256 colors");
        }

        animate::palette_header->start_index = (int16_t)shift;
    }

    void __cdecl Interlaced_Gray_Scale_Off_Page(uint8_t color_offset1, int16_t gradient_start1, uint8_t color_offset2, int16_t gradient_start2, int16_t invert) {
        render::twin::Scope twin_scope("Interlaced_Gray_Scale_Off_Page", video::g_page_off.pixels, 0, 0, 0, 0);
        replace::palette_color_list = replace::graphics_buffer;

        replace::Establish_Color_Gradient(gradient_start1, invert);

        for (int32_t i = 0; i < 256; ++i) {
            replace::palette_color_list[i] += color_offset1;
        }

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
        shear::Interlace_Scale_Off_Page_ASM(video::current_video_page, replace::palette_color_list);
        render::Table_Rows(video::current_video_page, 0, replace::palette_color_list);
        platform::Unlock_Appropriate_Render_Buffer();

        replace::Establish_Color_Gradient(gradient_start2, invert);

        for (int32_t i = 0; i < 256; ++i) {
            replace::palette_color_list[i] += color_offset2;
        }

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
        shear::Interlace_Scale_Off_Page_ASM(video::current_video_page + 0x280, replace::palette_color_list);
        render::Table_Rows(video::current_video_page, 1, replace::palette_color_list);
        platform::Unlock_Appropriate_Render_Buffer();
    }
} // namespace replace