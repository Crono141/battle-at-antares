#include "pch.h"

namespace MISC {
    uint8_t mouse_clock_image_num;

    s_mouse_list mouse_list_clock[4] = {
        {7, 0, 0, 0, 639, 479},
        {8, 0, 0, 0, 639, 479},
        {9, 0, 0, 0, 639, 479},
        {10, 0, 0, 0, 639, 479},
    };

    s_colors mox_font_colors;

    void __cdecl Increment_Mouse_Clock_Image() {
        if (mouse_clock_image_num >= 16) {
            mouse_clock_image_num = 0;
        }

        uint32_t clock_index = mouse_clock_image_num >> 2;
        s_mouse_list* cursor_entry = &mouse_list_clock[clock_index];

        mouse::Set_Mouse_List(cursor_entry, 1);

        mouse_clock_image_num++;
    }

    int16_t Get_Font_Style() {
        return fonts::current_font_style;
    }

    void __cdecl Set_Font_Colors(const s_colors* colors) {
        for (int i = 0; i < 8; ++i) {
            uint8_t color = colors->colors[i];
            fonts::font_header.selected_colors.colors[i] = color;
            fonts::font_header.normal_colors.colors[i] = color;
        }
    }

    s_colors* __cdecl Font_Colors2(int16_t mode, uint8_t primary_color, uint8_t secondary_color) {
        for (int i = 0; i < 8; ++i) {
            mox_font_colors.colors[i] = 0;
        }

        switch (mode) {
            case 0:
            case 1:
                mox_font_colors.colors[0] = primary_color;
                for (int i = 1; i < 8; ++i) {
                    mox_font_colors.colors[i] = secondary_color;
                }
                break;

            case 2:
            case 3:
                mox_font_colors.colors[0] = primary_color;
                for (int i = 1; i < 8; ++i) {
                    mox_font_colors.colors[i] = secondary_color;
                }
                if (MOX::mox_font_colors_offset != 0) {
                    mox_font_colors.colors[2] = MOX::mox_font_colors_offset;
                } else {
                    int32_t next_color = (int32_t)primary_color + 1;
                    if (next_color > (int32_t)secondary_color) {
                        next_color = (int32_t)secondary_color;
                    }
                    mox_font_colors.colors[2] = (uint8_t)next_color;
                }
                break;

            case 4:
                mox_font_colors.colors[0] = primary_color;
                for (int i = 1; i < 8; ++i) {
                    mox_font_colors.colors[i] = secondary_color;
                }
                if (MOX::mox_font_colors_offset != 0) {
                    mox_font_colors.colors[2] = MOX::mox_font_colors_offset;
                } else {
                    mox_font_colors.colors[2] = secondary_color - 1;
                }
                break;

            case 5:
                mox_font_colors.colors[1] = primary_color;
                mox_font_colors.colors[2] = secondary_color - 1;
                mox_font_colors.colors[3] = secondary_color - 1;
                mox_font_colors.colors[4] = secondary_color - 1;
                mox_font_colors.colors[5] = secondary_color;
                mox_font_colors.colors[6] = secondary_color;
                mox_font_colors.colors[7] = secondary_color;
                break;

            default:
                break;
        }

        MOX::mox_font_colors_offset = 0;
        return &mox_font_colors;
    }

    void __cdecl Get_Mox_Font_Colors(int16_t mode, uint8_t primary_color, uint8_t secondary_color, s_colors* out_colors) {
        s_colors* colors_ptr = MISC::Font_Colors2(mode, primary_color, secondary_color);
        memcpy(out_colors->colors, colors_ptr->colors, sizeof(out_colors->colors));
    }

    int16_t __cdecl Get_Font_Spacing() {
        return fonts::font_header.font_style_spacing;
    }

    void __cdecl Increase_Font_Color_Range(int16_t increment) {
        s_colors colors;
        fonts::Get_Colors(&colors);

        for (int i = 1; i < 8; ++i) {
            int32_t val = (int32_t)colors.colors[i] + increment;
            if (val > 255) {
                val = 255;
            }
            colors.colors[i] = (uint8_t)val;
        }

        MISC::Set_Font_Colors(&colors);
    }

