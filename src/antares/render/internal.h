#pragma once

// Shared between the render modules (src/antares/render/*.cpp); not part of the facade.
// Include after pch.h.

#include "antares/render/render.h"
#include "antares/render/pixels.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct yyjson_val;

namespace render {

#define HD_MAX_ASSETS 16384

    enum e_frame_state {
        FRAME_ORIGINAL = 0,
        FRAME_ART = 1,
        FRAME_SAME_AS = 2,
    };

    // Decoding state of a frame's pixels (index/alpha). Frames are decoded by worker threads;
    // only READY frames are read or freed by the game thread, only QUEUED/LOADING ones are
    // written by workers.
    enum e_frame_load : uint8_t {
        LOAD_NONE = 0,
        LOAD_QUEUED = 1,
        LOAD_LOADING = 2,
        LOAD_READY = 3,
        LOAD_FAILED = 4,
    };

    struct s_hd_asset;

    struct s_hd_frame {
        e_frame_state state;
        int32_t same_as;
        int32_t origin_x;
        int32_t origin_y;
        int32_t width;
        int32_t height;
        uint8_t* index;
        uint8_t* alpha;
        uint8_t* rgb;     // full-colour art (RGB per pixel) instead of index, or null
        bool full_colour; // the PNG is RGBA (sprite.json "format": "rgba")
        std::atomic<uint8_t> load;
        uint8_t queued_priority;     // e_priority of the queue it was last put in (game thread)
        std::string vfs_path;        // hd/<LBX>/<EEE>/<FFF>.png
        std::string disk_path;       // the same file on disk when it is not inside a zip
        std::vector<uint8_t> packed; // PNG bytes read on the game thread (zipped packs)
    };

    struct s_hd_asset {
        std::string directory;
        bool loaded;
        bool has_source_hash;
        uint32_t source_hash; // FNV-1a of the LBX entry the art was made from
        bool glassed;
        bool redrawn; // new art (sprite.json "redraw"), not an upscale of the original pixels
        // sprite.json "outline": "art": the art, not the original's pixels, is the sprite's outline
        // (static art for a sprite whose frames animate, e.g. the combat monsters' wings).
        bool art_outline;
        int32_t scale;
        int32_t frame_count;
        s_hd_frame* frames;
        uint32_t last_used; // publish count of the last draw or load, for eviction
        bool keep;          // belongs to a screen that is shown, next or resident (not evicted)
    };

    struct s_registration {
        int32_t asset;
        uint32_t data_hash;
        uint8_t header[12];
        int32_t size;
        // Indices the engine rewrote in the loaded bytes after loading (Note_Recolor), e.g. a
        // player's colour put into combat fighters; the HD art is drawn through the same map.
        bool recolored;
        uint8_t recolor[256];
    };

    // Decode priorities, most urgent first. Workers drain the queues in this order.
    enum e_priority : uint8_t {
        PRIORITY_URGENT,     // a draw is waiting for the frame now
        PRIORITY_SCREEN,     // the game just loaded the entry: it is about to be drawn
        PRIORITY_NEXT,       // screens reachable from the current one (hd/screens.json)
        PRIORITY_BACKGROUND, // startup prefetch
        PRIORITY_COUNT,
    };

    // ------------------------------------------------------------ switches and statistics
    // (render.cpp)

    extern bool initialized;
    extern bool enabled;
    // ANTARES_HD_STATS: count draws that had no HD art, by asset, and log them
    // periodically. Debug aid for finding coverage gaps.
    extern bool stats_enabled;
    // ANTARES_HD_TRACE: logs every LBX entry load and how long draws waited for their art.
    extern bool trace_enabled;
    extern uint64_t trace_meta_ns;
    extern std::unordered_map<const void*, std::string> stats_names;
    // ANTARES_HD_SLOW_IO=<ms>: debug aid, every art load waits this long first, to reproduce
    // what players on slow (network, spinning) disks see when art arrives late.
    extern uint32_t slow_io_ms;
    // Game-thread time per stage (ANTARES_HD_STATS), reported with the settle count.
    enum e_timer { TIMER_RENDER,
        TIMER_COPY,
        TIMER_PUBLISH,
        TIMER_UPGRADE, // parts of TIMER_PUBLISH
        TIMER_EVICT,
        TIMER_PREFETCH,
        TIMER_SETTLE,
        TIMER_SCOPE_OPEN,  // twin planes: a scope's snapshot of its rectangle
        TIMER_SCOPE_CLOSE, // twin planes: the compare and Unported fallback when it closes
        TIMER_TWIN_RENDER, // twin planes: drawing a draw's art into its plane (Render_Plane)
        TIMER_COUNT };
    extern uint64_t stats_timer_ns[TIMER_COUNT];

