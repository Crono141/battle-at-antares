#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

struct s_animation_header;

namespace video {
    struct s_palette_color;
}

// Parallel 4x rendering of the game screen with higher-resolution art: the facade of the render
// modules in src/antares/render/ (the game calls only what is declared here).
//
//   render.cpp   switches (ANTARES_HD_*), statistics, Initialize
//   assets.cpp   frame decoding, hd/index.json, sprite.json, LBX load registry
//   cache.cpp    decoding workers, eviction, preloads, screen-map prefetch
//   glyphs.cpp   HD font glyphs
//   stamps.cpp   the current draw (Begin_* hooks) and the touched-pixel reports (Stamp_*)
//   facade.cpp   page and image plumbing (planes, copies, flips, publish), cursor art
//   video.cpp    HD cutscenes
//   parity.cpp   parity harness (ANTARES_PARITY, WP1.2)
//   twin*.cpp    twin planes: scopes, Unported fallback, the drawing twins, present
//   pixels.h     colour helpers; internal.h, twin_internal.h: what the modules share
//
// The engine keeps drawing 8-bit pixels into its 640x480 pages, and everything that reads
// them (game logic, hit tests, captures) is unchanged. Every page, and every off-screen image
// once written, has a twin plane at 4x (twin_internal.h). Each drawing operation opens a
// twin::Scope; the hooked blitters report the 1x pixels a draw writes ("stamps"), and its twin
// renders the draw's HD art into the plane, blended at its edges and spilling a soft fringe
// past the original outline. Pixels an operation without a twin changed are bound to their
// new index, enlarged, when its scope closes (the Unported fallback), so unported drawing
// shows as plain 4x scaling and never as stale or wrong art. Until WP1.3b (2026-11-02) attempt
// 2's model ran instead when ANTARES_TWINS was unset; it is gone.
//
// Plane pixels are palette based (foreground index, background index and coverage, coloured
// with the live palette when presented, so fades and palette cycling keep working) or full
// colour (shading, glass tints), which follow screen fades by a factor derived from the palette.
//
// HD art is read from the VFS under hd/<LBX>/<EEE>/ (sprite.json plus one PNG per frame,
// grey = palette index, alpha = coverage), normally supplied by a mod package's data/ tree.
namespace render {

    constexpr int32_t SCALE = 4;

    // Enables the feature when the VFS provides hd/pack.json. Safe to call more than once.
    void Initialize();
    bool Enabled();
    // Art requested between Begin_Preload and End_Preload (the LBX entries a screen loads while it
    // sets up, e.g. a battle's ships) is decoded before End_Preload returns, so the screen starts
    // with its art in memory instead of streaming it in. Waits at most `max_ms`.
    void Begin_Preload();
    void End_Preload(uint32_t max_ms);
    // Queues the HD art of an LBX entry the screen is about to draw without loading it into
    // memory first (file animations, e.g. the diplomacy rooms): its first `first_frames` frames
    // join a preload in progress, the rest are decoded behind them.
    void Preload_Asset(const char* lbx_name, int32_t entry, int32_t first_frames);
    // Stops the art decoding threads (before SDL shuts down).
    void Shutdown();

    // Called for every LBX entry loaded into memory.
    void Register_Load(const void* data, int32_t size, const char* lbx_name, int32_t entry);
    // The loaded entries in [begin, end) were freed, or moved by `delta` bytes (cache compaction).
    void Forget_Loads(const void* begin, const void* end);
    void Move_Loads(const void* begin, const void* end, ptrdiff_t delta);
    // The engine rewrote indices [first, first + count) of a loaded entry in place through `map`
    // (replace::Replace_Color_Range); its HD art is drawn through the same mapping.
    void Note_Recolor(const void* anim, int32_t first, int32_t count, const uint8_t* map);

    // Brackets one sprite draw; blitters stamp the pixels they write in between.
    void Begin_Draw(const s_animation_header* anim, int16_t x, int16_t y);
    void End_Draw();