    void __cdecl Decrease_Font_Color_Range(int16_t amount) {
        s_colors colors;
        fonts::Get_Colors(&colors);

        for (int32_t i = 1; i < 8; ++i) {
            int32_t new_val = (int32_t)colors.colors[i] - (int32_t)amount;

            if (new_val <= 0) {
                new_val = 0;
            }

            colors.colors[i] = (uint8_t)new_val;
        }

        MISC::Set_Font_Colors(&colors);
    }

    void __cdecl Save_Window_Status(int16_t* status) {
        graphics::Save_Window();
        *status = animate::clipping_flag;
    }

    void __cdecl Restore_Window_Status(int16_t status) {
        graphics::Restore_Window();
        if (status != 0) {
            video::Clip_On();
        } else {
            video::Clip_Off();
        }
    }

    void __cdecl Get_LBX_Message(int16_t message_id, char* buffer) {
        const char* src = HAROLD::H_Message(message_id);

        if (src != nullptr) {
            char* dst = buffer;
            while ((*dst++ = *src++) != '\0');
        }
    }

    void __cdecl Replace_Bitmap_Colors(int16_t x_start, int16_t y_start, int16_t x_end, int16_t y_end, uint8_t full_bitmap, uint8_t* old_colors, uint8_t* new_colors, int8_t color_count, s_animation_header* anim) {
        render::twin::Scope twin_scope("Replace_Bitmap_Colors", anim, 0, 0, 0, 0);
        int16_t width;
        int16_t height;

        MISC::Get_Bitmap_Dimensions(anim, &width, &height);

        if (full_bitmap != 0) {
            x_start = 0;
            y_start = 0;
            x_end = width;
            y_end = height;
        }

        uint8_t* pixel_data = (uint8_t*)anim + sizeof(*anim) + sizeof(uint32_t);

        for (int16_t y = y_start; y < y_end; ++y) {
            int32_t y_offset = (int32_t)y * width;
            for (int16_t x = x_start; x < x_end; ++x) {
                int32_t offset = y_offset + x;
                uint8_t current_pixel = pixel_data[offset];

                if (color_count > 0) {
                    for (int i = 0; i < color_count; ++i) {
                        if ((int8_t)old_colors[i] == current_pixel) {
                            current_pixel = new_colors[i];
                            pixel_data[offset] = current_pixel;
                        }
                    }
                }
            }
        }
        render::Bitmap_Replace_Colors(anim, x_start, y_start, x_end, y_end, old_colors, new_colors, color_count);
    }

    void __cdecl Fade_Into_Screen(void (*draw_callback)(), uint8_t* status_flag, int16_t palette_id) {
        fields::Disable_Draw_All_Fields();

        *status_flag = 1;
        palette::Clear_Palette_Changes(0, 0xff);
        mouse::Set_Mouse_List(&MOX::mouse_list_none, 1);

        ERIC::Fast_Fade_Out();
        fonts::Load_Palette(palette_id, 0, 0xff);
        palette::Set_Palette_Changes(0, 0xff);

        palette::Refresh_Palette();
        palstore::Darken_Palette(100);

        mouse::Set_Mouse_List(&MOX::mouse_list_arrow, 1);
        video::Set_Page_Off();

        draw_callback();

        palette::Clear_Palette_Changes(0, 0xff);

        if (MOX::reloaded_ship_palette == 0) {
            ERIC::Fast_Fade_In();
        } else {
            ERIC::Set_Fade_Percent(0);
            palstore::Darken_Palette(0);
        }

        RUSS::Mox_Sync_Update();
        MOX::reloaded_ship_palette = 0;

        fields::Enable_Draw_All_Fields();
    }

    void __cdecl _Darken_Fill(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
        if (animate::clipping_flag == 1) {
            if (x1 < video::screen_window_x1) {
                x1 = video::screen_window_x1;
            }
            if (y1 < video::screen_window_y1) {
                y1 = video::screen_window_y1;
            }
            if (x2 > video::screen_window_x2) {
                x2 = video::screen_window_x2;
            }
            if (y2 > video::screen_window_y2) {
                y2 = video::screen_window_y2;
            }
        }

        graphics::Tint_Fill(x1, y1, x2, y2, 0);
    }

    void __cdecl Change_Font_Style(int16_t font_id) {
        s_colors colors;
        int16_t saved_style = fonts::font_header.font_style;

        fonts::Get_Colors(&colors);
        fonts::Set_Font_Style((int32_t)font_id, &colors);

        fonts::font_header.font_style = saved_style;
    }

