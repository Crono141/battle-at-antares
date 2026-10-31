#include "pch.h"

namespace draw {
    /**
     * @brief Draws a sprite to the current video page using the dimensions from the global animation header.
     * @param x Left destination position.
     * @param y Top destination position.
     * @param bitmap_data Source pixel data laid out row-by-row.
     *
     * The function performs basic transparency (skips pixel value 0).
     */
    void __cdecl Draw_Bitmap_Sprite(int16_t x, int16_t y, uint8_t* bitmap_data) {
        uint16_t width = (uint16_t)animate::animation_header->width;
        uint16_t height = (uint16_t)animate::animation_header->height;
        uint8_t* video_page = video::current_video_page;

        uint8_t* dst = video_page + (uint16_t)x + (uint16_t)y * 640u - 640u + width;

        while (height != 0) {
            --height;
            dst += 640;
            dst -= width;
            --dst;

            uint32_t count = width;
            while (true) {
                ++dst;
                if (count == 0) {
                    break;
                }

                --count;
                uint8_t pixel = *bitmap_data++;
                if (pixel != 0) {
                    *dst = pixel;
                    render::Stamp_Run(dst, 1);
                }
            }
        }
    }

    /**
     * @brief Copies a stream of colors from src to dst, skipping color 0 (transparency).
     */
    void __cdecl Color_Stream_Copy(uint8_t* src, uint8_t* dst, int32_t pixel_count) {
        for (int32_t i = 0; i < pixel_count; ++i) {
            uint8_t color = src[i];
            if (color != 0x00) {
                dst[i] = color;
                render::Stamp_Run(dst + i, 1);
            }
        }
    }

    /**
     * @brief Draws a packed animated-sprite frame to the current video page.
     * @param x Left destination position.
     * @param y Top destination position.
     * @param frame_data Packed frame data beginning with an s_packed_frame_header.
     */
    void __cdecl Draw_Animated_Sprite(int16_t x, int16_t y, uint8_t* frame_data) {
        s_packed_frame_header frame_header;
        memcpy(&frame_header, frame_data, sizeof(frame_header));

        uint32_t x_pos = (uint16_t)x;
        uint32_t y_pos = (uint16_t)y;
        uint32_t sprite_y_pos = frame_header.start_y;
        int32_t remaining_height =
            (int32_t)(uint16_t)animate::animation_header->height - (int32_t)frame_header.start_y;

        uint8_t* dest_row_start = video::current_video_page +
            (y_pos + sprite_y_pos) * 640u + x_pos;
        uint8_t* dest_ptr = dest_row_start;
        uint8_t* stream = frame_data + sizeof(frame_header);

        while (remaining_height > 0) {
            s_packed_frame_run run;
            memcpy(&run, stream, sizeof(run));
            stream += sizeof(run);

            if (run.pixel_count == 0) {
                remaining_height -= run.skip_count;
                dest_row_start += (uint32_t)run.skip_count * 640u;
                dest_ptr = dest_row_start;
            } else {
                dest_ptr += run.skip_count;

                memcpy(dest_ptr, stream, run.pixel_count);
                render::Stamp_Run(dest_ptr, run.pixel_count);
                dest_ptr += run.pixel_count;
                stream += run.pixel_count;

                if ((uintptr_t)stream & 1) {
                    stream++;
                }
            }
        }
    }
} // namespace draw