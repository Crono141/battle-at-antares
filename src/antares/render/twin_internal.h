#pragma once

// Twin planes (WP1.3a, PLAN.md sections 3.1 to 3.4): shared by the twin modules. Include after
// internal.h.

#include <string>

namespace render::twin {

    // One plane pixel: a 32-bit base colour plus a mask saying how it follows the game's index
    // tricks (D20, PLAN.md section 3.2).
    //   mask bits 0-7    fg index (`index`, `shade`) or glass level (`glass`)
    //   mask bits 8-15   bg index, shown under partial coverage
    //   mask bits 16-23  coverage of fg over bg (255 = full)
    //   mask bits 24-27  kind
    //   mask bit  28     MASK_CLEAR: the bg is transparent (inside images)
    // `none` pixels show their base (0xAARRGGBB) and follow whole-screen fades at present.
    struct s_px {
        uint32_t base;
        uint32_t mask;
    };

    enum e_kind : uint32_t {
        KIND_NONE = 0,
        KIND_INDEX = 1,
        KIND_SHADE = 2, // WP1.7
        KIND_GLASS = 3, // WP1.4
    };
    constexpr uint32_t KIND_SHIFT = 24;
    constexpr uint32_t MASK_CLEAR = 1u << 28;
    // Written by a line stroke (D21): parity counts it apart (the stroke follows the line, not the
    // 1x pixels). Any later write of the pixel drops it.
    constexpr uint32_t MASK_STROKE = 1u << 29;

    inline uint32_t Kind(const s_px& px) {
        return (px.mask >> KIND_SHIFT) & 0xF;
    }

    // Bound to palette index `index` at full coverage: what the stock loader and the Unported
    // fallback write.
    inline s_px Index_Px(uint8_t index) {
        return {0u, index | (255u << 16) | (KIND_INDEX << KIND_SHIFT)};
    }

    // Nothing: a transparent pixel inside an image (drawing the image leaves the page as it is).
    inline s_px Clear_Px() {
        return {0u, (KIND_INDEX << KIND_SHIFT) | MASK_CLEAR};
    }

    inline s_px Literal_Px(uint32_t rgb) {
        return {0xFF000000u | (rgb & 0xFFFFFFu), KIND_NONE << KIND_SHIFT};
    }

    // A page or an off-screen image at scale S. An image plane with no pixels yet stands for its
    // 1x pixels enlarged (planes are attached at header creation and filled on first write).
    struct s_plane {
        int32_t width = 0; // in 1x pixels
        int32_t height = 0;
        std::vector<s_px> px;           // (width * S) x (height * S), or empty (images)
        std::vector<uint8_t> row_dirty; // pages: 1x rows changed since copied onward
        // Images: holds geometry (MASK_STROKE pixels) that may show past its 1x pixels (a combat
        // shield): drawing the image also draws those (twin_draw.cpp, Render_Plane).
        bool geometry = false;
    };

    extern s_plane plane_off;
    extern s_plane plane_back;
    extern s_plane plane_published;
    extern s_plane plane_present;
    // Off-screen images keyed by their header (bitmap::Create_Bitmap_Header).
    extern std::unordered_map<const void*, s_plane> images;

    inline s_px* Block(s_plane& plane, int32_t x, int32_t y) {
        return &plane.px[(size_t)y * S * plane.width * S + (size_t)x * S];
    }

    // The plane `target` names: a page's 1x pixels (g_page_off/back) or an image header. Sets
    // the 1x pixels and size. Null when the target has no plane (loaded sprites, unknown buffers).
    s_plane* Resolve_Target(const void* target, const uint8_t** pixels, int32_t* width, int32_t* height);
    // Fills the plane from its 1x pixels, enlarged, every pixel bound to its index.
    void Fill_From_1x(s_plane& plane, const uint8_t* pixels);
    void Fill_Block_Index(s_plane& plane, int32_t x, int32_t y, uint8_t index);
    // ANTARES_HD_PROBE=x,y: logs twin writes to that page pixel.
    void Probe(const s_plane& plane, int32_t x, int32_t y, const char* what, int32_t value);

    void Attach_Image(const void* header, int32_t width, int32_t height);
    size_t Drop_Images(const void* begin, const void* end); // returns how many planes it dropped
    // Image planes dropped because the allocator rewound past their header (facade.cpp), since
    // the last stats window.
    extern uint32_t rewind_drops;
    // Bitmap twin calls since the last stats window (facade.cpp), by operation.
    extern std::unordered_map<std::string, uint32_t> bitmap_twin_calls;
    void Allocate_Pages(int32_t width, int32_t height);
    void Free_Pages();

