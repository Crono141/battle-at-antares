#include "pch.h"
#include "antares/combat_native.h"

namespace bolt {
    int16_t head_wide_bolts[192] = {
        0,
        -1,
        0,
        1,
        -1,
        -1,
        -1,
        1,
        -1,
        -2,
        -1,
        2,
        -1,
        -1,
        0,
        1,
        -1,
        0,
        -1,
        1,
        -2,
        -1,
        -1,
        2,
        -1,
        0,
        1,
        0,
        -1,
        0,
        0,
        1,
        1,
        1,
        -1,
        -1,
        -1,
        0,
        1,
        1,
        -1,
        1,
        0,
        1,
        -2,
        1,
        1,
        2,
        -1,
        0,
        0,
        1,
        -1,
        1,
        1,
        1,
        -2,
        1,
        2,
        1,
        1,
        0,
        -1,
        1,
        1,
        1,
        0,
        1,
        2,
        1,
        -1,
        2,
        1,
        0,
        -1,
        0,
        1,
        0,
        0,
        1,
        -1,
        1,
        1,
        -1,
        1,
        -1,
        0,
        1,
        1,
        0,
        1,
        1,
        2,
        -1,
        1,
        2,
        0,
        -1,
        0,
        1,
        1,
        -1,
        1,
        1,
        1,
        -2,
        1,
        2,
        1,
        1,
        0,
        -1,
        1,
        0,
        1,
        -1,
        2,
        1,
        1,
        -2,
        1,
        0,
        -1,
        0,
        1,
        0,
        0,
        -1,
        -1,
        -1,
        1,
        1,
        1,
        0,
        -1,
        -1,
        1,
        -1,
        0,
        -1,
        2,
        -1,
        -1,
        -2,
        1,
        0,
        -1,
        -1,
        -1,
        -1,
        1,
        -1,
        2,
        -1,
        -1,
        -2,
        -1,
        0,
        1,
        -1,
        -1,
        -1,
        0,
        -1,
        -2,
        -1,
        1,
        -2,
        -1,
        0,
        1,
        0,
        -1,
        0,
        0,
        -1,
        1,
        -1,
        -1,
        1,
        -1,
        1,
        0,
        -1,
        -1,
        0,
        -1,
        -1,
        -2,
        1,
        -1,
        -2,
    };

    int16_t tail_wide_bolts[192] = {
        0,
        1,
        0,
        -1,
        1,
        -1,
        1,
        1,
        1,
        2,
        1,
        -2,
        0,
        -1,
        1,
        1,
        1,
        0,
        1,
        -1,
        1,
        -2,
        2,
        1,
        -1,
        0,
        1,
        0,
        1,
        0,
        0,
        -1,
        1,
        1,
        -1,
        -1,
        -1,
        -1,
        1,
        0,
        1,
        -1,
        0,
        -1,
        -1,
        -2,
        2,
        -1,
        -1,
        -1,
        1,
        0,
        -1,
        -1,
        1,
        -1,
        -1,
        -2,
        2,
        -1,
        1,
        -1,
        -1,
        0,
        -1,
        -1,
        0,
        -1,
        1,
        -2,
        -2,
        -1,
        1,
        0,
        -1,
        0,
        -1,
        0,
        0,
        -1,
        -1,
        1,
        1,
        -1,
        0,
        -1,
        -1,
        1,
        -1,
        0,
        -1,
        -1,
        -1,
        -2,
        -2,
        1,
        0,
        1,
        0,
        -1,
        -1,
        -1,
        -1,
        1,
        -1,
        2,
        -1,
        -2,
        0,
        1,
        -1,
        -1,
        -1,
        0,
        -1,
        1,
        -1,
        2,
        -2,
        -1,
        1,
        0,
        -1,
        0,
        -1,
        0,
        0,
        1,
        -1,
        -1,
        1,
        1,
        1,
        1,
        -1,
        0,
        -1,
        1,
        0,
        1,
        1,
        2,
        -2,
        1,
        0,
        1,
        -1,
        0,
        -1,
        1,
        1,
        1,
        2,
        1,
        -2,
        1,
        -1,
        1,
        1,
        0,
        1,
        1,
        0,
        1,
        -1,
        2,
        2,
        1,
        -1,
        0,
        1,
        0,
        1,
        0,
        0,
        1,
        1,
        -1,
        -1,
        1,
        0,
        1,
        1,
        -1,
        1,
        0,
        1,
        1,
        1,
        2,
        2,
        -1,
    };

