#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"

// The current draw: the Begin_* hooks say which art a draw uses, where and how it is
// recoloured, and the hooked blitters report the 1x pixels they write ("stamps"); the twins
// (twin_draw.cpp) render the art into the planes. The stamp reports stay as the record of the
// touched pixels (PLAN.md 3.5 deviation, approved 2026-11-02); attempt 2's use of them (settle,
// art matching, shadows, deferred upgrades) was removed in WP1.3b.
namespace render {

    s_draw current_draw;

    namespace {

        // Ends the previous draw (its twin renders it with its own settings) and clears the
        // current one.
        void Start_Draw() {
            if (enabled) {
                twin::End_Draw();
            }
            current_draw.waiting = false;
            current_draw.source = SOURCE_NONE;
            current_draw.frame = nullptr;
            current_draw.glyph = false;
            current_draw.glassed = false;
            current_draw.art_outline = false;
            current_draw.remapped = false;
            current_draw.shade.clear();
            current_draw.anim = nullptr;
            current_draw.name.clear();
        }

        // The clip window at the start of an art draw: its fringe stays inside it.
        void Note_Clip_Window() {
            if (animate::clipping_flag == 1) {
                current_draw.clip_x1 = video::screen_window_x1;
                current_draw.clip_y1 = video::screen_window_y1;
                current_draw.clip_x2 = video::screen_window_x2;
                current_draw.clip_y2 = video::screen_window_y2;
            } else {
                current_draw.clip_x1 = current_draw.clip_y1 = 0;
                current_draw.clip_x2 = current_draw.clip_y2 = 0x7FFF;
            }
        }

        // Sets up the current draw for `anim` drawn with its top-left at (x, y).
        void Begin_Source(const s_animation_header* anim, int16_t x, int16_t y) {
            Start_Draw();
            if (!enabled || anim == nullptr) {
                return;
            }
            current_draw.anim = anim;
            current_draw.x = x;
            current_draw.y = y;
            // Also for pictures made at run time (bitmap::Set_Picture_Glassed): their glass is
            // drawn from their plane (twin_draw.cpp Image_Block).
            current_draw.glassed = (anim->flags & ANIMATION_FLAG_GLASSED) != 0;
            if (!capture_dir.empty() || probe_x >= 0 || stats_enabled) {
                const auto name = stats_names.find(anim);
                if (name != stats_names.end()) {
                    current_draw.name = name->second;
                    if (!capture_dir.empty()) {
                        Capture_Palette(name->second);
                    }
                }
            }
            const auto found = registrations.find(anim);
            if (found == registrations.end()) {
                if (stats_enabled) {
                    Stats_Missing(!current_draw.name.empty() ? current_draw.name
                                                             : "unregistered:" + std::to_string(anim->width) + "x" + std::to_string(anim->height));
                }
                return;
            }
            // The buffer may have been reused for other data since it was loaded; the header
            // (dimensions, frame count, flags) must still match, apart from the frame counter.
            uint8_t header[12];
            memcpy(header, anim, sizeof(header));
            memcpy(header + 4, found->second.header + 4, 2);
            if (memcmp(header, found->second.header, sizeof(header)) != 0) {
                return;
            }
            s_hd_asset* asset = assets[found->second.asset];
            if (!asset->loaded) {
                Load_Asset(asset, found->second.header);
                Prefetch_Asset(asset);
            }
            if (asset->has_source_hash && asset->source_hash != found->second.data_hash) {
                if (stats_enabled) {
                    Stats_Missing("source-mismatch:" + current_draw.name);
                }
                return;
            }
            const s_hd_frame* frame = Resolve_Frame(asset, anim->current_frame);
            if (frame == nullptr) {
                return;
            }
            asset->last_used = publish_count;
            if (!Frame_Ready(frame)) {
                Request_Frame(const_cast<s_hd_frame*>(frame), true);
                current_draw.waiting = true;
            }
            current_draw.source = SOURCE_ART;
            current_draw.frame = frame;
            current_draw.x = x;
            current_draw.y = y;
            if (found->second.recolored) {
                memcpy(current_draw.remap, found->second.recolor, sizeof(current_draw.remap));
                current_draw.remapped = true;
            }
            current_draw.glassed = asset->glassed || (anim->flags & ANIMATION_FLAG_GLASSED) != 0;
            current_draw.art_outline = asset->art_outline;
            Note_Clip_Window();
        }

    } // namespace

    void Begin_Draw(const s_animation_header* anim, int16_t x, int16_t y) {
        Begin_Source(anim, x, y);
    }