    void __cdecl Set_Font_Default_Spacing() {
        fonts::font_header.font_style_spacing =
            fonts::font_header.font_spacing[fonts::current_font_style];
    }

    int16_t __cdecl Bit_Value_To_Num(int32_t value, int16_t bit_limit) {
        int16_t first_bit_index = 0;
        bool bit_found = false;
        int16_t current_bit = 0;

        if (bit_limit > 0) {
            do {
                if (bit_found) {
                    break;
                }

                if ((value >> (current_bit & 0x1F)) & 1) {
                    first_bit_index = current_bit;
                    bit_found = true;
                }

                current_bit++;
            } while (current_bit < bit_limit);
        }

        return first_bit_index;
    }

    void __cdecl Set_Fitted_Font_Style(char* text, s_colors* colors, int16_t initial_style, int16_t initial_spacing, int16_t variant, int16_t max_width, int16_t min_style) {
        int16_t current_style = initial_style;
        int16_t current_spacing = initial_spacing;
        bool is_finished = false;

        if (variant == -1) {
            variant = 0;
        }

        do {
            fonts::Set_Font_Style(current_style, colors);
            fonts::Set_Font_Spacing(current_spacing);
            fonts::font_header.font_style = variant;

            if (fonts::Get_String_Width(text) < max_width) {
                is_finished = true;
            } else {
                current_spacing--;

                if (current_spacing < 1) {
                    current_spacing = initial_spacing;
                    current_style--;

                    if (current_style < min_style) {
                        is_finished = true;
                    }
                }
            }
        } while (!is_finished);
    }

    void __cdecl Replace_Char(char* str, char target, char replacement) {
        int16_t length = (int16_t)strlen(str);

        if (length <= 0) {
            return;
        }

        for (int16_t i = 0; i < length; ++i) {
            if (str[i] == target) {
                str[i] = replacement;
            }
        }
    }

    bool __cdecl Occluded(int16_t x1_min, int16_t y1_min, int16_t x1_max, int16_t y1_max, int16_t x2_min, int16_t y2_min, int16_t x2_max, int16_t y2_max) {
        if (((x2_min <= x1_min && x1_min <= x2_max) ||
                (x2_min <= x1_max && x1_max <= x2_max) ||
                (x1_min <= x2_min && x2_min <= x1_max) ||
                (x1_min <= x2_max && x2_max <= x1_max))) {
            if (((y2_min <= y1_min && y1_min <= y2_max) ||
                    (y2_min <= y1_max && y1_max <= y2_max) ||
                    (y1_min <= y2_min && y2_min <= y1_max) ||
                    (y1_min <= y2_max && y2_max <= y1_max))) {
                return true;
            }
        }

        return false;
    }

    bool __cdecl Is_Vowel(char c) {
        bool is_vowel = false;

        switch (c) {
            case 'A':
            case 'E':
            case 'I':
            case 'O':
            case 'U':
            case 'a':
            case 'e':
            case 'i':
            case 'o':
            case 'u':
                is_vowel = true;
                break;
            default:
                break;
        }

        if (MOX::settings.language == 2) {
            switch (c) {
                case '#':
                case '$':
                case '&':
                case '=':
                case '>':
                case '[':
                case ']':
                case '{':
                case '|':
                case '}':
                    is_vowel = true;
                    break;
                default:
                    break;
            }
        }

        return is_vowel;
    }

    void __cdecl Draw_Highlight_Box(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t* colors) {
        int16_t x2 = x + w;
        int16_t y2 = y + h;
        uint8_t c0 = colors[0];
        uint8_t c1 = colors[1];
        uint8_t c2 = colors[2];
        uint8_t c3 = colors[3];

        // Outer Top and Right edges
        line::Line(x, y, x2, y, c3);
        line::Line(x2, y, x2, y2, c3);

        int16_t x_inner = x + 1;
        int16_t y_inner = y + 1;
        int16_t x2_inner = x2 - 1;
        int16_t y2_inner = y2 - 1;

        // Inner Top and Right edges
        line::Line(x_inner, y_inner, x2_inner, y_inner, c2);
        line::Line(x2_inner, y_inner, x2_inner, y2_inner, c2);

        // Outer Left and Bottom edges
        line::Line(x, y_inner, x, y2, c0);
        line::Line(x, y2, x2, y2, c0);

        // Inner Left and Bottom edges
        line::Line(x_inner, y_inner, x_inner, y2_inner, c1);
        line::Line(x_inner, y2_inner, x2_inner, y2_inner, c1);
    }