    void __cdecl Wide_Bolt_Line(
        int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t palette_base, int16_t palette_span, int16_t color_jitter_chance, int16_t color_jitter_range, uint16_t sparkle_phase, int16_t legacy_core_flag, int16_t point_source_end, int16_t tip_highlight, int16_t full_core_gradient_flag, int16_t apply_sparkles, int16_t legacy_side_flag, int16_t core_max_offset, int16_t core_min_offset, int16_t mid_max_offset, int16_t mid_min_offset, int16_t outer_max_offset, int16_t outer_min_offset) {
        (void)legacy_core_flag;
        (void)legacy_side_flag;

        int16_t angle = angle::Get_Angle(x2 - x1, y2 - y1);
        int16_t direction_index = (((371 - angle) % 360) * 2) / 45;

        int16_t x1_1, y1_1, x2_1, y2_1;
        int16_t x1_2, y1_2, x2_2, y2_2;

        if (point_source_end == 1) {
            x1_1 = x1;
            y1_1 = y1;
            x2_1 = head_wide_bolts[direction_index * 12 + 8];
            y2_1 = head_wide_bolts[direction_index * 12 + 9];

            x1_2 = x1;
            y1_2 = y1;
            x2_2 = head_wide_bolts[direction_index * 12 + 10];
            y2_2 = head_wide_bolts[direction_index * 12 + 11];
        } else {
            x1_1 = tail_wide_bolts[direction_index * 12 + 8] + x1;
            y1_1 = tail_wide_bolts[direction_index * 12 + 9] + y1;
            x2_1 = head_wide_bolts[direction_index * 12 + 8];
            y2_1 = head_wide_bolts[direction_index * 12 + 9];

            x1_2 = tail_wide_bolts[direction_index * 12 + 10] + x1;
            y1_2 = tail_wide_bolts[direction_index * 12 + 11] + y1;
            x2_2 = head_wide_bolts[direction_index * 12 + 10];
            y2_2 = head_wide_bolts[direction_index * 12 + 11];
        }

        int16_t max_color = palette_base + palette_span - 1;
        int16_t side_rand_range = color_jitter_range / 2;

        Bolt_Segment(x1_1, y1_1, x2_1 + x2, y2_1 + y2, palette_base + outer_min_offset, outer_max_offset - outer_min_offset, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, 0);
        Bolt_Segment(x1_2, y1_2, x2_2 + x2, y2_2 + y2, palette_base + outer_min_offset, outer_max_offset - outer_min_offset, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, 0);

        int16_t x1_3, y1_3, x2_3, y2_3;
        int16_t x1_4, y1_4, x2_4, y2_4;
        int16_t x1_5, y1_5, x2_5, y2_5;
        int16_t x1_6, y1_6, x2_6, y2_6;

        if (point_source_end == 1) {
            x1_3 = x1;
            y1_3 = y1;
            x2_3 = head_wide_bolts[direction_index * 12 + 0];
            y2_3 = head_wide_bolts[direction_index * 12 + 1];

            x1_4 = x1;
            y1_4 = y1;
            x2_4 = head_wide_bolts[direction_index * 12 + 2];
            y2_4 = head_wide_bolts[direction_index * 12 + 3];

            x1_5 = x1;
            y1_5 = y1;
            x2_5 = head_wide_bolts[direction_index * 12 + 4];
            y2_5 = head_wide_bolts[direction_index * 12 + 5];

            x1_6 = x1;
            y1_6 = y1;
            x2_6 = head_wide_bolts[direction_index * 12 + 6];
            y2_6 = head_wide_bolts[direction_index * 12 + 7];
        } else {
            x1_3 = tail_wide_bolts[direction_index * 12 + 0] + x1;
            y1_3 = tail_wide_bolts[direction_index * 12 + 1] + y1;
            x2_3 = head_wide_bolts[direction_index * 12 + 0];
            y2_3 = head_wide_bolts[direction_index * 12 + 1];

            x1_4 = tail_wide_bolts[direction_index * 12 + 2] + x1;
            y1_4 = tail_wide_bolts[direction_index * 12 + 3] + y1;
            x2_4 = head_wide_bolts[direction_index * 12 + 2];
            y2_4 = head_wide_bolts[direction_index * 12 + 3];

            x1_5 = tail_wide_bolts[direction_index * 12 + 4] + x1;
            y1_5 = tail_wide_bolts[direction_index * 12 + 5] + y1;
            x2_5 = head_wide_bolts[direction_index * 12 + 4];
            y2_5 = head_wide_bolts[direction_index * 12 + 5];

            x1_6 = tail_wide_bolts[direction_index * 12 + 6] + x1;
            y1_6 = tail_wide_bolts[direction_index * 12 + 7] + y1;
            x2_6 = head_wide_bolts[direction_index * 12 + 6];
            y2_6 = head_wide_bolts[direction_index * 12 + 7];
        }

        int16_t mid_color_base = mid_min_offset + palette_base;
        int16_t mid_color_range = mid_max_offset - mid_min_offset;

        Bolt_Segment(x1_3, y1_3, x2_3 + x2, y2_3 + y2, mid_color_base, mid_color_range, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, tip_highlight);
        Bolt_Segment(x1_4, y1_4, x2_4 + x2, y2_4 + y2, mid_color_base, mid_color_range, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, tip_highlight);
        Bolt_Segment(x1_5, y1_5, x2_5 + x2, y2_5 + y2, mid_color_base, mid_color_range, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, tip_highlight);
        Bolt_Segment(x1_6, y1_6, x2_6 + x2, y2_6 + y2, mid_color_base, mid_color_range, color_jitter_chance, side_rand_range, palette_base, max_color, 0, apply_sparkles, sparkle_phase, tip_highlight);

        int16_t core_color_base = core_min_offset + palette_base;
        int16_t core_color_range = core_max_offset - core_min_offset;

        if (full_core_gradient_flag == 0) {
            core_color_base += (core_color_range * 3) / 4;
            core_color_range = 0;
        }

        Bolt_Segment(x1, y1, x2, y2, core_color_base, core_color_range, color_jitter_chance, color_jitter_range, palette_base, max_color, full_core_gradient_flag, apply_sparkles, sparkle_phase, tip_highlight);
    }

