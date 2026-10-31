#pragma once

#include <stddef.h>
#include <stdint.h>

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
//   planes.cpp   the HD surfaces of the pages and their copies
//   images.cpp   HD surfaces of off-screen images (bitmap::*)
//   stamps.cpp   attempt 2's draw model: stamps, settle, deferred upgrades (retired in WP1.3a)
//   present.cpp  publish, presenter snapshot, resolve to ARGB, cursor art
//   video.cpp    HD cutscenes
//   pixels.h     the HD pixel format; internal.h: what the modules share
//
// The engine keeps drawing 8-bit pixels into its 640x480 pages, and everything that reads
// them (game logic, hit tests, captures) is unchanged. Alongside each page, and alongside
// off-screen images, an HD surface holds 4x4 HD pixels per page pixel. The hooked blitters
// report every page pixel a draw writes ("stamps"); at the end of the draw the HD art of that
// sprite, glyph or image is rendered into the HD surface, blended at its edges over what was
// already there and spilling a soft fringe past the original outline.
//
// Each HD block remembers the page value it was made for. Pixels written by code without
// hooks (fills, lines, video, effects) no longer match; publishing a frame replaces those
// blocks with the original pixel enlarged, so unhooked drawing shows as plain 4x scaling and
// never as stale or wrong art.
//
// HD pixels are either palette based (foreground index, background index and coverage,
// coloured with the live palette when presented, so fades and palette cycling keep working)
// or literal RGB (shading, glass tints), which follow screen fades by a factor derived from the
// palette.
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

    // Off-screen images (bitmap-mode animations built with bitmap::*) get their own HD surface
    // once something with HD art is drawn into them.
    void Bitmap_Reset(const void* bitmap, int32_t width, int32_t height);
    // Source `src` drawn with its top-left at (x, y) of `target_bitmap`.
    void Begin_Draw_To_Bitmap(const s_animation_header* src, int16_t x, int16_t y, const void* target_bitmap);
    // Pixel `index` of `bitmap` was written with `value` by the current draw.
    void Stamp_Bitmap(const void* bitmap, int32_t index, uint8_t value);
    // Same-size copy of src's HD surface to dst (bitmap::Copy_Bitmap_To_Bitmap etc.).
    // bitmap::Flip_Bitmap: dst becomes src mirrored (vertical: top to bottom, else left to right).
    void Bitmap_Flip(const void* dst, const void* src, bool vertical);
    void Bitmap_Copy(const void* dst, const void* src);
    // `dst` (width x height, pixels at `dst_pixels`) is a resized copy of `src`
    // (bitmap::Scale_Bitmap, e.g. stars on a zoomed-out galaxy).
    void Bitmap_Scale(const void* dst, const void* src, const uint8_t* dst_pixels, int32_t width, int32_t height);
    // The pixels of `bitmap` (at `pixels`) went through glass level `level` (bitmap::Tint_Bitmap):
    // its HD pixels are tinted the same way in true colour.
    void Bitmap_Glass(const void* bitmap, const uint8_t* pixels, int32_t level);
    // The pixels of `bitmap` were changed in place by plain recolouring (Mask_Out_Colors etc.):
    // HD pixels whose page pixel became 0 turn transparent, other changes are recoloured.
    void Bitmap_Sync(const void* bitmap, const uint8_t* pixels);
    // Every non-zero index of `bitmap` in [first, first + count) became map[index - first]
    // (replace::Replace_Color_Range): indexed HD pixels take the same map, true-colour ones keep
    // their colour.
    void Bitmap_Recolor(const void* bitmap, int32_t first, int32_t count, const uint8_t* map);
    // A rectangle of the current page captured into `bitmap`.
    void Bitmap_Capture(const void* bitmap, int32_t x1, int32_t y1, int32_t width, int32_t height);

    // Records that `count` pixels at `dst` (inside the current video page) were written.
    void Stamp_Run(const uint8_t* dst, int32_t count);

    // The rectangle (inclusive corners) of `page` was overwritten by plain drawing (solid
    // fills): its HD pixels become the original pixels enlarged, whatever values were written.
    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

    // The rectangle (x, y, width, height) of `page` went through glass level `level`
    // (graphics::Tint_Fill): its HD pixels are tinted the same way in true colour.
    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level);

    // Full-colour pixels replace the HD pixels of the rectangle (x, y, width, height) of `page`,
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

    // Page plumbing: HD surfaces follow page allocation, copies and publishing.
    void Allocate_Planes(int32_t width, int32_t height);
    void Free_Planes();
    void Copy_Plane(const uint8_t* src_page, const uint8_t* dst_page);
    // Partial page copy (count bytes at byte offset `offset`).
    void Copy_Plane_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count);
    // Game thread, with the off page being published: settles unhooked writes and copies the
    // changed rows to the published surface.
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

    // HD cutscenes on the parallel 4x screen (video.cpp).
    //
    // A Smacker video `<NAME>.LBX` has HD frames when the VFS provides hd/VIDEO/<NAME>/video.json
    // (width, height, frames, scale, hd_frames) plus one <NNNNNN>.webp per HD frame. With
    // hd_frames == 2 * frames, HD frame 2k is source frame k and 2k+1 lies halfway to k+1; those
    // in-between frames are published on their own, halfway between two source frames, so the
    // video plays at twice its frame rate. Audio, timing and the 8-bit page are unchanged.

    // A Smacker video was opened (smckplay::Open_Smacker_Animation).
    void Video_Open(const char* file_name, int32_t width, int32_t height, int32_t frame_count);
    // Source frame `frame` was just written at (left, top) of the off page.
    void Video_Frame(int32_t left, int32_t top, int32_t frame);
    // Before waiting for the next source frame, due at `next_frame_ns` (SDL_GetTicksNS):
    // publishes the in-between frame at its time when it is ready.
    void Video_Between_Frames(uint64_t next_frame_ns, uint64_t frame_duration_ns);
    void Video_Close();

} // namespace render