    // Like Begin_Draw for animate::Remap_Draw: pixels go through `remap` (256 entries), and the
    // HD art is recoloured the same way.
    void Begin_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap);

    // Like Begin_Remap_Draw for draws shaded by a per-pixel mask (anim->width x height levels;
    // level n > 0 darkens by gray_percentages[n] percent), e.g. planets in the system display.
    void Begin_Masked_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap, const int8_t* mask, const int8_t* gray_percentages, int32_t level_count);

    // Brackets one glyph draw (fonts::Print_Character and variants). (x, y) is the unclipped
    // top-left of the glyph; colors are the 8 style colours the glyph's roles map to.
    void Begin_Glyph(const char* font_file, int16_t font_index, uint8_t code, int16_t x, int16_t y, const uint8_t* colors);

    // Brackets a draw of a known LBX entry that was not loaded through farload (streamed file
    // animations): frame `frame` drawn with its top-left at (x, y).
    void Begin_Asset_Draw(const char* lbx_name, int32_t entry, int16_t frame, int16_t x, int16_t y);

    // D21: a line from (x1, y1) to (x2, y2) (after clipping) is being drawn; its pixels follow
    // (Stamp_Run). The twin draws diagonal lines as a smooth stroke at 4x (line.cpp).
    void Begin_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2);

    // Every other row of `page` from `first_row` went through `table` (256 entries) in place
    // (replace::Interlaced_Gray_Scale_Off_Page): twin planes map their pixels the same way.
    void Table_Rows(const uint8_t* page, int32_t first_row, const uint8_t* table);

    // Off-screen images (bitmap-mode animations built with bitmap::*) get a twin plane when
    // their header is created; it stays empty (the 1x pixels enlarged) until written.
    void Bitmap_Reset(const void* bitmap, int32_t width, int32_t height);
    // The allocator rewound a block: [begin, end) is free memory now, and the planes of image
    // headers in it are dropped (PLAN.md 3.1, lifetime). LBX load registrations are kept: those
    // go when a new load lands on them (Forget_Loads).
    void Forget_Images(const void* begin, const void* end);
    // Source `src` drawn with its top-left at (x, y) of `target_bitmap`.
    void Begin_Draw_To_Bitmap(const s_animation_header* src, int16_t x, int16_t y, const void* target_bitmap);
    // Pixel `index` of `bitmap` was written with `value` by the current draw.
    void Stamp_Bitmap(const void* bitmap, int32_t index, uint8_t value);
    // Same-size copy of src's plane to dst (bitmap::Copy_Bitmap_To_Bitmap etc.).
    // bitmap::Flip_Bitmap: dst becomes src mirrored (vertical: top to bottom, else left to right).
    void Bitmap_Flip(const void* dst, const void* src, bool vertical);
    void Bitmap_Copy(const void* dst, const void* src);
    // The pixels of `bitmap` (at `pixels`) went through glass level `level` (bitmap::Tint_Bitmap):
    // its plane pixels are tinted the same way in true colour.
    void Bitmap_Glass(const void* bitmap, const uint8_t* pixels, int32_t level);
    // Every non-zero index of `bitmap` in [first, first + count) became map[index - first]
    // (replace::Replace_Color_Range): indexed HD pixels take the same map, true-colour ones keep
    // their colour.
    void Bitmap_Recolor(const void* bitmap, int32_t first, int32_t count, const uint8_t* map);

    // Bitmap twins (WP1.4): called right after the 1x operation, inside its scope.
    // A rectangle (x1, y1, width x height) of the current page was captured into `bitmap`.
    void Bitmap_Capture(const void* bitmap, int32_t x1, int32_t y1, int32_t width, int32_t height);
    // `dst` was made a resized copy of `src` (bitmap::Scale_Bitmap).
    void Bitmap_Scale(const void* dst, const void* src);
    // `bitmap` was outlined in `color` (bitmap::Outline_Bitmap; D17: the outline follows the art).
    void Bitmap_Outline(const void* bitmap, int32_t color, bool no_glass);
    // Pixels of `bitmap` were set to 0 (masked out): they become transparent.
    void Bitmap_Cleared(const void* bitmap);
    // `dst` was made `src` with some pixels cleared (bitmap::Vanish_Bitmap).
    void Bitmap_Vanish(const void* dst, const void* src);
    // Every pixel of `bitmap` went through `table` (bitmap::Gray_Scale_Bitmap).
    void Bitmap_Table(const void* bitmap, const uint8_t* table);
    // shear::Mask_Brighten_Bitmap_Pixels over `bitmap` from its pixel `dst_offset`, with the same
    // walk (source pointer, skips, size) and tables (256 per source colour from `start_color`).
    void Bitmap_Brighten(const void* bitmap, int32_t dst_offset, const uint8_t* src, int32_t src_skip, int32_t dst_skip, int32_t width, int32_t height, const uint8_t* tables, int32_t start_color);
    // shear::Jumble_Bitmap_Pixels moved pixels of `src` into `dst` with this noise.
    void Bitmap_Jumble(const void* dst, const void* src, uint32_t mask, uint32_t threshold, uint32_t noise_start, uint32_t toggle_after, const uint8_t* noise_table);
    // The pixels of `bitmap` in [x0, x1) x [y0, y1) went through `table` (256 entries): the
    // misc.cpp colour swaps and tints, info.cpp Colorize_Bitmap.
    void Bitmap_Map_Rect(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* table);
    // misc.cpp's colour operations and info.cpp Colorize_Bitmap, called after their loops with
    // the same arguments (the rectangle as the loops ran it); each builds its index table the way
    // its loop treats every value and maps the plane through it.
    void Bitmap_Replace_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* old_colors, const uint8_t* new_colors, int32_t count);
    void Bitmap_Swap_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* old_colors, const uint8_t* new_colors, int32_t count);
    void Bitmap_Tint_Colors(const void* bitmap, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t tint);
    void Bitmap_Colorize(const void* bitmap, const uint8_t* colors, int32_t first, int32_t last);
    // replace::Replace_Color_All and Replace_Color, after their loops: loaded sprites take the same
    // map for their art (Note_Recolor), bitmaps their plane (Bitmap_Recolor).
    void Recolor_All(s_animation_header* anim, uint8_t color);
    void Recolor_One(s_animation_header* anim, int16_t old_color, uint8_t new_color);
    // CMBTDRW1::Plasma_Darken over `bitmap`: each pixel through column `level` of `darken_map`
    // (256 x 16), the level from the plasma frame `darken` and `darkness`.
    void Bitmap_Plasma(const void* bitmap, const uint8_t* darken, int32_t darken_width, int32_t darken_height, int32_t darkness, const uint8_t* darken_map);
    // bitmap::Draw_Bitmap_Line drew `bitmap` stretched and rotated along a line (the stellar
    // converter): its geometry (centre, angle in degrees, the source span, the stretch in percent)
    // and the page rectangle it covered. The twin samples the image's HD plane per sub-pixel.
    void Bitmap_Line(const void* bitmap, int32_t mid_x, int32_t mid_y, int32_t angle, int32_t ext_x1, int32_t ext_x2, int32_t img_x1, int32_t img_y1, int32_t img_y2, int32_t scale_percent, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

    // A combat ship's shield glow, as the game just built it in `image` (CMBTDRW1): the colours Outline_Bitmap grew in order (1 and 2 are
    // spacers the game clears afterwards), the gap mask and each arc's mask (1x pixels with
    // their sizes, applied at 0, 0) and, per arc, the ring colours 0x50 + n that
    // weakness dropped (bit n). The twin draws the shield again at sub-pixel scale.
    struct s_shield {
        const void* image = nullptr;
        uint8_t rings[16] = {};
        int32_t ring_count = 0;
        const uint8_t* gap_mask = nullptr;
        const uint8_t* arc_masks[4] = {};
        int32_t mask_width = 0; // the gap mask's size
        int32_t mask_height = 0;
        int32_t arc_width[4] = {};
        int32_t arc_height[4] = {};
        uint8_t dropped[4] = {};
    };
    // The ship on its own, before the shield is grown (`ship` placed at x, y in the shield image):
    // Combat_Shield measures distances from this outline.
    void Combat_Shield_Ship(const void* ship, int32_t x, int32_t y);
    void Combat_Shield(const s_shield& shield);

    // Records that `count` pixels at `dst` (inside the current video page) were written.
    void Stamp_Run(const uint8_t* dst, int32_t count);

    // The rectangle (inclusive corners) of `page` was overwritten by plain drawing (solid
    // fills): its plane pixels become the original pixels enlarged, whatever values were written.
    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

    // The rectangle (x, y, width, height) of `page` went through glass level `level`
    // (graphics::Tint_Fill): its HD pixels are tinted the same way in true colour.
    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level);

    // Full-colour pixels replace the plane pixels of the rectangle (x, y, width, height) of `page`,
    // whose page pixels were just written (HD video frames). `rgb` holds (width*4) x (height*4)
    // pixels, 3 bytes each.
    void Literal_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb);

    // Like Stamp_Run, for pixels written to an explicit page (draws that write the back page
    // directly) instead of the current video page.
    void Stamp_Page(const uint8_t* page, const uint8_t* dst, int32_t count);

    // A glass pixel of level `level` (0-15) was written at `dst`: the page held
    // `background_value` before the glass remap replaced it. The HD pixels underneath are
    // tinted in true colour.
    void Stamp_Glass(const uint8_t* dst, uint8_t background_value, int32_t level);

    // Page plumbing: the page planes follow page allocation, copies and publishing.
    void Allocate_Planes(int32_t width, int32_t height);
    void Free_Planes();
    void Copy_Plane(const uint8_t* src_page, const uint8_t* dst_page);
    // Partial page copy (count bytes at byte offset `offset`).
    void Copy_Plane_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count);
    // Game thread, with the off page being published: replays late art and copies the changed
    // rows to the published plane.
    void Publish_Plane(const uint8_t* published_page_pixels);

    // HD art for the mouse cursor image `pixels` (size x size palette indices, 0 = transparent),
    // looked up by its fingerprint under hd/CURSORS/. Writes (size*4)^2 ARGB pixels coloured
    // with `palette`; false when there is no art for this cursor.
    bool Cursor_Art(const uint8_t* pixels, int32_t size, const video::s_palette_color* palette, uint32_t* argb);

    // Presenter side: copies the published HD rows for presenting. Call it together with the
    // copy of the published page, while the game thread cannot publish a newer frame.
    void Snapshot_Plane();

    // Writes the presented HD frame (width*4 x height*4 ARGB8888) to `output`, only the rows
    // that changed since the last call unless the palette changed. Returns false when nothing
    // changed; otherwise [row_first, row_last) are the changed output rows.
    bool Resolve(const video::s_palette_color* palette, uint32_t* output, int32_t* row_first, int32_t* row_last);

    // The HD pack was found (Initialize): the twin planes run.
    extern bool enabled;
    // Developer setting `render art_hot_reload = true;` (PLAN.md section 5; local disk only):
    // loose art folders are looked at even with an index, entries without art are looked up
    // again, and changed art files are decoded again (hot_reload.cpp).
    extern bool art_hot_reload;

    namespace twin {

        // Brackets one drawing operation (the twin contract, PLAN.md section 3.3): the first
        // statement of every drawing entry point. `target` is the 1x pixels of a page
        // (video::current_video_page) or an image header; the rectangle is in its 1x pixels
        // (width or height <= 0: all of it). Until the operation has a twin, closing the scope
        // runs the Unported fallback (section 3.4): every 1x pixel the operation changed is
        // bound to its new index, enlarged. Scopes nest; each covers its own rectangle.
        class Scope {
          public:
            Scope(const char* op, const void* target, int32_t x, int32_t y, int32_t width, int32_t height) {
                if (enabled) {
                    Open(op, target, x, y, width, height);
                }
            }
            // A sprite drawn with its top-left at (x, y); screen backgrounds cover the page.
            Scope(const char* op, const void* target, int32_t x, int32_t y, const s_animation_header* anim) {
                if (enabled) {
                    Open_Sprite(op, target, x, y, anim);
                }
            }
            ~Scope() {
                if (slot_ >= 0) {
                    Close();
                }
            }
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

          private:
            void Open(const char* op, const void* target, int32_t x, int32_t y, int32_t width, int32_t height);
            void Open_Sprite(const char* op, const void* target, int32_t x, int32_t y, const s_animation_header* anim);
            void Close();
            int32_t slot_ = -1;
        };

    } // namespace twin

    // Parity harness (parity.cpp, ANTARES_PARITY=1): compares the presented HD plane with the page
    // it was presented with (presenter thread, after Snapshot_Plane), and writes the last
    // comparison next to an HD screenshot as <name>.parity.png.
    bool Parity_Enabled();
    void Parity_Check(const uint8_t* page, const video::s_palette_color* palette);
    bool Parity_Save(const char* screenshot_path);

    // WP3.3a: the HD art of an LBX entry as plain images for a frontend (export.cpp). Each frame
    // is its art at scale S, straight RGBA bytes, with its top-left `origin` in 1x pixels from the
    // top-left of the entry's 1x frame (art may reach past it). Indexed pixels take their colour
    // from `palette_rgb` (0xRRGGBB per index); full-colour pixels keep theirs.
    struct s_exported_frame {
        int32_t width = 0; // in S pixels
        int32_t height = 0;
        int32_t origin_x = 0; // in 1x pixels
        int32_t origin_y = 0;
        std::vector<uint8_t> rgba;
    };
    // Game thread. `loaded_entry` is the entry's bytes, registered with Register_Load. False when
    // the HD pack has no art for the entry, or it didn't decode within `max_ms`: the caller then
    // uses the 1x frames.
    bool Export_Art(const char* lbx_name, int32_t entry, const s_animation_header* loaded_entry, const uint32_t* palette_rgb,
        std::vector<s_exported_frame>* frames, uint32_t max_ms);

    // The palette an art pack's sprite was made under (its sprite.json "palette", from the art
    // index): FONTS.LBX palettes and LBX entries' embedded palettes, applied in order. False when
    // the index doesn't say, or says it with a layer only the art tools know.
    struct s_palette_layer {
        int32_t palette = -1; // a FONTS.LBX palette number, or -1 for an embedded palette:
        std::string lbx;      // the entry whose embedded palette is laid over
        int32_t entry = -1;
    };
    bool Declared_Palette(const char* lbx_name, int32_t entry, std::vector<s_palette_layer>* layers);

    // HD cutscenes on the parallel 4x screen (video.cpp).
    //
    // A Smacker video `<NAME>.LBX` has HD frames when the VFS provides hd/VIDEO/<NAME>/video.json
    // (width, height, frames, scale, hd_frames) plus one <NNNNNN>.webp per HD frame. With
    // hd_frames == 2 * frames, HD frame 2k is source frame k and 2k+1 lies halfway to k+1; those
    // in-between frames are published on their own, halfway between two source frames, so the
    // video plays at twice its frame rate. Audio, timing and the 8-bit page are unchanged.

    // A Smacker video was opened (smckplay::Open_Smacker_Animation).
    void Video_Open(const char* file_name, int32_t width, int32_t height, int32_t frame_count);
    // Source frame `frame` (width x height) was just written at (left, top) of the off page.
    void Video_Frame(int32_t left, int32_t top, int32_t width, int32_t height, int32_t frame);
    // Before waiting for the next source frame, due at `next_frame_ns` (SDL_GetTicksNS):
    // publishes the in-between frame at its time when it is ready.
    void Video_Between_Frames(uint64_t next_frame_ns, uint64_t frame_duration_ns);
    void Video_Close();

} // namespace render