    void __cdecl Bolt_Segment(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t color_base, int16_t color_range, int16_t color_jitter_chance, int16_t color_jitter_range, int16_t min_color, int16_t max_color, int16_t legacy_segment_flag, int16_t apply_sparkles, uint16_t sparkle_phase, int16_t tip_highlight) {
        (void)legacy_segment_flag;

        int32_t rand_range2 = color_jitter_range * 2 + 1;
        int32_t dx = x2 - x1;
        int32_t dy = y2 - y1;

        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }

        int32_t max_d = dy;
        if (max_d < dx) {
            max_d = dx;
        }

        int32_t len = max_d + 1;
        int32_t step_val = ((int32_t)color_range << 16) / len;
        int32_t color_accum = 32000 / len;
        int32_t rand_val = 0;

        if (y2 < y1) {
            if (len >= 0) {
                for (int32_t i = 0; i <= len; ++i) {
                    uint8_t c = (uint8_t)((color_accum >> 16) + rand_val + color_base);
                    replace::graphics_buffer[len - i + 1] = c;
                    color_accum += step_val;

                    if ((int16_t)game_random::Random(100) < color_jitter_chance) {
                        rand_val = game_random::Random(rand_range2) - color_jitter_range;
                    } else {
                        rand_val = 0;
                    }
                }
            }
            replace::graphics_buffer[len] = (uint8_t)color_base;
            replace::graphics_buffer[0] = (uint8_t)(color_base + color_range - 1);
        } else {
            if (len >= 0) {
                for (int32_t i = 0; i <= len; ++i) {
                    uint8_t c = (uint8_t)((color_accum >> 16) + rand_val + color_base);
                    replace::graphics_buffer[i + 1] = c;
                    color_accum += step_val;

                    if ((int16_t)game_random::Random(100) < color_jitter_chance) {
                        rand_val = game_random::Random(rand_range2) - color_jitter_range;
                    } else {
                        rand_val = 0;
                    }
                }
            }
            replace::graphics_buffer[0] = (uint8_t)color_base;
            replace::graphics_buffer[len] = (uint8_t)(color_base + color_range - 1);
        }

        if (apply_sparkles == 1) {
            int32_t i = sparkle_phase % 8;
            int32_t end_i = len + 2;
            if (i < end_i) {
                do {
                    replace::graphics_buffer[i] += 3;
                    i += 8;
                } while (i < end_i);
            }
        }

        int32_t buf_len = len + 2;
        if (buf_len > 0) {
            for (int32_t i = 0; i < buf_len; ++i) {
                if (replace::graphics_buffer[i] < min_color) {
                    replace::graphics_buffer[i] = (uint8_t)min_color;
                }
                if (replace::graphics_buffer[i] > max_color) {
                    replace::graphics_buffer[i] = (uint8_t)max_color;
                }
            }
        }

