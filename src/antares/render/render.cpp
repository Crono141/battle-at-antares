#include "pch.h"

#include "antares/render/internal.h"
#include "antares/identity.h"

#include <algorithm>

namespace render {

    bool initialized;
    bool enabled;
    bool stats_enabled;
    bool trace_enabled;
    std::unordered_map<const void*, std::string> stats_names;
    uint32_t slow_io_ms = 0;
    uint64_t stats_timer_ns[TIMER_COUNT];
    int32_t probe_x = -1;
    int32_t probe_y = -1;
    std::string capture_dir;

    namespace {

        std::unordered_map<std::string, uint32_t> stats_missing;
        uint32_t stats_draws;
        uint32_t stats_interval = 20000;
        // ANTARES_HD_CAPTURE=<dir>: write the live palette the first time each LBX sprite is
        // drawn under it, as <dir>/<LBX>_<EEE>_<hash>.pal (256 x RGB, 8-bit). The HD pipeline uses
        // these instead of palettes inferred from the source code.
        std::unordered_map<std::string, bool> captured;

    } // namespace

    void Capture_Palette(const std::string& name) {
        uint8_t rgb[768];
        uint32_t hash = 2166136261u;
        for (int32_t i = 0; i < 256; ++i) {
            const s_palette_entry& entry = video::current_palette[i];
            rgb[i * 3 + 0] = (uint8_t)((entry.r & 0x3F) * 255 / 63);
            rgb[i * 3 + 1] = (uint8_t)((entry.g & 0x3F) * 255 / 63);
            rgb[i * 3 + 2] = (uint8_t)((entry.b & 0x3F) * 255 / 63);
        }
        for (uint8_t byte : rgb) {
            hash = (hash ^ byte) * 16777619u;
        }
        char file[160];
        std::string base = name;
        std::replace(base.begin(), base.end(), '#', '_');
        snprintf(file, sizeof(file), "%s_%08x.pal", base.c_str(), hash);
        if (!captured.emplace(file, true).second) {
            return;
        }
        const std::string path = capture_dir + "/" + file;
        SDL_IOStream* out = SDL_IOFromFile(path.c_str(), "wb");
        if (out != nullptr) {
            (void)SDL_WriteIO(out, rgb, sizeof(rgb));
            SDL_CloseIO(out);
        }
    }

    void Stats_Missing(const std::string& key) {
        stats_missing[key]++;
        if (++stats_draws % stats_interval != 0) {
            return;
        }
        std::vector<std::pair<uint32_t, std::string>> top;
        for (const auto& item : stats_missing) {
            top.emplace_back(item.second, item.first);
        }
        std::sort(top.rbegin(), top.rend());
        std::string line;
        for (size_t i = 0; i < top.size() && i < 25; ++i) {
            line += " " + top[i].second + "=" + std::to_string(top[i].first);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd stats: draws without HD art:%s", line.c_str());
    }

    void Initialize() {
        if (initialized) {
            return;
        }
        initialized = true;
        // An art pack: art/ (format v2) or its alias hd/, marked by pack.json or an index.
        for (const char* marker : {"art/pack.json", "hd/pack.json", "art/index.json", "hd/index.json"}) {
            enabled = enabled || vfs::File_Exists(marker) != 0;
        }
        const char* stats = antares::Get_Env("HD_STATS");
        stats_enabled = enabled && stats != nullptr;
        trace_enabled = enabled && antares::Get_Env("HD_TRACE") != nullptr;
        const char* capture = antares::Get_Env("HD_CAPTURE");
        if (capture != nullptr && capture[0] != '\0') {
            capture_dir = capture;
            (void)SDL_CreateDirectory(capture_dir.c_str());
        }
        const char* probe = antares::Get_Env("HD_PROBE");
        if (probe != nullptr) {
            (void)sscanf(probe, "%d,%d", &probe_x, &probe_y);
        }
        if (stats_enabled && atoi(stats) > 1) {
            stats_interval = (uint32_t)atoi(stats);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: %s", enabled ? "HD sprite pack found (twin planes)" : "no HD sprite pack");
        const char* slow_io = antares::Get_Env("HD_SLOW_IO");
        if (slow_io != nullptr && atoi(slow_io) > 0) {
            slow_io_ms = (uint32_t)atoi(slow_io);
        }
        const char* budget = antares::Get_Env("HD_CACHE_MB");
        if (budget != nullptr && atoi(budget) > 0) {
            cache_budget = (int64_t)atoi(budget) << 20;
        }
        if (enabled) {
            Start_Workers();
            Load_Index();
            Load_Screen_Map();
            Queue_Startup_Art();
        }
        if (enabled && video::g_page_off.pixels != nullptr) {
            Allocate_Planes(video::g_page_off.width, video::g_page_off.height);
        }
    }

    bool Enabled() {
        return enabled;
    }

} // namespace render
