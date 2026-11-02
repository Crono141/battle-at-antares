#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"

// The facade's page and image plumbing, routed to the twin planes (twin*.cpp), and the cursor
// art. Until WP1.3b (2026-11-02) these also ran attempt 2's surfaces (planes.cpp, images.cpp,
// present.cpp), chosen by ANTARES_TWINS.
namespace render {

    // Guards allocating/freeing the page planes (game thread) against the presenter reading the
    // published/present planes.
    std::mutex planes_mutex;

    // ------------------------------------------------------------------ pages

    void Allocate_Planes(int32_t width, int32_t height) {
        if (enabled) {
            twin::Allocate_Pages(width, height);
        }
    }

    void Free_Planes() {
        twin::Free_Pages();
    }

    void Copy_Plane(const uint8_t* src_page, const uint8_t* dst_page) {
        if (enabled) {
            twin::Copy_Page(src_page, dst_page);
        }
    }

    void Copy_Plane_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count) {
        if (enabled) {
            twin::Copy_Page_Span(src_page, dst_page, offset, count);
        }
    }

    void Publish_Plane(const uint8_t* published_page_pixels) {
        (void)published_page_pixels;
        if (enabled) {
            twin::Publish();
        }
    }

    void Snapshot_Plane() {
        if (enabled) {
            twin::Snapshot();
        }
    }

    bool Resolve(const video::s_palette_color* palette, uint32_t* output, int32_t* row_first, int32_t* row_last) {
        return enabled && twin::Resolve(palette, output, row_first, row_last);
    }

    // ------------------------------------------------------------------ images

    namespace twin {
        uint32_t rewind_drops;
    }

    void Forget_Images(const void* begin, const void* end) {
        if (enabled && begin < end && !twin::images.empty()) {
            twin::rewind_drops += (uint32_t)twin::Drop_Images(begin, end);
        }
    }

    void Bitmap_Reset(const void* bitmap, int32_t width, int32_t height) {
        if (enabled) {
            twin::Attach_Image(bitmap, width, height);
        }
    }

    void Bitmap_Flip(const void* dst, const void* src, bool vertical) {
        if (enabled) {
            twin::Flip_Image(dst, src, vertical);
        }
    }

    void Bitmap_Copy(const void* dst, const void* src) {
        if (enabled) {
            twin::Copy_Image(dst, src);
        }
    }

    void Bitmap_Glass(const void* bitmap, const uint8_t* pixels, int32_t level) {
        (void)pixels;
        if (enabled) {
            twin::Glass_Image(bitmap, level);
        }
    }

    void Bitmap_Recolor(const void* bitmap, int32_t first, int32_t count, const uint8_t* map) {
        if (enabled) {
            twin::Recolor_Image(bitmap, first, count, map);
        }
    }

    // ------------------------------------------------------------------ cursor

    bool Cursor_Art(const uint8_t* pixels, int32_t size, const video::s_palette_color* palette, uint32_t* argb) {
        static std::unordered_map<uint32_t, s_hd_frame*> cursors;
        if (!enabled || pixels == nullptr || size <= 0) {
            return false;
        }
        uint32_t hash = 2166136261u;
        for (int32_t i = 0; i < size * size; ++i) {
            hash = (hash ^ pixels[i]) * 16777619u;
        }
        auto found = cursors.find(hash);
        if (found == cursors.end()) {
            // Small and rare: loaded synchronously on the presenter, through the VFS lock-free
            // path only when the file is on disk (the VFS is not thread safe).
            s_hd_frame* frame = new s_hd_frame();
            char path[64];
            snprintf(path, sizeof(path), "hd/CURSORS/%08x.png", hash);
            char disk[1024];
            size_t bytes_size = 0;
            void* bytes = vfs::Resolve_Read_Path_No_Zip(path, disk, sizeof(disk)) != 0 ? SDL_LoadFile(disk, &bytes_size) : nullptr;
            const bool ok = bytes != nullptr && Decode_Frame_Bytes((const uint8_t*)bytes, bytes_size, path, frame) &&
                frame->index != nullptr && frame->width == size * S && frame->height == size * S;
            SDL_free(bytes);
            if (!ok) {
                delete frame;
                frame = nullptr;
                if (stats_enabled) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd stats: no art for cursor %08x", hash);
                }
            }
            found = cursors.emplace(hash, frame).first;
        }
        const s_hd_frame* frame = found->second;
        if (frame == nullptr) {
            return false;
        }
        for (int32_t i = 0; i < frame->width * frame->height; ++i) {
            const video::s_palette_color& c = palette[frame->index[i]];
            argb[i] = ((uint32_t)frame->alpha[i] << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
        }
        return true;
    }

} // namespace render