    void Begin_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap) {
        Begin_Source(anim, x, y);
        if (remap == nullptr) {
            return;
        }
        if (current_draw.source == SOURCE_ART) {
            if (current_draw.remapped) {
                // The art was recoloured in place first (Note_Recolor); this draw's remap applies after.
                for (int32_t i = 0; i < 256; ++i) {
                    current_draw.remap[i] = remap[current_draw.remap[i]];
                }
            } else {
                memcpy(current_draw.remap, remap, sizeof(current_draw.remap));
            }
            current_draw.remapped = true;
        }
    }

    void Begin_Masked_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap, const int8_t* mask, const int8_t* gray_percentages, int32_t level_count) {
        Begin_Remap_Draw(anim, x, y, remap);
        if (current_draw.source != SOURCE_ART || mask == nullptr || anim == nullptr) {
            return;
        }
        current_draw.shade_width = anim->width;
        current_draw.shade_height = anim->height;
        current_draw.shade.resize((size_t)anim->width * anim->height);
        for (size_t i = 0; i < current_draw.shade.size(); ++i) {
            const int32_t level = mask[i];
            // Level 0 is drawn unshaded; level n darkens by gray_percentages[n] percent.
            const int32_t percent = level <= 0 || level >= level_count ? 0 : gray_percentages[level];
            current_draw.shade[i] = (uint8_t)SDL_clamp(100 - percent, 0, 100);
        }
    }

    void Begin_Draw_To_Bitmap(const s_animation_header* src, int16_t x, int16_t y, const void* target_bitmap) {
        (void)target_bitmap;
        Begin_Source(src, x, y);
    }

    void Begin_Glyph(const char* font_file, int16_t font_index, uint8_t code, int16_t x, int16_t y, const uint8_t* colors) {
        if (!initialized) {
            Initialize();
        }
        Start_Draw();
        if (!enabled || font_file == nullptr || colors == nullptr) {
            return;
        }
        const s_hd_frame* glyph = Glyph_Art(font_file, font_index, code);
        if (glyph == nullptr) {
            return;
        }
        current_draw.source = SOURCE_ART;
        current_draw.frame = glyph;
        current_draw.glyph = true;
        current_draw.x = x;
        current_draw.y = y;
        memcpy(current_draw.colors, colors, sizeof(current_draw.colors));
        Note_Clip_Window();
        if (probe_x >= 0) {
            current_draw.name = std::string("glyph:") + font_file + "/" + std::to_string(font_index) + "/" + std::to_string(code);
        }
    }

    void Begin_Asset_Draw(const char* lbx_name, int32_t entry, int16_t frame, int16_t x, int16_t y) {
        if (!initialized) {
            Initialize();
        }
        Start_Draw();
        if (!enabled || lbx_name == nullptr) {
            return;
        }
        const int32_t asset_index = Find_Or_Create_Asset(lbx_name, entry);
        if (asset_index < 0) {
            return;
        }
        s_hd_asset* asset = assets[asset_index];
        if (!asset->loaded) {
            Load_Asset(asset);
        }
        const s_hd_frame* art = Resolve_Frame(asset, frame);
        if (art == nullptr) {
            return;
        }
        asset->last_used = publish_count;
        if (!Frame_Ready(art)) {
            Request_Frame(const_cast<s_hd_frame*>(art), true);
            current_draw.waiting = true;
        }
        Read_Ahead(asset, frame, 8);
        current_draw.source = SOURCE_ART;
        current_draw.frame = art;
        current_draw.x = x;
        current_draw.y = y;
        current_draw.glassed = asset->glassed;
        current_draw.art_outline = asset->art_outline;
        Note_Clip_Window();
        if (probe_x >= 0) {
            current_draw.name = asset->directory;
        }
    }

    void Begin_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
        Start_Draw();
        if (!enabled) {
            return;
        }
        current_draw.source = SOURCE_LINE;
        current_draw.line_x1 = x1;
        current_draw.line_y1 = y1;
        current_draw.line_x2 = x2;
        current_draw.line_y2 = y2;
        Note_Clip_Window();
    }

    void End_Draw() {
        Start_Draw();
    }

    void Stamp_Run(const uint8_t* dst, int32_t count) {
        Stamp_Page(video::current_video_page, dst, count);
    }

    void Stamp_Page(const uint8_t* page, const uint8_t* dst, int32_t count) {
        if (enabled) {
            twin::Touch_Page(page, dst, count, -1);
        }
    }

    void Stamp_Glass(const uint8_t* dst, uint8_t background_value, int32_t level) {
        (void)background_value;
        if (enabled) {
            twin::Touch_Page(video::current_video_page, dst, 1, level);
        }
    }

    void Stamp_Bitmap(const void* bitmap, int32_t index, uint8_t value) {
        (void)value;
        if (enabled) {
            twin::Touch_Image(bitmap, index);
        }
    }

    void Literal_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb) {
        if (enabled) {
            twin::Literal_Rect(page, x, y, width, height, rgb);
        }
    }

    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        if (enabled) {
            twin::Overwrite_Rect(page, x1, y1, x2, y2);
        }
    }

    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level) {
        if (enabled) {
            twin::Tint_Rect(page, x, y, width, height, level);
        }
    }

    void Table_Rows(const uint8_t* page, int32_t first_row, const uint8_t* table) {
        if (enabled) {
            twin::Table_Rows(page, first_row, table);
        }
    }

} // namespace render
