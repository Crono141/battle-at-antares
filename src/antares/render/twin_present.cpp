#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"

#include <string>
#include <thread>

// Twin planes, WP1.3a: publishing the off plane, the presenter's snapshot, and resolving plane
// pixels to ARGB. Same flow as attempt 2's present.cpp, on the base-plus-mask pixels.
namespace render::twin {

    namespace {

        // Factor (0-256) applied to `none` pixels so they follow palette fades.
        uint32_t published_fade = 256;
        uint32_t present_fade = 256;
        // Unported calls of the frame published last and of the frame being presented.
        std::string published_unported;
        std::string present_unported;
        uint32_t window_publishes;
        uint64_t window_publish_ns;

        void Copy_Dirty_Rows(s_plane& from, s_plane& to) {
            const size_t row_size = (size_t)from.width * S * S;
            for (int32_t y = 0; y < from.height; ++y) {
                if (from.row_dirty[(size_t)y] == 0) {
                    continue;
                }
                memcpy(&to.px[(size_t)y * row_size], &from.px[(size_t)y * row_size], row_size * sizeof(s_px));
                from.row_dirty[(size_t)y] = 0;
                to.row_dirty[(size_t)y] = 1;
            }
        }

    } // namespace

    void Publish() {
        if (plane_off.px.empty()) {
            return;
        }
        const uint64_t start = SDL_GetTicksNS();
        std::string names;
        uint32_t pixels = 0;
        End_Frame_Unported(&names, &pixels);
        published_unported = names;
        Replay_Late_Art();
        publish_count++;
        Evict_Over_Budget();
        Pump_Prefetch(PREFETCH_BUDGET_NS);
        Poll_Hot_Reload();
        Copy_Dirty_Rows(plane_off, plane_published);
        // `none` pixels follow screen fades: compare the shown palette with the base palette.
        uint64_t base = 0;
        uint64_t shown = 0;
        for (int32_t i = 1; i < 256; ++i) {
            const s_palette_entry& entry = video::current_palette[i];
            base += (uint64_t)((entry.r & 0x3F) + (entry.g & 0x3F) + (entry.b & 0x3F)) * 4;
            shown += (uint64_t)video::g_palette_entries[i].r + video::g_palette_entries[i].g + video::g_palette_entries[i].b;
        }
        published_fade = base == 0 ? 256u : (uint32_t)SDL_min((uint64_t)256, shown * 256 / base);
        window_publish_ns += SDL_GetTicksNS() - start;
        if (stats_enabled && ++window_publishes % 100 == 0) {
            std::string window;
            uint32_t window_pixels = 0;
            uint64_t scope_ns = 0;
            End_Window_Unported(&window, &window_pixels, &scope_ns);
            size_t image_bytes = 0;
            for (const auto& item : images) {
                image_bytes += item.second.px.capacity() * sizeof(s_px);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "twin stats: last 100 frames: publish %.2f ms, drawing scopes %.2f ms per frame; %zu image planes (%zu MB); %zu draws waiting for art; "
                "Unported fallback %u pixels: %s",
                (double)window_publish_ns / 1e8,
                (double)scope_ns / 1e8,
                images.size(),
                image_bytes >> 20,
                Late_Count(),
                window_pixels,
                window.empty() ? "(none)" : window.c_str());
            static uint64_t window_started;
            const uint64_t now = SDL_GetTicksNS();
            const double window_ms = window_started != 0 ? (double)(now - window_started) / 1e6 : 0.0;
            window_started = now;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin stats: 100 frames took %.0f ms; slowest scopes: %s; per frame: snapshot %.2f ms, close %.2f ms, art %.2f ms", window_ms, window_slowest.empty() ? "(none)" : window_slowest.c_str(), (double)stats_timer_ns[TIMER_SCOPE_OPEN] / 1e8, (double)stats_timer_ns[TIMER_SCOPE_CLOSE] / 1e8, (double)stats_timer_ns[TIMER_TWIN_RENDER] / 1e8);
            stats_timer_ns[TIMER_SCOPE_OPEN] = stats_timer_ns[TIMER_SCOPE_CLOSE] = stats_timer_ns[TIMER_TWIN_RENDER] = 0;
            // Resident memory, to spot growth over a long session (Linux; 0 elsewhere).
            long long rss_mb = 0;
            if (FILE* statm = fopen("/proc/self/statm", "r")) {
                long long size = 0;
                long long resident = 0;
                if (fscanf(statm, "%lld %lld", &size, &resident) == 2) {
                    rss_mb = resident * 4096 >> 20;
                }
                fclose(statm);
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin stats: %s; cache %lld MB decoded, %d frames queued; process %lld MB resident; %u image planes dropped by allocator rewinds", Late_Stats().c_str(), (long long)(decoded_bytes.load() >> 20), Queue_Length(), rss_mb, rewind_drops);
            rewind_drops = 0;
            if (!bitmap_twin_calls.empty()) {
                std::string calls;
                for (const auto& item : bitmap_twin_calls) {
                    calls += (calls.empty() ? "" : ",") + item.first + "=" + std::to_string(item.second);
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin stats: bitmap twins: %s", calls.c_str());
                bitmap_twin_calls.clear();
            }
            window_publish_ns = 0;
        }
    }

    void Snapshot() {
        std::lock_guard<std::mutex> lock(planes_mutex);
        if (plane_published.px.empty()) {
            return;
        }
        Copy_Dirty_Rows(plane_published, plane_present);
        present_fade = published_fade;
        present_unported = published_unported;
    }

    std::string Presented_Unported() {
        return present_unported;
    }

    bool Resolve(const video::s_palette_color* palette, uint32_t* output, int32_t* row_first, int32_t* row_last) {
        static uint32_t last_palette[256];
        static uint32_t last_fade = 0xFFFFFFFFu;
        std::lock_guard<std::mutex> lock(planes_mutex);
        if (plane_present.px.empty()) {
            return false;
        }
        uint32_t colors[256];
        for (int32_t i = 0; i < 256; ++i) {
            colors[i] = ((uint32_t)palette[i].r << 16) | ((uint32_t)palette[i].g << 8) | palette[i].b;
        }
        const bool all = memcmp(colors, last_palette, sizeof(colors)) != 0 || present_fade != last_fade;
        memcpy(last_palette, colors, sizeof(colors));
        last_fade = present_fade;
        const uint32_t fade = present_fade;
        const size_t row_size = (size_t)plane_present.width * S * S;
        std::vector<int32_t> rows;
        for (int32_t y = 0; y < plane_present.height; ++y) {
            if (all || plane_present.row_dirty[(size_t)y] != 0) {
                plane_present.row_dirty[(size_t)y] = 0;
                rows.push_back(y);
            }
        }
        if (rows.empty()) {
            return false;
        }
        auto resolve_rows = [&](size_t begin, size_t end) {
            for (size_t r = begin; r < end; ++r) {
                const int32_t y = rows[r];
                const s_px* src = &plane_present.px[(size_t)y * row_size];
                uint32_t* dst = output + (size_t)y * row_size;
                for (size_t i = 0; i < row_size; ++i) {
                    const s_px& px = src[i];
                    uint32_t rgb;
                    if (Kind(px) == KIND_INDEX) {
                        const uint32_t alpha = (px.mask >> 16) & 0xFF;
                        rgb = colors[px.mask & 0xFF];
                        if (alpha < 255) {
                            rgb = Mix(rgb, (px.mask & MASK_CLEAR) != 0 ? 0u : colors[(px.mask >> 8) & 0xFF], alpha);
                        }
                    } else {
                        rgb = px.base & 0xFFFFFFu;
                        if (fade < 256) {
                            rgb = ((((rgb >> 16) & 0xFF) * fade >> 8) << 16) | ((((rgb >> 8) & 0xFF) * fade >> 8) << 8) |
                                ((rgb & 0xFF) * fade >> 8);
                        }
                    }
                    dst[i] = 0xFF000000u | rgb;
                }
            }
        };
        const size_t workers = rows.size() >= 64 ? SDL_min((size_t)8, (size_t)SDL_max(1, SDL_GetNumLogicalCPUCores() / 2)) : 1;
        if (workers <= 1) {
            resolve_rows(0, rows.size());
        } else {
            std::vector<std::thread> threads;
            const size_t chunk = (rows.size() + workers - 1) / workers;
            for (size_t w = 1; w < workers; ++w) {
                const size_t begin = SDL_min(rows.size(), w * chunk);
                const size_t end = SDL_min(rows.size(), begin + chunk);
                threads.emplace_back(resolve_rows, begin, end);
            }
            resolve_rows(0, SDL_min(rows.size(), chunk));
            for (std::thread& thread : threads) {
                thread.join();
            }
        }
        *row_first = rows.front() * S;
        *row_last = (rows.back() + 1) * S;
        return true;
    }

    void Literal_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb) {
        const uint8_t* pixels = nullptr;
        int32_t page_width = 0;
        int32_t page_height = 0;
        s_plane* plane = Resolve_Target(page, &pixels, &page_width, &page_height);
        if (plane == nullptr || plane->px.empty() || rgb == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const size_t stride = (size_t)plane->width * S;
        const size_t rgb_stride = (size_t)width * S * 3;
        const int32_t x0 = SDL_max(x, 0);
        const int32_t y0 = SDL_max(y, 0);
        const int32_t x1 = SDL_min(x + width, plane->width);
        const int32_t y1 = SDL_min(y + height, plane->height);
        for (int32_t py = y0; py < y1; ++py) {
            for (int32_t px = x0; px < x1; ++px) {
                s_px* block = Block(*plane, px, py);
                const uint8_t* source = rgb + (size_t)(py - y) * S * rgb_stride + (size_t)(px - x) * S * 3;
                for (int32_t sy = 0; sy < S; ++sy) {
                    const uint8_t* in = source + (size_t)sy * rgb_stride;
                    for (int32_t sx = 0; sx < S; ++sx, in += 3) {
                        block[sy * stride + sx] = Literal_Px(((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) | in[2]);
                    }
                }
            }
            plane->row_dirty[(size_t)py] = 1;
        }
        Accept_Rect(page, x0, y0, x1, y1); // the operation that wrote the frame is handled
    }

} // namespace render::twin
