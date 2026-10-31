#include "pch.h"

#include "antares/render/internal.h"
#include <thread>

namespace render {

    namespace {

        uint64_t trace_wait_start;
        size_t trace_wait_peak;
        uint32_t stats_publishes;
        uint32_t stats_settled_pixels;
        uint64_t stats_last_publish_ns;
        uint64_t stats_longest_frame_ns;

    } // namespace

    void Publish_Plane(const uint8_t* published_page_pixels) {
        (void)published_page_pixels;
        if (page_off.hd.empty()) {
            return;
        }
        Flush_Pending();
        if (trace_enabled) {
            static uint64_t last_publish;
            const uint64_t now = SDL_GetTicks();
            if (last_publish != 0 && now - last_publish >= 100) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu gap %llu ms between frames (sprite.json reading %.0f ms)", (unsigned long long)now, (unsigned long long)(now - last_publish), (double)trace_meta_ns / 1e6);
            }
            trace_meta_ns = 0;
            last_publish = now;
        }
        if (stats_enabled) {
            const uint64_t now = SDL_GetTicksNS();
            if (stats_last_publish_ns != 0) {
                stats_longest_frame_ns = SDL_max(stats_longest_frame_ns, now - stats_last_publish_ns);
            }
            stats_last_publish_ns = now;
        }
        s_scoped_timer timer(TIMER_PUBLISH);
        {
            s_scoped_timer part(TIMER_UPGRADE);
            Upgrade_Deferred();
        }
        if (trace_enabled) {
            if (Deferred_Count() != 0) {
                trace_wait_start = trace_wait_start != 0 ? trace_wait_start : SDL_GetTicks();
                trace_wait_peak = SDL_max(trace_wait_peak, Deferred_Count());
            } else if (trace_wait_start != 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu waited %llu ms for art (%d draws at most)", (unsigned long long)SDL_GetTicks(), (unsigned long long)(SDL_GetTicks() - trace_wait_start), (int)trace_wait_peak);
                trace_wait_start = 0;
                trace_wait_peak = 0;
            }
        }
        publish_count++;
        {
            s_scoped_timer part(TIMER_EVICT);
            Evict_Over_Budget();
        }
        {
            s_scoped_timer part(TIMER_PREFETCH);
            Pump_Prefetch(PREFETCH_BUDGET_NS);
        }
        // Settle pixels changed by code without hooks: the original pixel, enlarged.
        uint32_t settled;
        {
            s_scoped_timer part(TIMER_SETTLE);
            settled = Settle(page_off, video::g_page_off.pixels, 0, 0, page_off.width, page_off.height);
        }
        if (stats_enabled) {
            stats_settled_pixels += settled;
            if (++stats_publishes % 100 == 0) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "hd stats: %u pixels settled to plain scaling over the last 100 frames; per frame: render %.2f ms, "
                    "page copies %.2f ms, publish %.2f ms (upgrades %.2f, eviction %.2f, prefetch %.2f, settle %.2f), longest frame %.0f ms; "
                    "cache %lld MB decoded, %d frames queued, %d draws waiting for art",
                    stats_settled_pixels,
                    (double)stats_timer_ns[TIMER_RENDER] / 1e8,
                    (double)stats_timer_ns[TIMER_COPY] / 1e8,
                    (double)stats_timer_ns[TIMER_PUBLISH] / 1e8,
                    (double)stats_timer_ns[TIMER_UPGRADE] / 1e8,
                    (double)stats_timer_ns[TIMER_EVICT] / 1e8,
                    (double)stats_timer_ns[TIMER_PREFETCH] / 1e8,
                    (double)stats_timer_ns[TIMER_SETTLE] / 1e8,
                    (double)stats_longest_frame_ns / 1e6,
                    (long long)(decoded_bytes.load() >> 20),
                    Queue_Length(),
                    (int)Deferred_Count());
                stats_settled_pixels = 0;
                memset(stats_timer_ns, 0, sizeof(stats_timer_ns));
                stats_longest_frame_ns = 0;
            }
        }
        const size_t row_size = (size_t)page_off.width * S * S;
        for (int32_t y = 0; y < page_off.height; ++y) {
            if (page_off.row_dirty[(size_t)y] == 0) {
                continue;
            }
            memcpy(&page_published.hd[(size_t)y * row_size], &page_off.hd[(size_t)y * row_size], row_size * sizeof(uint32_t));
            page_off.row_dirty[(size_t)y] = 0;
            page_published.row_dirty[(size_t)y] = 1;
        }
        // Literal pixels follow screen fades: compare the shown palette with the base palette.
        uint64_t base = 0;
        uint64_t shown = 0;
        for (int32_t i = 1; i < 256; ++i) {
            const s_palette_entry& entry = video::current_palette[i];
            base += (uint64_t)((entry.r & 0x3F) + (entry.g & 0x3F) + (entry.b & 0x3F)) * 4;
            shown += (uint64_t)video::g_palette_entries[i].r + video::g_palette_entries[i].g + video::g_palette_entries[i].b;
        }
        published_fade = base == 0 ? 256u : (uint32_t)SDL_min((uint64_t)256, shown * 256 / base);
    }

    void Snapshot_Plane() {
        std::lock_guard<std::mutex> lock(planes_mutex);
        if (page_published.hd.empty()) {
            return;
        }
        const size_t row_size = (size_t)page_published.width * S * S;
        for (int32_t y = 0; y < page_published.height; ++y) {
            if (page_published.row_dirty[(size_t)y] == 0) {
                continue;
            }
            memcpy(&page_present.hd[(size_t)y * row_size], &page_published.hd[(size_t)y * row_size], row_size * sizeof(uint32_t));
            page_published.row_dirty[(size_t)y] = 0;
            page_present.row_dirty[(size_t)y] = 1;
        }
        present_fade = published_fade;
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

    bool Resolve(const video::s_palette_color* palette, uint32_t* output, int32_t* row_first, int32_t* row_last) {
        static uint32_t last_palette[256];
        static uint32_t last_fade = 0xFFFFFFFFu;
        std::lock_guard<std::mutex> lock(planes_mutex);
        if (page_present.hd.empty()) {
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
        const size_t row_size = (size_t)page_present.width * S * S;
        std::vector<int32_t> rows;
        for (int32_t y = 0; y < page_present.height; ++y) {
            if (all || page_present.row_dirty[(size_t)y] != 0) {
                page_present.row_dirty[(size_t)y] = 0;
                rows.push_back(y);
            }
        }
        if (rows.empty()) {
            return false;
        }
        const int32_t first = rows.front();
        const int32_t last = rows.back();
        auto resolve_rows = [&](size_t begin, size_t end) {
            for (size_t r = begin; r < end; ++r) {
                const int32_t y = rows[r];
                const uint32_t* src = &page_present.hd[(size_t)y * row_size];
                uint32_t* dst = output + (size_t)y * row_size;
                for (size_t i = 0; i < row_size; ++i) {
                    const uint32_t pixel = src[i];
                    uint32_t rgb;
                    if ((pixel & HD_LITERAL) != 0) {
                        rgb = pixel & 0xFFFFFFu;
                        if (fade < 256) {
                            rgb = ((((rgb >> 16) & 0xFF) * fade >> 8) << 16) | ((((rgb >> 8) & 0xFF) * fade >> 8) << 8) |
                                ((rgb & 0xFF) * fade >> 8);
                        }
                    } else {
                        const uint32_t alpha = (pixel >> 16) & 0xFF;
                        rgb = colors[pixel & 0xFF];
                        if (alpha < 255) {
                            rgb = Mix(rgb, (pixel & HD_CLEAR) != 0 ? 0u : colors[(pixel >> 8) & 0xFF], alpha);
                        }
                    }
                    dst[i] = 0xFF000000u | rgb;
                }
            }
        };
        // Whole-screen updates (palette fades and cycling) are split across threads.
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
        *row_first = first * S;
        *row_last = (last + 1) * S;
        return true;
    }

} // namespace render
