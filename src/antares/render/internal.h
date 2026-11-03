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
        uint8_t* mask;    // full-colour art: its mask image (D20, art::Mask_Meaning), or null
        bool full_colour; // RGBA art: sprite.json "format": "rgba", or the file's own format
        // The file as sprite.json describes it: its size in its own pixels and its scale. 0 when
        // inferred (no sprite.json): the scale comes from the 1x frame size (base_width x
        // base_height) at decode. Decoded art is resampled to scale S; `width` and `height` above
        // are that size (set at decode for inferred frames).
        int32_t file_width;
        int32_t file_height;
        int32_t file_scale;
        int32_t base_width;
        int32_t base_height;
        std::atomic<uint8_t> load;
        uint8_t queued_priority;     // e_priority of the queue it was last put in (game thread)
        std::string vfs_path;        // hd/<LBX>/<EEE>/<FFF>.png
        std::string disk_path;       // the same file on disk when it is not inside a zip
        std::vector<uint8_t> packed; // PNG bytes read on the game thread (zipped packs)
        std::string mask_vfs_path;   // <FFF>.mask.png beside it (may not exist)
        std::string mask_disk_path;
        std::vector<uint8_t> packed_mask;
        int64_t modified; // hot reload: the file's modification time when it was decoded
    };

    struct s_hd_asset {
        std::string key;       // "<LBX>/<EEE>"
        std::string directory; // "art/<key>" or "hd/<key>" (the alias, PLAN.md section 5)
        // No sprite.json (section 5): one frame file per frame of the LBX entry, the scale
        // inferred from the 1x size, origin 0,0. Needs the entry's header (frame count, size),
        // so it is completed when the game loads the entry (Register_Load).
        bool inferred;
        std::string extension; // ".png" or ".webp" (inferred assets)
        int32_t base_width;
        int32_t base_height;
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
    // `enabled` (the HD pack was found) is declared in render.h.
    // ANTARES_HD_STATS: count draws that had no HD art, by asset, and log them
    // periodically. Debug aid for finding coverage gaps.
    extern bool stats_enabled;
    // ANTARES_HD_TRACE: logs every LBX entry load and how long draws waited for their art.
    extern bool trace_enabled;
    extern std::unordered_map<const void*, std::string> stats_names;
    // ANTARES_HD_SLOW_IO=<ms>: debug aid, every art load waits this long first, to reproduce
    // what players on slow (network, spinning) disks see when art arrives late.
    extern uint32_t slow_io_ms;
    // Game-thread time per stage (ANTARES_HD_STATS), reported in the twin stats.
    enum e_timer { TIMER_SCOPE_OPEN, // twin planes: a scope's snapshot of its rectangle
        TIMER_SCOPE_CLOSE,           // twin planes: the compare and Unported fallback when it closes
        TIMER_TWIN_RENDER,           // twin planes: drawing a draw's art into its plane (Render_Plane)
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
    bool Decode_Frame_Bytes(const uint8_t* bytes, size_t size, const std::string& name, s_hd_frame* frame, const uint8_t* mask_bytes = nullptr, size_t mask_size = 0);
    bool Load_Frame_Png(const std::string& path, s_hd_frame* frame);
    void Load_Index();
    // Reads an asset's frames list (sprite.json, the index, or inferred from `header`, the LBX
    // entry's s_animation_header, when it has no sprite.json).
    void Load_Asset(s_hd_asset* asset, const uint8_t* header = nullptr);
    // Hot reload (hot_reload.cpp; the setting is in render.h): at publish, decodes changed art
    // files again; `Art_Modified` is a frame's file time (with its mask's), -1 off disk.
    void Poll_Hot_Reload();
    int64_t Art_Modified(const s_hd_frame* frame);
    const s_hd_frame* Resolve_Frame(const s_hd_asset* asset, int32_t frame_number);
    int32_t Find_Or_Create_Asset(const char* lbx_name, int32_t entry);
    // Looks for an entry's art in loose folders (art/, then hd/); sets the asset's directory and
    // whether it is inferred (no sprite.json).
    bool Find_Loose_Art(const std::string& key, s_hd_asset* asset);
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
    void Free_Frame(s_hd_frame& frame); // a decoded frame's pixels (game thread)
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

    // Guards allocating/freeing the page planes (game thread) against the presenter reading the
    // published/present planes (facade.cpp).
    extern std::mutex planes_mutex;

    // ------------------------------------------------------------ the current draw
    // Set up by the Begin_* hooks (stamps.cpp): which art a draw uses, where, and how it is
    // recoloured.

    enum e_source {
        SOURCE_NONE, // no HD art: written pixels show the original enlarged
        SOURCE_ART,  // a sprite, glyph or file animation frame with HD art
        SOURCE_LINE, // a line (D21): its pixels are drawn as a stroke at 4x
    };

    struct s_draw {
        e_source source = SOURCE_NONE;
        const s_hd_frame* frame = nullptr;
        bool glyph = false;
        bool glassed = false;
        bool art_outline = false;
        int32_t x = 0; // where the sprite's top-left lands in the target
        int32_t y = 0;
        bool remapped = false;
        uint8_t remap[256];
        uint8_t colors[8];
        std::vector<uint8_t> shade; // brightness percent per sprite pixel, empty = none
        int32_t shade_width = 0;
        int32_t shade_height = 0;
        const void* anim = nullptr; // the header drawn (image sources)
        int32_t line_x1 = 0;        // SOURCE_LINE: the segment, in 1x pixels
        int32_t line_y1 = 0;
        int32_t line_x2 = 0;
        int32_t line_y2 = 0;
        std::string name; // for the probe
        // The frame has art that is still being decoded: written pixels show the original
        // for now, and the draw is replayed when it arrives (late art, twin_draw.cpp).
        bool waiting = false;
        // Clip window at the start of the draw (the fringe must stay inside it).
        int32_t clip_x1 = 0;
        int32_t clip_y1 = 0;
        int32_t clip_x2 = 0x7FFF;
        int32_t clip_y2 = 0x7FFF;
    };

    extern s_draw current_draw;

} // namespace render
