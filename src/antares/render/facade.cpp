#include "pch.h"
#include "miniz.h"
#include <cstring>
#include <set>
#include <mutex>

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include "antares/render/art_format.h"

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
        std::unordered_map<std::string, uint32_t> bitmap_twin_calls;
    } // namespace twin

    namespace {
        // ANTARES_HD_STATS: which bitmap twins ran (coverage of a test run).
        void Count(const char* name) {
            if (stats_enabled) {
                twin::bitmap_twin_calls[name]++;
            }
        }
    } // namespace

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

    void Bitmap_Capture(const void* bitmap, int32_t x1, int32_t y1, int32_t width, int32_t height) {
        if (enabled) {
            Count("Capture");
            twin::Capture_Image(bitmap, video::current_video_page, x1, y1, width, height);
        }
    }

    void Bitmap_Scale(const void* dst, const void* src) {
        if (enabled) {
            Count("Scale");
            twin::Scale_Image(dst, src);
        }
    }

    void Bitmap_Outline(const void* bitmap, int32_t color, bool no_glass) {
        if (enabled) {
            Count("Outline");
            twin::Outline_Image(bitmap, (uint8_t)color, no_glass);
        }
    }

    void Bitmap_Cleared(const void* bitmap) {
        if (enabled) {
            Count("Cleared");
            twin::Sync_Cleared(bitmap);
        }
    }

    void Bitmap_Vanish(const void* dst, const void* src) {
        if (enabled) {
            Count("Vanish");
            twin::Vanish_Image(dst, src);
        }
    }

    void Bitmap_Table(const void* bitmap, const uint8_t* table) {
        if (enabled) {
            Count("Table");
            twin::Table_Image(bitmap, table);
        }
    }

    void Bitmap_Brighten(const void* bitmap, int32_t dst_offset, const uint8_t* src, int32_t src_skip, int32_t dst_skip, int32_t width, int32_t height, const uint8_t* tables, int32_t start_color) {
        if (enabled) {
            Count("Brighten");
            twin::Brighten_Image(bitmap, dst_offset, src, src_skip, dst_skip, width, height, tables, start_color);
        }
    }

    void Bitmap_Jumble(const void* dst, const void* src, uint32_t mask, uint32_t threshold, uint32_t noise_start, uint32_t toggle_after, const uint8_t* noise_table) {
        if (enabled) {
            Count("Jumble");
            twin::Jumble_Image(dst, src, mask, threshold, noise_start, toggle_after, noise_table);
        }
    }

    void Bitmap_Replace_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* old_colors, const uint8_t* new_colors, int32_t count) {
        if (!enabled || old_colors == nullptr || new_colors == nullptr) {
            return;
        }
        // misc.cpp: each pair applies in turn to the pixel as it stands ((int8_t)old == pixel).
        uint8_t table[256];
        for (int32_t v = 0; v < 256; ++v) {
            uint8_t pixel = (uint8_t)v;
            for (int32_t i = 0; i < count; ++i) {
                if ((int8_t)old_colors[i] == pixel) {
                    pixel = new_colors[i];
                }
            }
            table[v] = pixel;
        }
        Bitmap_Map_Rect(bitmap, x0, y0, x1, y1, table);
    }

    void Bitmap_Swap_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* old_colors, const uint8_t* new_colors, int32_t count) {
        if (!enabled || old_colors == nullptr || new_colors == nullptr) {
            return;
        }
        uint8_t table[256]; // the first pair that matches wins
        for (int32_t v = 0; v < 256; ++v) {
            table[v] = (uint8_t)v;
            for (int32_t i = 0; i < count; ++i) {
                if (old_colors[i] == v) {
                    table[v] = new_colors[i];
                    break;
                }
            }
        }
        Bitmap_Map_Rect(bitmap, x0, y0, x1, y1, table);
    }

    void Bitmap_Tint_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t tint) {
        uint8_t table[256];
        for (int32_t v = 0; v < 256; ++v) {
            table[v] = v != 0 ? (uint8_t)(v + tint) : 0;
        }
        Bitmap_Map_Rect(bitmap, x0, y0, x1, y1, table);
    }

    void Bitmap_Colorize(const void* bitmap, const uint8_t* colors, int32_t first, int32_t last) {
        if (colors == nullptr) {
            return;
        }
        uint8_t table[256];
        for (int32_t v = 0; v < 256; ++v) {
            table[v] = v >= first && v <= last ? colors[v - first] : (uint8_t)v;
        }
        Bitmap_Map_Rect(bitmap, 0, 0, 0x7FFF, 0x7FFF, table);
    }

    void Recolor_All(s_animation_header* anim, uint8_t color) {
        if (!enabled || anim == nullptr) {
            return;
        }
        uint8_t map[255];
        memset(map, color, sizeof(map)); // every non-zero index
        Note_Recolor(anim, 1, 255, map);
        if ((anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_BITMAP) {
            Bitmap_Recolor(anim, 1, 255, map);
        }
    }

    void Recolor_One(s_animation_header* anim, int16_t old_color, uint8_t new_color) {
        // Sprite frames compare the pixel as a signed byte with old_color (replace.cpp), so only
        // -128..127 can match; bitmaps were handled by Bitmap_Recolor already.
        if (!enabled || anim == nullptr || old_color < -128 || old_color > 127 ||
            (anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_BITMAP) {
            return;
        }
        Note_Recolor(anim, (uint8_t)old_color, 1, &new_color);
    }

    void Bitmap_Map_Rect(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* table) {
        if (enabled) {
            Count("Map_Rect");
            twin::Map_Image_Rect(bitmap, x0, y0, x1, y1, table);
        }
    }

    void Bitmap_Plasma(const void* bitmap, const uint8_t* darken, int32_t darken_width, int32_t darken_height, int32_t darkness, const uint8_t* darken_map) {
        if (enabled) {
            Count("Plasma");
            twin::Plasma_Image(bitmap, darken, darken_width, darken_height, darkness, darken_map);
        }
    }

    void Bitmap_Line(const void* bitmap, int32_t mid_x, int32_t mid_y, int32_t angle, int32_t ext_x1, int32_t ext_x2, int32_t img_x1, int32_t img_y1, int32_t img_y2, int32_t scale_percent, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        if (enabled) {
            Count("Line");
            twin::Line_Image(video::current_video_page, bitmap, mid_x, mid_y, angle, ext_x1, ext_x2, img_x1, img_y1, img_y2, scale_percent, x1, y1, x2, y2);
        }
    }

    void Combat_Shield_Ship(const void* ship, int32_t x, int32_t y) {
        if (enabled) {
            twin::Shield_Ship(ship, x, y);
        }
    }

    void Combat_Shield(const s_shield& shield) {
        if (enabled) {
            Count("Shield");
            twin::Shield_Image(shield);
        }
    }

    // ------------------------------------------------------------------ cursor

    namespace {
        struct s_packaged_cursor {
            std::vector<uint8_t> png;
            std::vector<uint8_t> mask;
            std::string root; // "art" or "hd", for the frame's name
        };
        std::mutex s_packaged_mutex;
        std::unordered_map<uint32_t, s_packaged_cursor> s_packaged_cursors;
    } // namespace

    void Preload_Packaged_Cursors() {
        // Every cursor file in the zipped packages (art/CURSORS/<hash>.png, or the hd/ alias),
        // then each read through the VFS, so the highest-priority package's copy wins.
        std::set<std::string> names;
        for (const vfs::s_package_source& source : vfs::Package_Sources()) {
            if (!source.is_archive) continue;
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, source.source_path.c_str(), 0)) continue;
            const mz_uint count = mz_zip_reader_get_num_files(&zip);
            for (mz_uint index = 0; index < count; ++index) {
                char name[512];
                if (mz_zip_reader_get_filename(&zip, index, name, sizeof(name)) == 0) continue;
                std::string entry = name;
                for (const char* prefix : {"data/art/CURSORS/", "data/hd/CURSORS/"}) {
                    const size_t length = std::strlen(prefix);
                    if (entry.size() == length + 12 && entry.compare(0, length, prefix) == 0 && entry.compare(entry.size() - 4, 4, ".png") == 0) {
                        names.insert(entry.substr(5)); // without "data/": the VFS path
                    }
                }
            }
            mz_zip_reader_end(&zip);
        }
        std::unordered_map<uint32_t, s_packaged_cursor> found;
        for (const std::string& name : names) {
            const std::string root = name.substr(0, name.find('/'));
            const uint32_t hash = static_cast<uint32_t>(std::strtoul(name.substr(name.size() - 12, 8).c_str(), nullptr, 16));
            if (found.count(hash) != 0) continue; // art/ was listed before hd/
            s_packaged_cursor cursor;
            cursor.root = root;
            if (!Read_Vfs_File(name.c_str(), &cursor.png)) continue;
            (void)Read_Vfs_File((name.substr(0, name.size() - 4) + ".mask.png").c_str(), &cursor.mask);
            found.emplace(hash, std::move(cursor));
        }
        std::lock_guard<std::mutex> lock(s_packaged_mutex);
        s_packaged_cursors = std::move(found);
        if (!s_packaged_cursors.empty()) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: %zu cursors from zipped packages", s_packaged_cursors.size());
    }

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
            // art/ first, then its alias hd/ (PLAN.md section 5; the pack moved to art/ in WP2.4).
            char path[64];
            char disk[1024];
            size_t bytes_size = 0;
            void* bytes = nullptr;
            const char* root = "art";
            for (const char* candidate : {"art", "hd"}) {
                snprintf(path, sizeof(path), "%s/CURSORS/%08x.png", candidate, hash);
                if (vfs::Resolve_Read_Path_No_Zip(path, disk, sizeof(disk)) != 0) {
                    bytes = SDL_LoadFile(disk, &bytes_size);
                    root = candidate;
                    break;
                }
            }
            // Full colour (WP1.10): its mask beside it; converted indexed art decodes to indices.
            size_t mask_size = 0;
            void* mask = nullptr;
            if (bytes != nullptr && art::Is_Full_Colour((const uint8_t*)bytes, bytes_size)) {
                char mask_path[64];
                snprintf(mask_path, sizeof(mask_path), "%s/CURSORS/%08x.mask.png", root, hash);
                mask = vfs::Resolve_Read_Path_No_Zip(mask_path, disk, sizeof(disk)) != 0 ? SDL_LoadFile(disk, &mask_size) : nullptr;
            }
            // Not on disk: a zipped package's copy, read earlier on the main thread.
            if (bytes == nullptr) {
                std::lock_guard<std::mutex> lock(s_packaged_mutex);
                const auto packaged = s_packaged_cursors.find(hash);
                if (packaged != s_packaged_cursors.end()) {
                    bytes = SDL_malloc(packaged->second.png.size());
                    bytes_size = packaged->second.png.size();
                    if (bytes != nullptr) SDL_memcpy(bytes, packaged->second.png.data(), bytes_size);
                    snprintf(path, sizeof(path), "%s/CURSORS/%08x.png", packaged->second.root.c_str(), hash);
                    if (!packaged->second.mask.empty() && bytes != nullptr && art::Is_Full_Colour((const uint8_t*)bytes, bytes_size)) {
                        mask = SDL_malloc(packaged->second.mask.size());
                        mask_size = packaged->second.mask.size();
                        if (mask != nullptr) SDL_memcpy(mask, packaged->second.mask.data(), mask_size);
                    }
                }
            }
            const bool ok = bytes != nullptr && Decode_Frame_Bytes((const uint8_t*)bytes, bytes_size, path, frame, (const uint8_t*)mask, mask_size) &&
                (frame->index != nullptr || frame->rgb != nullptr) && frame->width == size * S && frame->height == size * S;
            SDL_free(bytes);
            SDL_free(mask);
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
            uint32_t rgb;
            if (frame->index != nullptr) {
                const video::s_palette_color& c = palette[frame->index[i]];
                rgb = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
            } else {
                // Painted full colour: masked pixels take their index's colour at their brightness.
                const uint8_t* p = &frame->rgb[(size_t)i * 3];
                rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                const uint8_t value = frame->mask != nullptr ? frame->mask[i] : 0;
                if (value != 0 && frame->shade_luma != nullptr) {
                    const video::s_palette_color& c = palette[value];
                    const uint32_t luma = art::Luma(p[0], p[1], p[2]);
                    const uint32_t reference = SDL_max((uint32_t)frame->shade_luma[value], 1u);
                    const uint32_t ratio = luma == frame->shade_luma[value] ? 256u : SDL_clamp(luma * 256u / reference, 64u, 768u);
                    rgb = twin::Scale_Rgb(((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b, ratio);
                }
            }
            argb[i] = ((uint32_t)frame->alpha[i] << 24) | rgb;
        }
        return true;
    }

} // namespace render