    void __cdecl Draw_Darkened_Box(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color_base) {
        uint8_t colors[4];

        colors[0] = color_base;
        colors[1] = color_base + 1;
        colors[2] = color_base + 2;
        colors[3] = color_base + 3;

        // Darken the inner area of the box, offset by 1 pixel from the highlight border
        MISC::_Darken_Fill(x + 3, y + 3, x + width + 1, y + height + 1);

        // Draw the decorative highlighted border
        MISC::Draw_Highlight_Box(x + 2, y + 2, width, height, colors);
    }

    void __cdecl Get_Bitmap_Dimensions(s_animation_header* header, int16_t* width, int16_t* height) {
        *width = header->width;
        *height = header->height;
    }

    void __cdecl Swap_Bitmap_Colors(int16_t x_start, int16_t y_start, int16_t x_end, int16_t y_end, void* old_colors_ptr, void* new_colors_ptr, int16_t num_colors, s_animation_header* bitmap) {
        render::twin::Scope twin_scope("Swap_Bitmap_Colors", bitmap, 0, 0, 0, 0);
        int16_t width;
        int16_t height;

        Get_Bitmap_Dimensions(bitmap, &width, &height);

        uint8_t* old_colors = (uint8_t*)old_colors_ptr;
        uint8_t* new_colors = (uint8_t*)new_colors_ptr;

        uint8_t* pixel_data = (uint8_t*)bitmap + sizeof(*bitmap) + sizeof(uint32_t);

        for (int16_t y = y_start; y < y_end; ++y) {
            int32_t y_offset = (int32_t)y * (int32_t)width;
            for (int16_t x = x_start; x < x_end; ++x) {
                int32_t pixel_idx = (int32_t)x + y_offset;
                uint8_t current_color = pixel_data[pixel_idx];

                if (num_colors > 0) {
                    for (int16_t i = 0; i < num_colors; ++i) {
                        if (old_colors[i] == current_color) {
                            pixel_data[pixel_idx] = new_colors[i];
                            break;
                        }
                    }
                }
            }
        }
        render::Bitmap_Swap_Colors(bitmap, x_start, y_start, x_end, y_end, old_colors, new_colors, num_colors);
    }

    void __cdecl Tint_Bitmap_Colors(int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool full_bitmap, uint8_t tint_value, s_animation_header* anim) {
        render::twin::Scope twin_scope("Tint_Bitmap_Colors", anim, 0, 0, 0, 0);
        int16_t width;
        int16_t height;

        Get_Bitmap_Dimensions(anim, &width, &height);

        if (full_bitmap) {
            x1 = 0;
            y2 = height;
            y1 = 0;
            x2 = width;
        }

        uint8_t* pixels = (uint8_t*)anim + sizeof(*anim) + sizeof(uint32_t);

        for (int16_t y = y1; y < y2; ++y) {
            for (int16_t x = x1; x < x2; ++x) {
                uint8_t& pixel = pixels[x + (int32_t)width * y];
                if (pixel != 0) {
                    pixel += tint_value;
                }
            }
        }
        render::Bitmap_Tint_Colors(anim, x1, y1, x2, y2, tint_value);
    }

    void __cdecl Get_Bitmap_String_Range(int16_t segment_idx, int16_t divisor, int16_t* out_start_x, int16_t* out_start_y, int16_t* out_end_x, int16_t* out_end_y, char* text) {
        int16_t string_width = fonts::Get_String_Width(text);

        int32_t scaled_width = (((int32_t)string_width * 10) / (int32_t)divisor + 9) / 10;
        int16_t font_height = fonts::Get_Font_Height();

        int16_t start_x = segment_idx * (int16_t)scaled_width;
        *out_start_x = start_x;
        if (string_width < start_x) {
            *out_start_x = string_width;
        }

        int16_t end_x = *out_start_x + (int16_t)scaled_width;
        *out_end_x = end_x;
        if (string_width < end_x) {
            *out_end_x = string_width;
        }

        *out_start_y = 0;
        *out_end_y = font_height;
    }