        if (tip_highlight != 0) {
            int32_t fade_val = 3 / tip_highlight;
            if (y2 < y1) {
                for (int32_t i = 0; i <= 5; ++i) {
                    replace::graphics_buffer[i + 1] += fade_val;
                }
            } else {
                if (len > 5) {
                    for (int32_t i = 0; i <= 5; ++i) {
                        replace::graphics_buffer[len - i - 1] += fade_val;
                    }
                }
            }
        }

        if (buf_len > 0) {
            for (int32_t i = 0; i < buf_len; ++i) {
                if (replace::graphics_buffer[i] > color_base + 15) {
                    replace::graphics_buffer[i] = (uint8_t)(color_base + 15);
                }
            }
        }

        antares::combat::Record_Strand(x1, y1, x2, y2, replace::graphics_buffer, len + 2); // spike 3.9
        line::Plasma_Line(x1, y1, x2, y2, replace::graphics_buffer, 0);
    }

    void __cdecl Bolt_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t palette_base, int16_t palette_span, int16_t color_jitter_chance, int16_t color_jitter_range, uint16_t sparkle_phase, int16_t draw_caps, int16_t legacy_aux_flag, int16_t tip_highlight, int16_t full_core_gradient_flag, int16_t apply_sparkles, int16_t suppress_side_strands, int16_t core_max_offset, int16_t core_min_offset, int16_t side_max_offset, int16_t side_min_offset) {
        (void)legacy_aux_flag;

        if (draw_caps == 1) {
            uint8_t c_cross = (uint8_t)(palette_base + 1);
            uint8_t c_diag = (uint8_t)palette_base;
            if (tip_highlight == 1) {
                c_cross = (uint8_t)(palette_base + 4);
                c_diag = (uint8_t)(palette_base + 1);
            }

            uint8_t c_center = (uint8_t)(core_max_offset + palette_base);

            graphics::Dot(x2, y2, c_center);
            graphics::Dot(x2, y2 - 1, c_cross);
            graphics::Dot(x2, y2 + 1, c_cross);
            graphics::Dot(x2 - 1, y2, c_cross);
            graphics::Dot(x2 + 1, y2, c_cross);

            graphics::Dot(x2 - 1, y2 - 1, c_diag);
            graphics::Dot(x2 - 1, y2 + 1, c_diag);
            graphics::Dot(x2 + 1, y2 - 1, c_diag);
            graphics::Dot(x2 + 1, y2 + 1, c_diag);

            if (full_core_gradient_flag == 0) {
                graphics::Dot(x1, y1, c_center);
                graphics::Dot(x1, y1 - 1, c_cross);
                graphics::Dot(x1, y1 + 1, c_cross);
                graphics::Dot(x1 - 1, y1, c_cross);
                graphics::Dot(x1 + 1, y1, c_cross);

                graphics::Dot(x1 - 1, y1 - 1, c_diag);
                graphics::Dot(x1 - 1, y1 + 1, c_diag);
                graphics::Dot(x1 + 1, y1 - 1, c_diag);
                graphics::Dot(x1 + 1, y1 + 1, c_diag);
            }
        }

        int16_t x_off = 0;
        int16_t y_off = 0;
        if (abs(y2 - y1) >= abs(x2 - x1)) {
            x_off = 1;
        } else {
            y_off = 1;
        }

        int16_t flank_color_range = side_max_offset - side_min_offset;
        if (tip_highlight == 1) {
            flank_color_range += 3;
        }

        if (suppress_side_strands == 0) {
            bolt::Bolt_Segment(
                x1 - x_off, y1 - y_off, x2 - x_off, y2 - y_off, side_min_offset + palette_base, flank_color_range, color_jitter_chance, color_jitter_range / 2, palette_base, palette_span + palette_base - 1, 0, apply_sparkles, sparkle_phase, 0);

            bolt::Bolt_Segment(
                x1 + x_off, y1 + y_off, x2 + x_off, y2 + y_off, side_min_offset + palette_base, flank_color_range, color_jitter_chance, color_jitter_range / 2, palette_base, palette_span + palette_base - 1, 0, apply_sparkles, sparkle_phase, 0);
        }

        int16_t core_color_range = core_max_offset - core_min_offset;
        int16_t core_color_base = core_min_offset + palette_base;

        if (full_core_gradient_flag == 0) {
            int32_t t = (int32_t)core_color_range * 3;
            core_color_base += (int16_t)((t + ((t >> 31) & 3)) >> 2);
            core_color_range = 0;
        }

        bolt::Bolt_Segment(
            x1, y1, x2, y2, core_color_base, core_color_range, color_jitter_chance, color_jitter_range, palette_base, palette_span + palette_base - 1, full_core_gradient_flag, apply_sparkles, sparkle_phase, tip_highlight);
    }
} // namespace bolt