    // The open scopes on `target` take the 1x pixels of the rectangle [x0, x1) x [y0, y1) as
    // handled by a twin: their Unported fallback leaves them alone.
    void Accept_Rect(const void* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void Accept(const void* target, int32_t x, int32_t y);
    // The 1x pixels `target` had when the innermost scope open on it was opened, when that scope
    // covers all of it (bitmap twins compare them with the pixels now); null otherwise.
    const uint8_t* Scope_Before(const void* target);

    // Debug (ANTARES_TWIN_CHECK): the last operations run on an image.
    std::string Recent_Ops(const void* target);

    // core drawing twins (twin_draw.cpp)
    void Touch_Page(const uint8_t* page, const uint8_t* dst, int32_t count, int32_t level);
    void Touch_Image(const void* header, int32_t index);
    void End_Draw();
    // Late art: replays art draws whose frame has finished decoding (game thread, at publish).
    void Replay_Late_Art();
    // Before publishing: waits a little (capped, see twin_draw.cpp) for the art of draws this
    // frame made without it, so the frame is shown with its HD art instead of popping in after.
    void Hold_For_Late_Art();
    size_t Late_Count();
    std::string Late_Stats(); // and resets the window
    void Reset_Late_Art();
    // An image's indices [first, first + count) recoloured in place through `map` (Replace_Color*).
    void Recolor_Image(const void* header, int32_t first, int32_t count, const uint8_t* map);
    // bitmap::Copy_Bitmap_To_Bitmap and the glass tint of bitmap::Tint_Bitmap on image planes.
    void Copy_Image(const void* dst, const void* src);
    void Glass_Image(const void* header, int32_t level);
    // bitmap::Flip_Bitmap: dst becomes src mirrored (vertical: top to bottom, else left to right).
    void Flip_Image(const void* dst, const void* src, bool vertical);
    void Table_Rows(const uint8_t* page, int32_t first_row, const uint8_t* table);
    // bitmap twins (WP1.4; twin_draw.cpp), run after the 1x operation inside its scope
    void Sync_Cleared(const void* header); // pixels the operation set to 0 become transparent
    void Vanish_Image(const void* dst, const void* src);
    void Table_Image(const void* header, const uint8_t* table);
    void Brighten_Image(const void* header, int32_t dst_offset, const uint8_t* src, int32_t src_skip, int32_t dst_skip, int32_t width, int32_t height, const uint8_t* tables, int32_t start_color);
    void Capture_Image(const void* header, const uint8_t* page, int32_t x1, int32_t y1, int32_t width, int32_t height);
    void Scale_Image(const void* dst, const void* src);
    void Outline_Image(const void* header, uint8_t color, bool no_glass);
    // render::Combat_Shield (twin_shield.cpp).
    void Shield_Ship(const void* ship, int32_t x, int32_t y);
    void Shield_Image(const s_shield& shield);
    void Line_Image(const uint8_t* page, const void* header, int32_t mid_x, int32_t mid_y, int32_t angle, int32_t ext_x1, int32_t ext_x2, int32_t img_x1, int32_t img_y1, int32_t img_y2, int32_t scale_percent, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
    // Plane blocks of the image in [x0, x1) x [y0, y1) through an index table.
    void Map_Image_Rect(const void* header, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* table);
    void Plasma_Image(const void* header, const uint8_t* darken, int32_t darken_width, int32_t darken_height, int32_t darkness, const uint8_t* darken_map);
    void Jumble_Image(const void* dst, const void* src, uint32_t mask, uint32_t threshold, uint32_t noise_start, uint32_t toggle_after, const uint8_t* noise_table);
    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level);
    void Copy_Page(const uint8_t* src_page, const uint8_t* dst_page);
    void Copy_Page_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count);

    // present (twin_present.cpp)
    void Publish();
    void Snapshot();
    bool Resolve(const video::s_palette_color* palette, uint32_t* output, int32_t* row_first, int32_t* row_last);
    void Literal_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb);

    // Unported bookkeeping (twin.cpp): ops whose fallback ran since the last publish.
    void End_Frame_Unported(std::string* names, uint32_t* pixels);
    void End_Window_Unported(std::string* names, uint32_t* pixels, uint64_t* scope_ns);
    extern std::string window_slowest; // "op=total ms/calls", slowest first (End_Window_Unported)
    // The Unported calls of the frame last presented, for the parity harness.
    std::string Presented_Unported();

} // namespace render::twin
