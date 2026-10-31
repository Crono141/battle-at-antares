#include "pch.h"

namespace clipped {
    void __cdecl Clipped_Draw_Animated_Sprite(int16_t x, int16_t y, uint8_t* sprite) {
        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        int16_t clip_x_skip_count;
        int16_t draw_x_start;
        if (x < video::screen_window_x1) {
            clip_x_skip_count = video::screen_window_x1 - x;
            draw_x_start = video::screen_window_x1;
        } else {
            clip_x_skip_count = 0;
            draw_x_start = x;
        }

        int16_t clip_y_skip_count;
        int16_t draw_y_start;
        if (y < video::screen_window_y1) {
            clip_y_skip_count = video::screen_window_y1 - y;
            draw_y_start = video::screen_window_y1;
        } else {
            clip_y_skip_count = 0;
            draw_y_start = y;
        }

        int16_t draw_x_end = animate::animation_header->width + x - 1;
        if (draw_x_end > video::screen_window_x2) {
            draw_x_end = video::screen_window_x2;
        }

        int16_t draw_y_end = animate::animation_header->height + y - 1;
        if (draw_y_end > video::screen_window_y2) {
            draw_y_end = video::screen_window_y2;
        }

        int32_t draw_width = draw_x_end - draw_x_start + 1;
        int32_t draw_height = draw_y_end - draw_y_start + 1;

        int32_t current_sprite_y = (uint16_t)((s_packed_frame_header*)sprite)->start_y;
        int32_t line_base = (int32_t)x + (int32_t)draw_y_start * video::g_screen_res_x;
        int32_t sprite_word_idx = 2;

        // Skip segments above the clipping window
        while (current_sprite_y < clip_y_skip_count) {
            s_packed_frame_run* run = (s_packed_frame_run*)(sprite + sprite_word_idx * 2);
            int16_t count = run->pixel_count;
            if (count == 0) {
                current_sprite_y += (uint16_t)run->skip_count;
            } else {
                sprite_word_idx += (count + 1) / 2;
            }
            sprite_word_idx += 2;
        }

        // Adjust buffer pointer if first visible segment starts below initial drawing line
        if (clip_y_skip_count < current_sprite_y) {
            line_base += (current_sprite_y - clip_y_skip_count) * video::g_screen_res_x;
        }

        int32_t current_line_idx = current_sprite_y - clip_y_skip_count;
        int32_t win_start_abs = line_base + clip_x_skip_count;
        int32_t win_end_abs = win_start_abs + draw_width;
        int32_t cursor_abs = line_base;

        while (current_line_idx < draw_height) {
            s_packed_frame_run* run = (s_packed_frame_run*)(sprite + sprite_word_idx * 2);
            int16_t count = run->pixel_count;
            int16_t val = run->skip_count;
            sprite_word_idx += 2;

            if (count == 0) {
                // Vertical skip
                current_line_idx += val;
                line_base += (int32_t)val * 640;
                win_start_abs = line_base + clip_x_skip_count;
                cursor_abs = line_base;
                win_end_abs = win_start_abs + draw_width;
            } else {
                // Horizontal segment
                cursor_abs += val;
                int32_t run_start_abs = cursor_abs;
                int32_t run_end_abs = run_start_abs + count;
                uint32_t pixel_ptr = sprite_word_idx * 2;

                if (run_start_abs < win_end_abs) {
                    // Clip left
                    if (run_start_abs < win_start_abs) {
                        int32_t skip_px = win_start_abs - run_start_abs;
                        if (count < skip_px) {
                            skip_px = count;
                        }
                        pixel_ptr += skip_px;
                        run_start_abs += skip_px;
                        cursor_abs += skip_px;
                    }

                    // Clip right
                    int32_t run_clipped_end = win_end_abs;
                    if (run_end_abs < win_end_abs) {
                        run_clipped_end = run_end_abs;
                    }

                    // Draw visible run
                    uint8_t* screen_ptr = (uint8_t*)(video::current_video_page + run_start_abs);
                    uint8_t* stamp_start = screen_ptr;
                    while (run_start_abs < run_clipped_end) {
                        *screen_ptr++ = sprite[pixel_ptr++];
                        run_start_abs++;
                        cursor_abs++;
                    }
                    render::Stamp_Run(stamp_start, (int32_t)(screen_ptr - stamp_start));
                }
                sprite_word_idx += (count + 1) / 2;
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Clipped_Draw_Stenciled_Animated_Sprite(int16_t x, int16_t y, uint8_t* sprite) {
        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        int16_t left_excess = 0;
        int16_t render_x_start = x;
        if (x < video::screen_window_x1) {
            left_excess = video::screen_window_x1 - x;
            render_x_start = video::screen_window_x1;
        }

        int16_t top_excess = 0;
        int16_t render_y_start = y;
        if (y < video::screen_window_y1) {
            top_excess = video::screen_window_y1 - y;
            render_y_start = video::screen_window_y1;
        }

        int16_t sprite_right_limit = x + animate::animation_header->width - 1;
        if (sprite_right_limit > video::screen_window_x2) {
            sprite_right_limit = video::screen_window_x2;
        }

        int16_t sprite_bottom_limit = y + animate::animation_header->height - 1;
        if (sprite_bottom_limit > video::screen_window_y2) {
            sprite_bottom_limit = video::screen_window_y2;
        }

        int16_t draw_width_limit = sprite_right_limit - render_x_start + 1;
        int16_t draw_height_limit = sprite_bottom_limit - render_y_start + 1;

        int32_t current_row_rel = (uint16_t)((s_packed_frame_header*)sprite)->start_y;
        int32_t line_base = (int32_t)render_y_start * video::g_screen_res_x + (int32_t)x;

        uint32_t sprite_word_idx = 2; // Offset in 16-bit words

        // Row skip loop: find the starting visible row
        while (current_row_rel < top_excess) {
            s_packed_frame_run* run = (s_packed_frame_run*)(sprite + sprite_word_idx * 2);
            int16_t count = run->pixel_count;
            if (count == 0) {
                current_row_rel += (uint16_t)run->skip_count;
            } else {
                sprite_word_idx += (count + 1) / 2;
            }
            sprite_word_idx += 2;
        }

        // Adjust screen offset if we skipped rows
        if (current_row_rel > top_excess) {
            line_base += (current_row_rel - top_excess) * video::g_screen_res_x;
        }

        int32_t current_line_idx = current_row_rel - top_excess;
        int32_t row_cursor_abs = line_base;
        int32_t render_x_start_offset = line_base + left_excess;
        int32_t render_x_end_limit = line_base + left_excess + draw_width_limit;

        bool is_drawing_run = false;
        int32_t first_run_pixel_offset = row_cursor_abs;
        int32_t last_run_pixel_offset = row_cursor_abs;

        // Main drawing loop
        while (current_line_idx < draw_height_limit) {
            s_packed_frame_run* run = (s_packed_frame_run*)(sprite + sprite_word_idx * 2);
            int16_t count = run->pixel_count;
            int16_t x_offset = run->skip_count;
            sprite_word_idx += 2;

            if (count == 0) {
                // Vertical row skip command
                current_row_rel += x_offset;
                current_line_idx = current_row_rel - top_excess;
                if (is_drawing_run) {
                    // Record scanline coverage if row was processed
                    video::Add_Line_To_Scan_List(
                        first_run_pixel_offset % 640,
                        first_run_pixel_offset / 640,
                        last_run_pixel_offset % 640);
                }

                // Update row tracking variables
                int32_t skip_pixels = x_offset * 640;
                line_base += skip_pixels;
                row_cursor_abs = line_base;
                render_x_start_offset = line_base + left_excess;
                render_x_end_limit = render_x_start_offset + draw_width_limit;

                is_drawing_run = false;
                first_run_pixel_offset = row_cursor_abs;
            } else {
                // Horizontal pixel run command
                row_cursor_abs += x_offset;
                int32_t run_byte_idx = sprite_word_idx * 2;
                int32_t run_end_limit = row_cursor_abs + count;

                // Horizontal clipping
                if (row_cursor_abs < render_x_end_limit) {
                    if (row_cursor_abs < render_x_start_offset) {
                        int16_t clip_left = (int16_t)(render_x_start_offset - row_cursor_abs);
                        if (count < clip_left) {
                            clip_left = count;
                        }
                        run_byte_idx += clip_left;
                        row_cursor_abs += clip_left;
                    }

                    int32_t visible_run_end = render_x_end_limit;
                    if (run_end_limit < visible_run_end) {
                        visible_run_end = run_end_limit;
                    }

                    // Copy pixels to video buffer
                    while (row_cursor_abs < visible_run_end) {
                        if (!is_drawing_run) {
                            is_drawing_run = true;
                            first_run_pixel_offset = row_cursor_abs;
                        }
                        last_run_pixel_offset = row_cursor_abs;
                        video::current_video_page[row_cursor_abs] = sprite[run_byte_idx];
                        run_byte_idx++;
                        row_cursor_abs++;
                    }
                }
                // Advance word index past pixel data
                sprite_word_idx += (count + 1) / 2;
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Clipped_Draw_Bitmap_Sprite(int16_t x, int16_t y, uint8_t* pixels) {
        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        s_animation_header* header = animate::animation_header;
        int32_t sprite_width = (int32_t)header->width;
        int32_t sprite_height = (int32_t)header->height;

        // Calculate horizontal clipping
        int32_t x_end = (int32_t)x + sprite_width - 1;
        if (x_end > (int32_t)video::screen_window_x2) {
            x_end = (int32_t)video::screen_window_x2;
        }

        int32_t clamped_x_start = (video::screen_window_x1 > x) ? (int32_t)video::screen_window_x1 : (int32_t)x;
        int32_t visible_width = x_end - clamped_x_start + 1;

        // Calculate vertical clipping
        int32_t y_end = (int32_t)y + sprite_height - 1;
        if (y_end > (int32_t)video::screen_window_y2) {
            y_end = (int32_t)video::screen_window_y2;
        }

        int32_t clamped_y_start = (video::screen_window_y1 > y) ? (int32_t)video::screen_window_y1 : (int32_t)y;
        int32_t visible_height = y_end - clamped_y_start + 1;

        // Initial destination pointer based on unclipped signed (x, y)
        uint8_t* dst = video::current_video_page + (int32_t)y * video::g_screen_res_x + (int32_t)x;

        // Adjust pointers and tracking for clipping
        int32_t scan_x = (int32_t)x;
        if (x < video::screen_window_x1) {
            int32_t left_clip = (int32_t)video::screen_window_x1 - (int32_t)x;
            dst += left_clip;
            pixels += left_clip;
            scan_x = left_clip;
        }

        int32_t scan_y = (int32_t)y;
        if (y < video::screen_window_y1) {
            int32_t top_clip = (int32_t)video::screen_window_y1 - (int32_t)y;
            pixels += top_clip * sprite_width;
            dst += top_clip * video::g_screen_res_x;
            scan_y = top_clip;
        }

        // Add dirty rect if requested
        if (video::screen_refresh_flag > 1) {
            setvesa::Add_Square_To_Scan_List(
                scan_x,
                scan_y,
                scan_x + visible_width - 1,
                scan_y + visible_height - 1);
        }

        // Draw clipped rows
        if (visible_height > 0) {
            for (int32_t i = 0; i < visible_height; ++i) {
                draw::Color_Stream_Copy(pixels, dst, visible_width);
                pixels += sprite_width;
                dst += video::g_screen_res_x;
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Clipped_Draw_Glassed_Bitmap_Sprite(int32_t x, int32_t y, uint8_t* sprite_pixels) {
        int32_t x_start = x;
        if (x < (int32_t)video::screen_window_x1) {
            x_start = (int32_t)video::screen_window_x1;
        }

        int32_t x_end = (int32_t)animate::animation_header->width + x;
        if ((int32_t)video::screen_window_x2 <= x_end) {
            x_end = (int32_t)video::screen_window_x2;
        }

        int32_t y_start = y;
        if (y < (int32_t)video::screen_window_y1) {
            y_start = (int32_t)video::screen_window_y1;
        }

        int32_t y_end = (int32_t)animate::animation_header->height + y;
        if ((int32_t)video::screen_window_y2 <= y_end) {
            y_end = (int32_t)video::screen_window_y2;
        }

        int32_t draw_height = y_end - y_start;
        int32_t draw_width = x_end - x_start;

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        uint8_t* screen_ptr = video::current_video_page + (y * video::g_screen_res_x) + x;

        if (x < (int32_t)video::screen_window_x1) {
            int32_t diff_x = (int32_t)video::screen_window_x1 - x;
            screen_ptr += diff_x;
            sprite_pixels += diff_x;
        }

        if (y < (int32_t)video::screen_window_y1) {
            int32_t diff_y = (int32_t)video::screen_window_y1 - y;
            screen_ptr += diff_y * video::g_screen_res_x;
            sprite_pixels += diff_y * animate::animation_header->width;
        }

        if (draw_height > 0) {
            do {
                if (draw_width > 0) {
                    uint8_t* current_sprite_row = sprite_pixels;
                    uint8_t* current_screen_row = screen_ptr;
                    int32_t count = draw_width;

                    do {
                        uint8_t pixel = *current_sprite_row;
                        if (pixel != 0) {
                            if (pixel >= 0xF0) {
                                const uint8_t background = *current_screen_row;
                                *current_screen_row = video::glass_remap_colors[pixel - 0xF0][background];
                                render::Stamp_Glass(current_screen_row, background, pixel - 0xF0);
                            } else {
                                *current_screen_row = pixel;
                                render::Stamp_Run(current_screen_row, 1);
                            }
                        }
                        current_screen_row++;
                        current_sprite_row++;
                        count--;
                    } while (count != 0);
                }

                screen_ptr += video::g_screen_res_x;
                sprite_pixels += animate::animation_header->width;
                draw_height--;
            } while (draw_height > 0);
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Clipped_Draw_Glassed_Animated_Sprite(int32_t x, int32_t y, uint8_t* sprite) {
        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        int32_t clipped_x = x;
        int32_t clipped_y = y;
        int32_t right_clip = 0x27f;
        int32_t bottom_clip = 0x1df;
        uint8_t* sprite_cursor = sprite + sizeof(s_packed_frame_header);

        if ((int32_t)video::screen_window_x1 > x) {
            clipped_x = (int32_t)video::screen_window_x1;
        }

        if ((int32_t)video::screen_window_y1 > y) {
            clipped_y = (int32_t)video::screen_window_y1;
        }

        if ((int32_t)video::screen_window_x2 < x + (int32_t)animate::animation_header->height) {
            right_clip = (int32_t)video::screen_window_x2;
        }

        int32_t sprite_bottom = y + (int32_t)animate::animation_header->height;
        if ((int32_t)video::screen_window_y2 < sprite_bottom) {
            bottom_clip = (int32_t)video::screen_window_y2;
        }

        int32_t start_y = (int32_t)((s_packed_frame_header*)sprite)->start_y + y;
        uint8_t* line_base = video::current_video_page + x + start_y * video::g_screen_res_x;
        uint8_t* screen_cursor = line_base;
        int32_t current_x = x;

        if (bottom_clip >= sprite_bottom) {
            bottom_clip = sprite_bottom;
        }

        while (start_y <= bottom_clip) {
            s_packed_frame_run* run = (s_packed_frame_run*)sprite_cursor;
            int32_t count = (int32_t)run->pixel_count;
            int32_t skip = (int32_t)run->skip_count;
            sprite_cursor += sizeof(s_packed_frame_run);

            if (count == 0) {
                current_x = x;
                start_y += skip;
                line_base += skip * 640;
                screen_cursor = line_base;
            } else if (clipped_y > start_y || right_clip <= current_x) {
                sprite_cursor += count;
            } else {
                current_x += skip;
                screen_cursor += skip;

                if (clipped_x > current_x) {
                    int32_t old_x = current_x;
                    sprite_cursor -= old_x;
                    sprite_cursor += clipped_x;
                    screen_cursor -= old_x;
                    screen_cursor += clipped_x;
                    count = count - clipped_x + old_x;
                    current_x = clipped_x;
                }

                uint8_t* pixel_src = sprite_cursor;
                uint8_t* dst = screen_cursor;
                if (right_clip < count + current_x) {
                    int32_t visible_count = right_clip - current_x;
                    if (visible_count > 0) {
                        visible_count++;
                        do {
                            uint8_t pixel = *pixel_src;
                            if (pixel != 0) {
                                if (pixel >= 0xF0) {
                                    const uint8_t background = *dst;
                                    *dst = video::glass_remap_colors[pixel - 0xF0][background];
                                    render::Stamp_Glass(dst, background, pixel - 0xF0);
                                } else {
                                    *dst = pixel;
                                    render::Stamp_Run(dst, 1);
                                }
                            }
                            ++pixel_src;
                            ++dst;
                            --visible_count;
                        } while (visible_count != 0);
                    }
                } else if (count > 0) {
                    int32_t remaining = count;
                    do {
                        uint8_t pixel = *pixel_src;
                        if (pixel != 0) {
                            if (pixel >= 0xF0) {
                                pixel = video::glass_remap_colors[pixel - 0xF0][*dst];
                            }
                            *dst = pixel;
                        }
                        ++pixel_src;
                        ++dst;
                        --remaining;
                    } while (remaining != 0);
                }

                current_x += count;
                sprite_cursor += count;
                screen_cursor += count;
            }

            if (((uint32_t)(uintptr_t)sprite_cursor & 1) != 0) {
                ++sprite_cursor;
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }
} // namespace clipped