    void __cdecl Get_Advancing_Bitmap_String_Range(int16_t tick, int16_t max_width, int16_t* out_start_x, int16_t* out_start_y, int16_t* out_end_x, int16_t* out_end_y, const char* text, uint8_t is_full_range) {
        int16_t string_width = fonts::Get_String_Width(text);
        int16_t font_height = fonts::Get_Font_Height();

        if (string_width < max_width) {
            *out_start_x = 0;
            *out_end_x = 0;

            int32_t divisor = ((max_width * 10 / (int32_t)string_width) + 9) / 10;
            if ((tick % divisor == 0) || (is_full_range != 0)) {
                int32_t start_x_calc = (tick * (int32_t)string_width * 10) / max_width;
                int16_t start_x = (int16_t)(start_x_calc / 10);

                if (start_x > string_width) {
                    start_x = string_width;
                }
                *out_start_x = start_x;

                if (is_full_range != 0) {
                    *out_end_x = string_width;
                } else {
                    *out_end_x = start_x + 1;
                }

                if (*out_end_x > string_width) {
                    *out_end_x = string_width;
                }
            }
        } else {
            int16_t current_tick;
            if (is_full_range != 0) {
                current_tick = max_width - 1;
            } else {
                current_tick = tick;
            }

            int32_t step = ((string_width * 10 / (int32_t)max_width) + 9) / 10;

            int16_t start_x = (int16_t)(current_tick * step);
            if (start_x > string_width) {
                start_x = string_width;
            }
            *out_start_x = start_x;

            int16_t end_x = start_x + (int16_t)step;
            if (end_x > string_width) {
                end_x = string_width;
            }
            *out_end_x = end_x;
        }

        *out_start_y = 0;
        *out_end_y = font_height;
    }

    void __cdecl Draw_Advancing_Fill_Message(uint8_t font_style, int16_t* p_frame_index, int16_t total_frames, s_colors* p_colors, s_colors* target_color, int16_t x, int16_t y, char* text, s_animation_header* anim, uint8_t alignment) {
        int16_t rect_x1;
        int16_t rect_y1;
        int16_t rect_x2;
        int16_t rect_y2;

        fonts::Set_Font_Style((uint16_t)font_style, p_colors);

        MISC::Get_Advancing_Bitmap_String_Range(*p_frame_index, total_frames, &rect_x1, &rect_y1, &rect_x2, &rect_y2, text, alignment);

        RUSS::Mox_Update();

        MISC::Replace_Bitmap_Colors(0, 0, rect_x2, rect_y2, 0, p_colors->colors, target_color->colors, 8, anim);

        RUSS::Mox_Update();

        animate::Draw(x, y, anim);

        *p_frame_index += 1;
    }

    char* __cdecl Get_Comma_String(int32_t value) {
        char separator = ',';
        if (MOX::settings.language == 1) {
            separator = ' ';
        }

        int32_t sign = value >> 31;
        int32_t abs_val = (value ^ sign) - sign;

        char raw_buf[32];
        snprintf(raw_buf, sizeof(raw_buf), "%d", abs_val);

        int16_t len = (int16_t)strlen(raw_buf);
        int16_t num_separators = (len - 1) / 3;

        MOX::temp_string[0] = '\0';

        if (num_separators == 0) {
            // Using raw_buf as format string to copy into temp_string
            strlcpy(MOX::temp_string, raw_buf, sizeof(MOX::temp_string));
        } else {
            char grouped_buf[32];
            int16_t src_idx = len - 1;
            int16_t group_counter = 0;
            int16_t sep_count = num_separators;
            int16_t total_len = len + num_separators;

            for (int16_t dst_idx = total_len; dst_idx >= 0; --dst_idx) {
                if (dst_idx == total_len) {
                    grouped_buf[dst_idx] = '\0';
                } else {
                    if ((group_counter % 4 == 0) && (group_counter > 1) && (sep_count > 0)) {
                        grouped_buf[dst_idx] = separator;
                        sep_count--;
                    } else {
                        grouped_buf[dst_idx] = raw_buf[src_idx];
                        src_idx--;
                    }
                }
                group_counter++;
            }

            strlcpy(MOX::temp_string, grouped_buf, sizeof(MOX::temp_string));
        }

        if (value < 0) {
            size_t current_len = strlen(MOX::temp_string);
            memmove(&MOX::temp_string[1], MOX::temp_string, current_len);
            MOX::temp_string[0] = '-';
            MOX::temp_string[current_len + 1] = '\0';
        }

        return MOX::temp_string;
    }

