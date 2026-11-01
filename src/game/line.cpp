#include "pch.h"

namespace line {

    void __cdecl Multi_Colored_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, s_colors* colors, int16_t offset) {
        render::twin::Scope twin_scope("Multi_Colored_Line", video::current_video_page, SDL_min(x1, x2), SDL_min(y1, y2), SDL_abs(x2 - x1) + 1, SDL_abs(y2 - y1) + 1);
        uint16_t color_index = (uint16_t)offset;

        if (animate::clipping_flag == 1) {
            if (line::Clip_Line(&x1, &y1, &x2, &y2) == 1) {
                return;
            }
        }

        if (y2 < y1) {
            strings::Swap_Short(&y1, &y2);
            strings::Swap_Short(&x1, &x2);
        }

        uint8_t* local_ptr = video::current_video_page + y1 * video::g_screen_res_x + x1;
        int16_t dx = x2 - x1;
        int16_t abs_dx = dx;
        int16_t x_dir = 1;

        if (dx < 0) {
            abs_dx = -dx;
            x_dir = -1;
        }

        int16_t dy = y2 - y1;

        if (abs_dx == 0) {
            int16_t count = dy + 1;
            while (count > 0) {
                *local_ptr = colors->colors[color_index & 7];
                render::Stamp_Run(local_ptr, 1);
                color_index++;
                local_ptr += video::g_screen_res_x;
                count--;
            }
            if (video::screen_refresh_flag >= 2 && y1 <= y2) {
                for (int16_t cur_y = y1; cur_y <= y2; cur_y++) {
                    video::Add_Dot_To_Scan_List(x1, cur_y);
                }
            }
            return;
        }

        if (dy == 0) {
            int16_t count = abs_dx + 1;
            while (count > 0) {
                *local_ptr = colors->colors[color_index & 7];
                render::Stamp_Run(local_ptr, 1);
                color_index++;
                local_ptr += x_dir;
                count--;
            }
            if (video::screen_refresh_flag >= 2) {
                if (x2 < x1) {
                    strings::Swap_Short(&x1, &x2);
                }
                setvesa::Add_Square_To_Scan_List(x1, y1, x2, y1);
            }
            return;
        }

        if (abs_dx == dy) {
            int16_t count = abs_dx + 1;
            while (count > 0) {
                *local_ptr = colors->colors[color_index & 7];
                render::Stamp_Run(local_ptr, 1);
                color_index++;
                local_ptr += video::g_screen_res_x + x_dir;
                count--;
            }
            if (video::screen_refresh_flag >= 2 && y1 <= y2) {
                if (x2 <= x1) {
                    for (int16_t cur_y = y1; cur_y <= y2; cur_y++) {
                        video::Add_Dot_To_Scan_List(x1 - (cur_y - y1), cur_y);
                    }
                } else {
                    for (int16_t cur_y = y1; cur_y <= y2; cur_y++) {
                        video::Add_Dot_To_Scan_List(x1 + (cur_y - y1), cur_y);
                    }
                }
            }
            return;
        }

        if (abs_dx > dy) {
            int16_t run_base = abs_dx / dy;
            int16_t rem2 = (abs_dx % dy) * 2;
            int16_t err = (abs_dx % dy) - 2 * dy;
            int16_t first_run = (run_base / 2) + 1;
            int16_t first_run_adj = first_run;

            if (rem2 == 0 && (run_base & 1) == 0) {
                first_run_adj--;
            }
            if ((run_base & 1) != 0) {
                err += dy;
            }

            line::Draw_Horizontal_Run_Multi(&local_ptr, x_dir, first_run_adj, colors, &color_index);

            if (video::screen_refresh_flag >= 2) {
                if (x_dir > 0) {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + first_run_adj, y1);
                    x1 += first_run_adj;
                } else {
                    setvesa::Add_Square_To_Scan_List(x1 - first_run_adj, y1, x1, y1);
                    x1 -= first_run_adj;
                }
                y1++;
            }

            int count = dy - 1;
            for (int i = 0; i < count; i++) {
                int16_t run = run_base;
                err += rem2;
                if (err > 0) {
                    run++;
                    err -= 2 * dy;
                }
                line::Draw_Horizontal_Run_Multi(&local_ptr, x_dir, run, colors, &color_index);

                if (video::screen_refresh_flag >= 2) {
                    if (x_dir > 0) {
                        setvesa::Add_Square_To_Scan_List(x1, y1, x1 + run, y1);
                        x1 += run;
                    } else {
                        setvesa::Add_Square_To_Scan_List(x1 - run, y1, x1, y1);
                        x1 -= run;
                    }
                    y1++;
                }
            }

            line::Draw_Horizontal_Run_Multi(&local_ptr, x_dir, first_run, colors, &color_index);

            if (video::screen_refresh_flag >= 2) {
                if (x_dir > 0) {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + first_run, y1);
                } else {
                    setvesa::Add_Square_To_Scan_List(x1 - first_run, y1, x1, y1);
                }
            }
        } else {
            int16_t run_base = dy / abs_dx;
            int16_t rem2 = (dy % abs_dx) * 2;
            int16_t err = (dy % abs_dx) - 2 * abs_dx;
            int16_t first_run = (run_base / 2) + 1;
            int16_t first_run_adj = first_run;

            if (rem2 == 0 && (run_base & 1) == 0) {
                first_run_adj--;
            }
            if ((run_base & 1) != 0) {
                err += abs_dx;
            }

            line::Draw_Vertical_Run_Multi(&local_ptr, x_dir, first_run_adj, colors, &color_index);

            if (video::screen_refresh_flag >= 2) {
                for (int16_t i = 0; i < first_run_adj; i++) {
                    video::Add_Dot_To_Scan_List(x1, y1 + i);
                }
                y1 += first_run_adj;
                x1 += x_dir;
            }

            int count = abs_dx - 1;
            for (int i = 0; i < count; i++) {
                int16_t run = run_base;
                err += rem2;
                if (err > 0) {
                    run++;
                    err -= 2 * abs_dx;
                }
                line::Draw_Vertical_Run_Multi(&local_ptr, x_dir, run, colors, &color_index);

                if (video::screen_refresh_flag >= 2) {
                    for (int16_t j = 0; j < run; j++) {
                        video::Add_Dot_To_Scan_List(x1, y1 + j);
                    }
                    y1 += run;
                    x1 += x_dir;
                }
            }

            line::Draw_Vertical_Run_Multi(&local_ptr, x_dir, first_run, colors, &color_index);

            if (video::screen_refresh_flag >= 2) {
                for (int16_t i = 0; i < first_run; i++) {
                    video::Add_Dot_To_Scan_List(x1, y1 + i);
                }
            }
        }
    }

    int16_t __cdecl Clip_Line(int16_t* x1, int16_t* y1, int16_t* x2, int16_t* y2) {
        int16_t is_clipped = 0;

        if (*x2 == *x1) {
            if (*x1 < video::screen_window_x1 || *x1 > video::screen_window_x2) {
                is_clipped = 1;
            } else {
                if (*y1 > *y2) {
                    strings::Swap_Short(y1, y2);
                }

                if (*y2 < video::screen_window_y1 || *y1 > video::screen_window_y2) {
                    is_clipped = 1;
                } else {
                    if (*y1 < video::screen_window_y1) {
                        *y1 = video::screen_window_y1;
                    }
                    if (*y2 > video::screen_window_y2) {
                        *y2 = video::screen_window_y2;
                    }
                }
            }
        } else {
            if (*x2 < *x1) {
                strings::Swap_Short(x1, x2);
                strings::Swap_Short(y1, y2);
            }

            int32_t dy = (int32_t)*y2 - (int32_t)*y1;
            int32_t dx = (int32_t)*x2 - (int32_t)*x1;

            if (*x1 < video::screen_window_x1) {
                *y1 = (int16_t)(*y1 + ((((int32_t)video::screen_window_x1 - (int32_t)*x1) * dy) / dx));
                *x1 = video::screen_window_x1;
            }

            if (*x2 < *x1) {
                is_clipped = 1;
            } else {
                if (*x2 > video::screen_window_x2) {
                    *y2 = (int16_t)(*y1 + ((((int32_t)video::screen_window_x2 - (int32_t)*x1) * dy) / dx));
                    *x2 = video::screen_window_x2;
                }

                if (*x2 < *x1) {
                    is_clipped = 1;
                }
            }
        }

        if (*y1 == *y2) {
            if (*y1 < video::screen_window_y1 || *y1 > video::screen_window_y2) {
                is_clipped = 1;
            } else {
                if (*x2 < *x1) {
                    strings::Swap_Short(x1, x2);
                }

                if (*x2 < video::screen_window_x1 || *x1 > video::screen_window_x2) {
                    is_clipped = 1;
                } else {
                    if (*x1 < video::screen_window_x1) {
                        *x1 = video::screen_window_x1;
                    }
                    if (*x2 > video::screen_window_x2) {
                        *x2 = video::screen_window_x2;
                    }
                }
            }
        } else {
            if (*y2 < *y1) {
                strings::Swap_Short(y1, y2);
                strings::Swap_Short(x1, x2);
            }

            int32_t dx = (int32_t)*x2 - (int32_t)*x1;
            int32_t dy = (int32_t)*y2 - (int32_t)*y1;

            if (*y1 < video::screen_window_y1) {
                *x1 = (int16_t)(*x1 + ((((int32_t)video::screen_window_y1 - (int32_t)*y1) * dx) / dy));
                *y1 = video::screen_window_y1;
            }

            if (*y1 > *y2) {
                is_clipped = 1;
            } else {
                if (*y2 > video::screen_window_y2) {
                    *x2 = (int16_t)(*x1 + ((((int32_t)video::screen_window_y2 - (int32_t)*y1) * dx) / dy));
                    *y2 = video::screen_window_y2;
                }

                if (*y2 < *y1) {
                    is_clipped = 1;
                }
            }
        }

        return is_clipped;
    }

    void __cdecl Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color) {
        render::twin::Scope twin_scope("Line", video::current_video_page, SDL_min(x1, x2), SDL_min(y1, y2), SDL_abs(x2 - x1) + 1, SDL_abs(y2 - y1) + 1);
        if (animate::clipping_flag == 1) {
            if (Clip_Line(&x1, &y1, &x2, &y2) == 1) {
                return;
            }
        }

        if (y2 < y1) {
            strings::Swap_Short(&y1, &y2);
            strings::Swap_Short(&x1, &x2);
        }

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
        uint8_t* render_ptr =
            video::current_video_page + (int32_t)y1 * video::g_screen_res_x + (int32_t)x1;

        int16_t dx = x2 - x1;
        int16_t x_step;
        if (dx < 0) {
            dx = -dx;
            x_step = -1;
        } else {
            x_step = 1;
        }

        int16_t dy = y2 - y1;

        if (dx == 0) {
            int16_t count = dy + 1;
            do {
                *render_ptr = color;
                render::Stamp_Run(render_ptr, 1);
                render_ptr += video::g_screen_res_x;
                count--;
            } while (count != 0);

            if (video::screen_refresh_flag >= 2) {
                for (int16_t y = y1; y <= y2; ++y) {
                    video::Add_Dot_To_Scan_List(x1, y);
                }
            }
            platform::Unlock_Appropriate_Render_Buffer();
            return;
        }

        if (dy == 0) {
            int16_t count = dx + 1;
            do {
                *render_ptr = color;
                render::Stamp_Run(render_ptr, 1);
                render_ptr += x_step;
                count--;
            } while (count != 0);

            if (video::screen_refresh_flag >= 2) {
                int16_t rx1 = x1, rx2 = x2;
                if (rx2 < rx1) strings::Swap_Short(&rx1, &rx2);
                setvesa::Add_Square_To_Scan_List(rx1, y1, rx2, y1);
            }
            platform::Unlock_Appropriate_Render_Buffer();
            return;
        }

        if (dy == dx) {
            int16_t count = dx + 1;
            do {
                *render_ptr = color;
                render::Stamp_Run(render_ptr, 1);
                render_ptr += (video::g_screen_res_x + x_step);
                count--;
            } while (count != 0);

            if (video::screen_refresh_flag >= 2) {
                if (x1 < x2) {
                    for (int16_t y = y1; y <= y2; ++y) {
                        video::Add_Dot_To_Scan_List(x1 - y1 + y, y);
                    }
                } else {
                    for (int16_t y = y1; y <= y2; ++y) {
                        video::Add_Dot_To_Scan_List(y1 - y + x1, y);
                    }
                }
            }
            platform::Unlock_Appropriate_Render_Buffer();
            return;
        }

        if (dy < dx) {
            int16_t run_len = dx / dy;
            int16_t rem = (dx % dy) * 2;
            int16_t error = (dx % dy) - (dy * 2);
            int16_t initial_run = (run_len / 2) + 1;
            int16_t final_run = initial_run;

            if (rem == 0) {
                if ((run_len & 1) == 0) initial_run--;
            }
            if ((run_len & 1) != 0) {
                error += dy;
            }

            Draw_Horizontal_Run(&render_ptr, x_step, initial_run, color);
            if (video::screen_refresh_flag >= 2) {
                if (x_step > 0) {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + initial_run, y1);
                    x1 += initial_run;
                } else {
                    setvesa::Add_Square_To_Scan_List(x1 - initial_run, y1, x1, y1);
                    x1 -= initial_run;
                }
                y1++;
            }

            for (int i = 0; i < dy - 1; ++i) {
                error += rem;
                int16_t current_run = run_len;
                if (error > 0) {
                    current_run++;
                    error -= (dy * 2);
                }
                Draw_Horizontal_Run(&render_ptr, x_step, current_run, color);
                if (video::screen_refresh_flag >= 2) {
                    if (x_step > 0) {
                        setvesa::Add_Square_To_Scan_List(x1, y1, x1 + current_run, y1);
                        x1 += current_run;
                    } else {
                        setvesa::Add_Square_To_Scan_List(x1 - current_run, y1, x1, y1);
                        x1 -= current_run;
                    }
                    y1++;
                }
            }

            Draw_Horizontal_Run(&render_ptr, x_step, final_run, color);
            if (video::screen_refresh_flag >= 2) {
                if (x_step > 0) {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + final_run, y1);
                } else {
                    setvesa::Add_Square_To_Scan_List(x1 - final_run, y1, x1, y1);
                }
            }
        } else {
            int16_t run_len = dy / dx;
            int16_t rem = (dy % dx) * 2;
            int16_t error = (dy % dx) - (dx * 2);
            int16_t initial_run = (run_len / 2) + 1;
            int16_t final_run = initial_run;

            if (rem == 0) {
                if ((run_len & 1) == 0) initial_run--;
            }
            if ((run_len & 1) != 0) {
                error += dx;
            }

            Draw_Vertical_Run(&render_ptr, x_step, initial_run, color);
            if (video::screen_refresh_flag >= 2) {
                for (int16_t i = 0; i < initial_run; ++i) {
                    video::Add_Dot_To_Scan_List(x1, y1 + i);
                }
                y1 += initial_run;
                x1 += x_step;
            }

            for (int i = 0; i < dx - 1; ++i) {
                error += rem;
                int16_t current_run = run_len;
                if (error > 0) {
                    current_run++;
                    error -= (dx * 2);
                }
                Draw_Vertical_Run(&render_ptr, x_step, current_run, color);
                if (video::screen_refresh_flag >= 2) {
                    for (int16_t j = 0; j < (int16_t)current_run; ++j) {
                        video::Add_Dot_To_Scan_List(x1, y1 + j);
                    }
                    y1 += current_run;
                    x1 += x_step;
                }
            }

            Draw_Vertical_Run(&render_ptr, x_step, final_run, color);
            if (video::screen_refresh_flag >= 2) {
                for (int16_t i = 0; i < (int16_t)final_run; ++i) {
                    video::Add_Dot_To_Scan_List(x1, y1 + i);
                }
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Draw_Vertical_Run(uint8_t** buf, int16_t post_offset, int16_t count, uint8_t color) {
        uint8_t* current_ptr = *buf;

        if (count > 0) {
            int32_t remaining = (int32_t)count;
            do {
                *current_ptr = color;
                render::Stamp_Run(current_ptr, 1);
                current_ptr += video::g_screen_res_x;
                remaining--;
            } while (remaining != 0);
        }

        *buf = current_ptr + post_offset;
    }

    void __cdecl Draw_Horizontal_Run(uint8_t** buf, int16_t step_x, int16_t count, uint8_t color) {
        uint8_t* current_pos = *buf;
        if (count > 0) {
            int32_t remaining = (int32_t)count;
            do {
                *current_pos = color;
                render::Stamp_Run(current_pos, 1);
                current_pos += step_x;
                remaining--;
            } while (remaining != 0);
        }

        *buf = current_pos + video::g_screen_res_x;
    }

    void __cdecl Plasma_Box(int16_t x, int16_t y, uint16_t width, int16_t height, int16_t thickness, int16_t seed, uint8_t start_value, const uint8_t* color_map) {
        Create_Plasma_Line(seed, start_value, color_map);

        if (thickness > 0) {
            for (int16_t i = 0; i < thickness; ++i) {
                int16_t y1 = y + i;
                int16_t x2 = x - i + width - 1;
                int16_t x1 = x + i;

                Plasma_Line(x1, y1, x2, y1, replace::graphics_buffer, 0);

                int16_t y2 = y - i + height - 1;
                Plasma_Line(x2, y1, x2, y2, replace::graphics_buffer, width);

                for (int16_t mirror_index = 0; mirror_index < 511; ++mirror_index) {
                    uint8_t mirrored_pixel = replace::graphics_buffer[mirror_index];
                    replace::graphics_buffer[mirror_index] = replace::graphics_buffer[1022 - mirror_index];
                    replace::graphics_buffer[1022 - mirror_index] = mirrored_pixel;
                }

                Plasma_Line(x1, y1, x1, y2, replace::graphics_buffer, height + width);
                Plasma_Line(x1, y2, x2, y2, replace::graphics_buffer, height + width * 2);
            }
        }
    }

    void __cdecl Draw_Horizontal_Run_Multi(uint8_t** dest_ptr, int16_t step, int16_t length, s_colors* colors, uint16_t* color_index) {
        uint8_t* ptr = *dest_ptr;
        if (length > 0) {
            int32_t count = length;
            do {
                *ptr = colors->colors[*color_index & 7];
                render::Stamp_Run(ptr, 1);
                ptr += step;
                (*color_index)++;
                count--;
            } while (count != 0);
        }
        *dest_ptr = ptr + video::g_screen_res_x;
    }

    void __cdecl Draw_Vertical_Run_Multi(uint8_t** screen_ptr, int16_t post_offset, int16_t height, s_colors* colors, uint16_t* color_index) {
        uint8_t* dest = *screen_ptr;

        for (int16_t i = 0; i < height; ++i) {
            *dest = colors->colors[*color_index & 7];
            render::Stamp_Run(dest, 1);
            (*color_index)++;
            dest += video::g_screen_res_x;
        }

        *screen_ptr = dest + post_offset;
    }

    void __cdecl Plasma_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t* color_map, uint16_t color_index) {
        render::twin::Scope twin_scope("Plasma_Line", video::current_video_page, SDL_min(x1, x2), SDL_min(y1, y2), SDL_abs(x2 - x1) + 1, SDL_abs(y2 - y1) + 1);
        if (animate::clipping_flag == 1) {
            if (line::Clip_Line(&x1, &y1, &x2, &y2) == 1) {
                return;
            }
        }

        if (y1 > y2) {
            strings::Swap_Short(&y1, &y2);
            strings::Swap_Short(&x1, &x2);
        }

        uint8_t* ptr = video::current_video_page + y1 * video::g_screen_res_x + x1;
        int16_t dx = x2 - x1;
        int16_t x_dir;

        if (dx >= 0) {
            x_dir = 1;
        } else {
            dx = -dx;
            x_dir = -1;
        }

        int16_t dy = y2 - y1;

        if (dx == 0) {
            if (dy >= 0) {
                int16_t count = dy + 1;
                do {
                    *ptr = color_map[color_index & 0x3ff];
                    render::Stamp_Run(ptr, 1);
                    color_index++;
                    ptr += video::g_screen_res_x;
                    count--;
                } while (count != 0);
            }

            if (video::screen_refresh_flag > 1) {
                if (y1 <= y2) {
                    int16_t y = y1;
                    do {
                        video::Add_Dot_To_Scan_List(x1, y);
                        y++;
                    } while (y <= y2);
                }
            }
            return;
        }

        if (dy == 0) {
            if (dx >= 0) {
                int16_t count = dx + 1;
                do {
                    *ptr = color_map[color_index & 0x3ff];
                    render::Stamp_Run(ptr, 1);
                    color_index++;
                    ptr += x_dir;
                    count--;
                } while (count != 0);
            }

            if (video::screen_refresh_flag > 1) {
                if (x1 > x2) {
                    strings::Swap_Short(&x2, &x1);
                }
                setvesa::Add_Square_To_Scan_List(x1, y1, x2, y1);
            }
            return;
        }

        if (dx == dy) {
            if (dx >= 0) {
                int16_t count = dx + 1;
                do {
                    *ptr = color_map[color_index & 0x3ff];
                    render::Stamp_Run(ptr, 1);
                    color_index++;
                    ptr += video::g_screen_res_x + x_dir;
                    count--;
                } while (count != 0);
            }

            if (video::screen_refresh_flag > 1) {
                if (x1 >= x2) {
                    if (y1 <= y2) {
                        int16_t y = y1;
                        do {
                            video::Add_Dot_To_Scan_List(x1 - (y - y1), y);
                            y++;
                        } while (y <= y2);
                    }
                } else {
                    if (y1 <= y2) {
                        int16_t y = y1;
                        do {
                            video::Add_Dot_To_Scan_List(x1 + (y - y1), y);
                            y++;
                        } while (y <= y2);
                    }
                }
            }
            return;
        }

        if (dx > dy) {
            int16_t run_len = dx / dy;
            int16_t err_inc = (dx % dy) * 2;
            int16_t err = (dx % dy) - dy * 2;
            int16_t run_len_half = (run_len / 2) + 1;
            int16_t first_run = run_len_half;

            if (err_inc == 0) {
                if ((run_len & 1) == 0) {
                    first_run--;
                } else {
                    err += dy;
                }
            } else {
                if ((run_len & 1) != 0) {
                    err += dy;
                }
            }

            line::Draw_Horizontal_Run_Plasma(&ptr, x_dir, first_run, color_map, &color_index);

            if (video::screen_refresh_flag > 1) {
                if (x_dir <= 0) {
                    setvesa::Add_Square_To_Scan_List(x1 - first_run, y1, x1, y1);
                    x1 -= first_run;
                } else {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + first_run, y1);
                    x1 += first_run;
                }
                y1++;
            }

            int32_t num_runs = dy - 1;
            if (num_runs > 0) {
                for (int32_t i = 0; i < num_runs; i++) {
                    err += err_inc;
                    int16_t run = run_len;
                    if (err > 0) {
                        run++;
                        err -= dy * 2;
                    }

                    line::Draw_Horizontal_Run_Plasma(&ptr, x_dir, run, color_map, &color_index);

                    if (video::screen_refresh_flag > 1) {
                        if (x_dir <= 0) {
                            setvesa::Add_Square_To_Scan_List(x1 - run, y1, x1, y1);
                            x1 -= run;
                        } else {
                            setvesa::Add_Square_To_Scan_List(x1, y1, x1 + run, y1);
                            x1 += run;
                        }
                        y1++;
                    }
                }
            }

            line::Draw_Horizontal_Run_Plasma(&ptr, x_dir, run_len_half, color_map, &color_index);

            if (video::screen_refresh_flag > 1) {
                if (x_dir <= 0) {
                    setvesa::Add_Square_To_Scan_List(x1 - run_len_half, y1, x1, y1);
                } else {
                    setvesa::Add_Square_To_Scan_List(x1, y1, x1 + run_len_half, y1);
                }
            }
        } else {
            int16_t run_len = dy / dx;
            int16_t err_inc = (dy % dx) * 2;
            int16_t err = (dy % dx) - dx * 2;
            int16_t run_len_half = (run_len / 2) + 1;
            int16_t first_run = run_len_half;

            if (err_inc == 0) {
                if ((run_len & 1) == 0) {
                    first_run--;
                } else {
                    err += dx;
                }
            } else {
                if ((run_len & 1) != 0) {
                    err += dx;
                }
            }

            line::Draw_Vertical_Run_Plasma(&ptr, x_dir, first_run, color_map, &color_index);

            if (video::screen_refresh_flag > 1) {
                int16_t i = 0;
                if (first_run > 0) {
                    do {
                        video::Add_Dot_To_Scan_List(x1, y1 + i);
                        i++;
                    } while (i < first_run);
                }
                y1 += first_run;
                x1 += x_dir;
            }

            int32_t num_runs = dx - 1;
            if (num_runs > 0) {
                for (int32_t i = 0; i < num_runs; i++) {
                    err += err_inc;
                    int16_t run = run_len;
                    if (err > 0) {
                        run++;
                        err -= dx * 2;
                    }

                    line::Draw_Vertical_Run_Plasma(&ptr, x_dir, run, color_map, &color_index);

                    if (video::screen_refresh_flag > 1) {
                        int16_t j = 0;
                        if (run > 0) {
                            do {
                                video::Add_Dot_To_Scan_List(x1, y1 + j);
                                j++;
                            } while (j < run);
                        }
                        y1 += run;
                        x1 += x_dir;
                    }
                }
            }

            line::Draw_Vertical_Run_Plasma(&ptr, x_dir, run_len_half, color_map, &color_index);

            if (video::screen_refresh_flag > 1) {
                int16_t i = 0;
                if (run_len_half > 0) {
                    do {
                        video::Add_Dot_To_Scan_List(x1, y1 + i);
                        i++;
                    } while (i < run_len_half);
                }
            }
        }
    }

    void __cdecl Draw_Vertical_Run_Plasma(uint8_t** pixel_ptr_ref, int16_t post_offset, int16_t length, uint8_t* plasma_table, uint16_t* plasma_index_ptr) {
        uint8_t* current_pixels = *pixel_ptr_ref;

        if (length > 0) {
            int32_t count = (int32_t)length;
            do {
                // Look up color from plasma table using a 10-bit masked index
                uint16_t index = *plasma_index_ptr;
                *current_pixels = plasma_table[index & 0x3FF /* 1023 */];
                render::Stamp_Run(current_pixels, 1);

                // Increment cyclic index and move to the next screen row
                *plasma_index_ptr = index + 1;
                current_pixels += video::g_screen_res_x;

                count--;
            } while (count != 0);
        }

        // Apply final offset (typically to move to the next column or segment) and update output pointer
        *pixel_ptr_ref = current_pixels + post_offset;
    }

    void __cdecl Draw_Horizontal_Run_Plasma(uint8_t** dest_ptr, int16_t step, int16_t length, uint8_t* plasma_table, uint16_t* plasma_index_ptr) {
        uint8_t* dest = *dest_ptr;

        if (length > 0) {
            int32_t count = (int32_t)length;
            do {
                *dest = plasma_table[*plasma_index_ptr & 0x3FF];
                render::Stamp_Run(dest, 1);
                dest += step;
                (*plasma_index_ptr)++;
                count--;
            } while (count != 0);
        }

        *dest_ptr = dest + video::g_screen_res_x;
    }

    void __cdecl Create_Plasma_Line(int16_t seed, uint16_t start_value, const uint8_t* color_map) {
        game_random::Save_Random_Seed();
        game_random::Set_Random_Seed(seed);

        replace::graphics_buffer[0] = (uint8_t)(start_value & 0x7);

        int8_t direction_state = (int8_t)game_random::Random(4) - 1;
        int8_t step_dir = (direction_state == 0) ? -1 : 1;

        for (int16_t sample_index = 1; sample_index < 1023; ++sample_index) {
            int8_t val = replace::graphics_buffer[sample_index - 1];

            if (game_random::Random(4) == 1) {
                direction_state = 1 - direction_state;
                step_dir = (direction_state == 0) ? -1 : 1;
            }

            if (game_random::Random(4) == 1) {
                val += step_dir;
                if (game_random::Random(4) == 1) {
                    val += step_dir;
                    if (game_random::Random(4) == 1) {
                        val += step_dir;
                    }
                }
            }

            if (val < 0) {
                val = 0;
            } else if (val > 7) {
                val = 7;
            }

            replace::graphics_buffer[sample_index] = (uint8_t)val;
        }

        for (int16_t sample_index = 1; sample_index < 1023; ++sample_index) {
            replace::graphics_buffer[sample_index] = color_map[replace::graphics_buffer[sample_index]];
        }

        game_random::Restore_Random_Seed();
    }

} // namespace line