    struct s_scoped_timer {
        e_timer timer;
        uint64_t start;
        explicit s_scoped_timer(e_timer which) : timer(which), start(stats_enabled ? SDL_GetTicksNS() : 0) {}
        ~s_scoped_timer() {
            if (stats_enabled) {
                stats_timer_ns[timer] += SDL_GetTicksNS() - start;
            }
        }
    };
    // ANTARES_HD_DEBUG=sd: tint blocks that fell back to the enlarged original because
    // unhooked code changed the page pixel.
    extern bool debug_sd;
    // ANTARES_HD_PROBE=x,y: log every HD write to that page pixel (debug aid).
    extern int32_t probe_x;
    extern int32_t probe_y;
    // ANTARES_HD_CAPTURE=<dir>: write the live palette the first time each LBX sprite is
    // drawn under it (see Capture_Palette).
    extern std::string capture_dir;

    void Capture_Palette(const std::string& name);
    void Stats_Missing(const std::string& key);

    // ------------------------------------------------------------ assets (assets.cpp)

    // Assets are appended and never moved or freed.
    extern s_hd_asset* assets[HD_MAX_ASSETS];
    extern int32_t asset_count;
    extern std::unordered_map<std::string, int32_t> asset_by_key;
    extern std::unordered_map<const void*, s_registration> registrations;

    bool Read_Vfs_File(const char* path, std::vector<uint8_t>* bytes);
    bool Decode_Frame_Bytes(const uint8_t* bytes, size_t size, const std::string& name, s_hd_frame* frame);
    bool Load_Frame_Png(const std::string& path, s_hd_frame* frame);
    void Load_Index();
    void Load_Asset(s_hd_asset* asset);
    const s_hd_frame* Resolve_Frame(const s_hd_asset* asset, int32_t frame_number);
    int32_t Find_Or_Create_Asset(const char* lbx_name, int32_t entry);
    // An asset listed by screens.json: its sprite.json is known to exist.
    int32_t Find_Or_Create_Known_Asset(const char* key);

    // ------------------------------------------------------------ cache (cache.cpp)

    extern std::atomic<int64_t> decoded_bytes;
    extern uint32_t publish_count;
    extern int64_t cache_budget;
    extern const uint64_t PREFETCH_BUDGET_NS;

    void Start_Workers();
    void Request_Frame(s_hd_frame* frame, e_priority priority);
    void Request_Frame(s_hd_frame* frame, bool urgent);
    int32_t Queue_Length();
    bool Frame_Ready(const s_hd_frame* frame);
    void Prefetch_Asset(s_hd_asset* asset);
    void Free_Asset_Frames(s_hd_asset* asset);
    void Evict_Over_Budget();
    void Load_Screen_Map();
    // At startup: queues the startup screens (or, without a screen map, the title screen).
    void Queue_Startup_Art();
    // An entry of `lbx_name` is being loaded: enters the screen that file belongs to, if any.
    void Enter_Screen_Of(const char* lbx_name);
    void Pump_Prefetch(uint64_t budget_ns);

    // ------------------------------------------------------------ glyphs (glyphs.cpp)

    // HD art of glyph `code` of a font, or null when there is none (counted in the stats).
    const s_hd_frame* Glyph_Art(const char* font_file, int16_t font_index, uint8_t code);

    // ------------------------------------------------------------ surfaces (planes.cpp)