    void __cdecl Fade_To_Black() {
        ERIC::Fast_Fade_Out();
        video::Set_Page_Off();

        // Fill the screen (640x480) with black color (index 0)
        graphics::Fill(0, 0, 0x27f, 0x1df, 0);

        RUSS::Mox_Sync_Update();
    }

    void __cdecl _Print_Shadow_Down(int16_t x, int16_t y, uint8_t shadow_color, const char* text) {
        s_colors orig_colors;
        s_colors shadow_colors;

        fonts::Get_Colors(&orig_colors);

        shadow_colors.colors[0] = orig_colors.colors[0];
        memset(&shadow_colors.colors[1], shadow_color, sizeof(shadow_colors.colors) - 1);

        Set_Font_Colors(&shadow_colors);
        fonts::Print(x + 1, y + 1, text);

        Set_Font_Colors(&orig_colors);
        fonts::Print(x, y, text);
    }

    void __cdecl _Print_Centered_Shadow_Down(int16_t x, int16_t y, uint8_t color, const char* text) {
        int16_t width = fonts::Get_String_Width(text);
        int16_t centered_x = x - (width / 2);
        MISC::_Print_Shadow_Down(centered_x, y, color, text);
    }

    void __cdecl Get_Time_Stamp(
        const char* input,
        int16_t* out_month,
        int16_t* out_day,
        int16_t* out_year,
        int16_t* out_hour,
        int16_t* out_minute,
        int16_t* out_second) {
        struct tm* time_info = dos::Time_Stamp(input);

        *out_year = (int16_t)time_info->tm_year;
        *out_month = (int16_t)time_info->tm_mon;
        *out_day = (int16_t)time_info->tm_mday;
        *out_hour = (int16_t)time_info->tm_hour;
        *out_minute = (int16_t)time_info->tm_min;
        *out_second = (int16_t)time_info->tm_sec;
    }

    int32_t __cdecl Substring(char* haystack, char* needle) {
        char* found_ptr = strstr(haystack, needle);

        if (found_ptr != nullptr) {
            return (int32_t)(found_ptr - haystack);
        }

        return (uint16_t)strlen(haystack);
    }

    void __cdecl Diagonal_Line_Fill(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t spacing, uint8_t color1, uint8_t color2) {
        int16_t window_status;
        Save_Window_Status(&window_status);

        if (spacing == 0) {
            spacing = 16;
        }

        if (((640 / spacing) * spacing != 640) || ((480 / spacing) * spacing != 480)) {
            spacing = 16;
        }

        graphics::Set_Window(x1, y1, x2, y2);
        video::Clip_On();

        int16_t num_steps_y = 480 / spacing;
        int16_t num_steps_x = 640 / spacing;

        if (num_steps_x > 0) {
            int16_t current_x = 0;
            for (int16_t i = num_steps_x; i > 0; --i) {
                line::Line(current_x - 1, 0, current_x + 479, 480, color2);
                line::Line(current_x + 1, 0, current_x + 481, 480, color2);
                line::Line(current_x, 0, current_x + 480, 480, color1);
                current_x += spacing;
            }
        }

        if (num_steps_y > 0) {
            int16_t current_y = 0;
            for (int16_t i = num_steps_y; i > 0; --i) {
                line::Line(-1, current_y, 479 - current_y, 480, color2);
                line::Line(1, current_y, 481 - current_y, 480, color2);
                line::Line(0, current_y, 480 - current_y, 480, color1);
                current_y += spacing;
            }

            if (num_steps_y > 0) {
                current_y = 0;
                for (int16_t i = num_steps_y; i > 0; --i) {
                    line::Line(current_y - 1, 0, 1, current_y, color2);
                    line::Line(current_y + 1, 0, 1, current_y, color2);
                    line::Line(current_y, 0, 0, current_y, color1);
                    current_y += spacing;
                }
            }
        }

        if (num_steps_x > 0) {
            int16_t current_x = 0;
            for (int16_t i = num_steps_x; i > 0; --i) {
                line::Line(current_x - 1, 480, current_x + 479, 0, color2);
                line::Line(current_x + 1, 480, current_x + 481, 0, color2);
                line::Line(current_x, 480, current_x + 480, 0, color1);
                current_x += spacing;
            }
        }

        Restore_Window_Status(window_status);
    }
} // namespace MISC