    struct s_surface {
        int32_t width = 0; // in page pixels
        int32_t height = 0;
        std::vector<uint32_t> hd;       // (width * S) x (height * S); empty: no HD (images)
        std::vector<uint16_t> shadow;   // page value each block was made for
        std::vector<uint32_t> mark;     // draw that last stamped each pixel
        std::vector<uint8_t> row_dirty; // pages: rows changed since last copied onward
        std::vector<uint8_t> fringe;    // pages: block holds art spilled past a sprite's outline
    };

    extern s_surface page_off;
    extern s_surface page_back;
    extern s_surface page_published;
    extern s_surface page_present;
    // Guards allocating/freeing the page surfaces (game thread) against the presenter
    // reading the published/present surfaces.
    extern std::mutex planes_mutex;
    // Factor (0-256) applied to literal pixels so they follow palette fades.
    extern uint32_t published_fade;
    extern uint32_t present_fade;
    // Off-screen images keyed by their header. Nodes stay put, so pointers remain valid.
    extern std::unordered_map<const void*, s_surface> images;

    inline uint32_t* Block(s_surface& surface, int32_t x, int32_t y) {
        return &surface.hd[(size_t)y * S * surface.width * S + (size_t)x * S];
    }

    inline void Mark_Row(s_surface& surface, int32_t y) {
        if (!surface.row_dirty.empty()) {
            surface.row_dirty[(size_t)y] = 1;
        }
    }

    void Ensure_Hd(s_surface& surface);
    void Fill_Block(s_surface& surface, int32_t x, int32_t y, uint32_t pixel);
    s_surface* Page_Surface(const uint8_t* page);

    // ------------------------------------------------------------ the current draw
    // Set up by the Begin_* hooks (stamps.cpp) for both models: which art a draw uses, where,
    // and how it is recoloured.

    enum e_source {
        SOURCE_NONE,  // no HD art: written pixels show the original enlarged
        SOURCE_ART,   // a sprite, glyph or file animation frame with HD art
        SOURCE_IMAGE, // an off-screen image with an HD surface
    };

    struct s_draw {
        e_source source = SOURCE_NONE;
        const s_hd_frame* frame = nullptr;
        bool glyph = false;
        bool glassed = false;
        bool redrawn = false;
        bool art_outline = false;
        int32_t x = 0; // where the sprite's top-left lands in the target
        int32_t y = 0;
        bool remapped = false;
        uint8_t remap[256];
        uint8_t colors[8];
        std::vector<uint8_t> shade; // brightness percent per sprite pixel, empty = none
        int32_t shade_width = 0;
        int32_t shade_height = 0;
        const s_surface* image = nullptr;
        const void* anim = nullptr; // the header drawn (twin planes: image sources)
        int32_t image_x = 0;
        int32_t image_y = 0;
        std::string name; // for the probe
        // The frame has art that is still being decoded: written pixels show the original
        // for now and are recorded so they can be upgraded when it arrives.
        bool waiting = false;
        // Clip window at the start of the draw (the fringe must stay inside it).
        int32_t clip_x1 = 0;
        int32_t clip_y1 = 0;
        int32_t clip_x2 = 0x7FFF;
        int32_t clip_y2 = 0x7FFF;
    };

    extern s_draw current_draw;

    // ------------------------------------------------------------ attempt 2's draw model
    // (stamps.cpp; retired in WP1.3a)

    void Flush_Pending();
    // Blocks whose page pixel was changed by code without hooks are replaced by the original
    // pixel enlarged. Runs over each publish, and over a draw's area before HD art is blended
    // into it, so soft edges never blend over leftovers of an earlier screen.
    uint32_t Settle(s_surface& surface, const uint8_t* page, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void Upgrade_Deferred();
    size_t Deferred_Count();
    // Drops pending stamps and deferred draws (the page surfaces are being freed).
    void Reset_Stamps();

    // Attempt 2's model is the one running (HD on, twin planes off).
    inline bool Legacy() {
        return enabled && !twins_on;
    }

} // namespace render
