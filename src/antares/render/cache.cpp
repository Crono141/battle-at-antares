#include "pch.h"

#include "antares/render/internal.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <thread>

#include "yyjson.h"

namespace render {

    std::atomic<int64_t> decoded_bytes;
    uint32_t publish_count;
    int64_t cache_budget = (int64_t)4096 << 20;
    const uint64_t PREFETCH_BUDGET_NS = 2 * SDL_NS_PER_MS;

    namespace {

        // ------------------------------------------------------------ frame streaming
        //
        // Frame PNGs are decoded by worker threads, so drawing never waits for art: a draw whose
        // frame is not ready shows the original pixels and is upgraded when the frame arrives
        // (see s_deferred). Workers read files directly when the pack is a directory; for zipped
        // packs the game thread reads the bytes (the VFS is not thread safe) and workers decode.

        std::mutex queue_mutex;
        std::condition_variable queue_ready;
        std::deque<s_hd_frame*> queues[PRIORITY_COUNT];
        std::vector<std::thread> workers;
        bool workers_stop;
        bool workers_started;

        int64_t Frame_Bytes(const s_hd_frame& frame) {
            return (int64_t)frame.width * frame.height * ((frame.full_colour ? 4 : 2) + (frame.mask != nullptr ? 1 : 0));
        }

        size_t Queued_Count() {
            size_t count = 0;
            for (const std::deque<s_hd_frame*>& queue : queues) {
                count += queue.size();
            }
            return count;
        }

        void Worker() {
            for (;;) {
                s_hd_frame* frame = nullptr;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    queue_ready.wait(lock, [] { return Queued_Count() > 0 || workers_stop; });
                    if (workers_stop) {
                        return;
                    }
                    for (std::deque<s_hd_frame*>& queue : queues) {
                        if (!queue.empty()) {
                            frame = queue.front();
                            queue.pop_front();
                            break;
                        }
                    }
                }
                uint8_t expected = LOAD_QUEUED;
                if (!frame->load.compare_exchange_strong(expected, LOAD_LOADING)) {
                    continue; // already taken (queued twice) or evicted meanwhile
                }
                bool ok = false;
                const uint64_t decode_start = SDL_GetTicksNS();
                if (slow_io_ms != 0) {
                    SDL_Delay(slow_io_ms);
                }
                if (!frame->disk_path.empty()) {
                    size_t size = 0;
                    void* bytes = SDL_LoadFile(frame->disk_path.c_str(), &size);
                    size_t mask_size = 0;
                    // The mask image may not exist: a failed open is the answer.
                    void* mask = !frame->mask_disk_path.empty() ? SDL_LoadFile(frame->mask_disk_path.c_str(), &mask_size) : nullptr;
                    if (bytes != nullptr) {
                        ok = Decode_Frame_Bytes((const uint8_t*)bytes, size, frame->disk_path, frame, (const uint8_t*)mask, mask_size);
                        SDL_free(bytes);
                    }
                    SDL_free(mask);
                } else {
                    ok = !frame->packed.empty() && Decode_Frame_Bytes(frame->packed.data(), frame->packed.size(), frame->vfs_path, frame,
                                                       frame->packed_mask.empty() ? nullptr : frame->packed_mask.data(), frame->packed_mask.size());
                    std::vector<uint8_t>().swap(frame->packed);
                    std::vector<uint8_t>().swap(frame->packed_mask);
                }
                if (ok) {
                    decoded_bytes += Frame_Bytes(*frame);
                }
                if (trace_enabled) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu decoded %s in %.1f ms", (unsigned long long)SDL_GetTicks(), frame->vfs_path.c_str(), (double)(SDL_GetTicksNS() - decode_start) / 1e6);
                }
                if (art_hot_reload) {
                    frame->modified = Art_Modified(frame);
                }
                frame->load.store(ok ? LOAD_READY : LOAD_FAILED, std::memory_order_release);
            }
        }

        // Workers must be stopped before the queue's mutex and condition variable are destroyed
        // at exit (destroying them under waiting threads hangs or crashes the shutdown).
        void Stop_Workers() {
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                workers_stop = true;
                for (std::deque<s_hd_frame*>& queue : queues) {
                    queue.clear();
                }
            }
            queue_ready.notify_all();
            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            workers.clear();
        }

        // Between Begin_Preload and End_Preload: every frame requested, to wait for.
        bool preloading = false;
        std::vector<s_hd_frame*> preload_frames;

    } // namespace

    void Start_Workers() {
        if (workers_started) {
            return;
        }
        workers_started = true;
        // Decoding runs beside a single-threaded game: use half the cores (prefetch keeps them busy
        // only while there is something to read ahead).
        const int32_t count = SDL_clamp(SDL_GetNumLogicalCPUCores() / 2, 2, 12);
        for (int32_t i = 0; i < count; ++i) {
            workers.emplace_back(Worker);
        }
        // Registered after the queue globals were constructed, so it runs before they are
        // destroyed.
        std::atexit(Stop_Workers);
    }

    // Queues a frame for decoding at `priority`. A frame already queued at a lower priority is
    // queued again at the higher one; the worker skips whichever entry comes second.
    void Request_Frame(s_hd_frame* frame, e_priority priority) {
        if (frame == nullptr || frame->state != FRAME_ART) {
            return;
        }
        if (frame->file_scale <= 0 && frame->base_width <= 0) {
            return; // inferred art needs the 1x size first (Register_Load)
        }
        if (preloading) {
            preload_frames.push_back(frame);
        }
        uint8_t expected = LOAD_NONE;
        if (!frame->load.compare_exchange_strong(expected, LOAD_QUEUED)) {
            if (expected == LOAD_QUEUED && priority < frame->queued_priority) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (priority == PRIORITY_URGENT) {
                    queues[priority].push_front(frame);
                } else {
                    queues[priority].push_back(frame);
                }
                frame->queued_priority = priority;
                queue_ready.notify_one();
            }
            return;
        }
        if (frame->disk_path.empty() && frame->packed.empty()) {
            (void)Read_Vfs_File(frame->vfs_path.c_str(), &frame->packed);
            if (!frame->mask_vfs_path.empty() && vfs::File_Exists(frame->mask_vfs_path.c_str()) != 0) {
                (void)Read_Vfs_File(frame->mask_vfs_path.c_str(), &frame->packed_mask);
            }
        }
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (priority == PRIORITY_URGENT) {
            queues[priority].push_front(frame);
        } else {
            queues[priority].push_back(frame);
        }
        frame->queued_priority = priority;
        queue_ready.notify_one();
    }

    void Request_Frame(s_hd_frame* frame, bool urgent) {
        Request_Frame(frame, urgent ? PRIORITY_URGENT : PRIORITY_SCREEN);
    }

    int32_t Queue_Length() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return (int32_t)Queued_Count();
    }

    bool Frame_Ready(const s_hd_frame* frame) {
        return frame != nullptr && frame->load.load(std::memory_order_acquire) == LOAD_READY;
    }

    // Queues every frame of an asset (the game just loaded its LBX entry, so it is about to
    // be drawn).
    void Prefetch_Asset(s_hd_asset* asset) {
        if (!asset->loaded) {
            Load_Asset(asset);
        }
        asset->last_used = publish_count;
        for (int32_t i = 0; i < asset->frame_count; ++i) {
            Request_Frame(&asset->frames[i], false);
        }
    }

    void Free_Frame(s_hd_frame& frame) {
        if (frame.load.load(std::memory_order_acquire) != LOAD_READY) {
            return;
        }
        decoded_bytes -= Frame_Bytes(frame);
        free(frame.index);
        free(frame.alpha);
        free(frame.rgb);
        free(frame.mask);
        free(frame.shade_luma);
        frame.index = frame.alpha = frame.rgb = frame.mask = frame.shade_luma = nullptr;
        frame.load.store(LOAD_NONE, std::memory_order_release);
    }

    void Free_Asset_Frames(s_hd_asset* asset) {
        for (int32_t i = 0; i < asset->frame_count; ++i) {
            Free_Frame(asset->frames[i]);
        }
    }

    // Frees the decoded pixels of the least recently used assets beyond the cache budget.
    void Evict_Over_Budget() {
        if (decoded_bytes.load() <= cache_budget) {
            return;
        }
        std::vector<std::pair<uint32_t, s_hd_asset*>> candidates;
        for (int32_t i = 0; i < asset_count; ++i) {
            s_hd_asset* asset = assets[i];
            // Keep anything used within the last few seconds, and the screens in use or next.
            if (asset->frames != nullptr && !asset->keep && publish_count - asset->last_used > 120) {
                candidates.emplace_back(asset->last_used, asset);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const int64_t target = cache_budget - cache_budget / 8;
        for (const auto& candidate : candidates) {
            if (decoded_bytes.load() <= target) {
                break;
            }
            Free_Asset_Frames(candidate.second);
        }
    }

    namespace {

        // ------------------------------------------------------------- screen map
        //
        // hd/screens.json (optional, generated by Moo2Mods tools/screens.py) describes the game's
        // screens: the HD assets each one draws, the LBX files whose loading means it is being
        // entered, and the screens reachable from it. Art is decoded ahead along that graph:
        //   - "startup" screens are queued in the background as soon as the pack is found;
        //   - entering a screen queues its "next" screens;
        //   - the shown screen, its next screens, "resident" screens once queued and startup
        //     screens until "released" are never evicted;
        //   - entering a screen frees the decoded art of the screens it "releases".
        // Assets are queued by a pump on the game thread with a small time budget per frame
        // (reading sprite.json touches the VFS, which is not thread safe).

        struct s_screen {
            std::string name;
            std::vector<int32_t> assets;
            std::vector<int32_t> next;
            std::vector<int32_t> release;
            bool resident = false;
            bool startup = false;
            bool queued = false;   // its assets were queued at least once
            bool released = false; // a later screen released it (startup screens)
        };

        std::vector<s_screen> screens;
        std::unordered_map<std::string, int32_t> screen_by_enter; // upper-case LBX name
        int32_t current_screen = -1;
        // Assets waiting for the pump, with the priority to queue their frames at.
        std::deque<std::pair<int32_t, e_priority>> prefetch_pending;

        std::vector<int32_t> Screen_Indices(yyjson_val* names, const std::unordered_map<std::string, int32_t>& by_name) {
            std::vector<int32_t> result;
            size_t index = 0;
            size_t max = 0;
            yyjson_val* item = nullptr;
            yyjson_arr_foreach(names, index, max, item) {
                const char* name = yyjson_get_str(item);
                const auto found = name != nullptr ? by_name.find(name) : by_name.end();
                if (found != by_name.end()) {
                    result.push_back(found->second);
                }
            }
            return result;
        }

    } // namespace

    void Load_Screen_Map() {
        std::vector<uint8_t> json;
        if (!Read_Vfs_File("art/screens.json", &json) && !Read_Vfs_File("hd/screens.json", &json)) {
            return;
        }
        yyjson_doc* document = yyjson_read((const char*)json.data(), json.size(), 0);
        yyjson_val* root = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
        yyjson_val* list = root != nullptr ? yyjson_obj_get(root, "screens") : nullptr;
        if (!yyjson_is_arr(list)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: invalid screens.json");
            yyjson_doc_free(document);
            return;
        }
        std::unordered_map<std::string, int32_t> by_name;
        size_t index = 0;
        size_t max = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_foreach(list, index, max, item) {
            const char* name = yyjson_get_str(yyjson_obj_get(item, "name"));
            if (name != nullptr) {
                by_name[name] = (int32_t)screens.size();
                screens.emplace_back();
                screens.back().name = name;
            }
        }
        yyjson_arr_foreach(list, index, max, item) {
            const char* name = yyjson_get_str(yyjson_obj_get(item, "name"));
            if (name == nullptr) {
                continue;
            }
            const int32_t screen_index = by_name[name];
            s_screen& screen = screens[(size_t)screen_index];
            screen.resident = yyjson_get_bool(yyjson_obj_get(item, "resident"));
            screen.next = Screen_Indices(yyjson_obj_get(item, "next"), by_name);
            screen.release = Screen_Indices(yyjson_obj_get(item, "release"), by_name);
            size_t enter_index = 0;
            size_t enter_max = 0;
            yyjson_val* enter = nullptr;
            yyjson_arr_foreach(yyjson_obj_get(item, "enter"), enter_index, enter_max, enter) {
                if (yyjson_is_str(enter)) {
                    std::string lbx = yyjson_get_str(enter);
                    for (char& c : lbx) {
                        c = (char)SDL_toupper((unsigned char)c);
                    }
                    screen_by_enter[lbx] = screen_index;
                }
            }
            // "assets": ["<LBX>/<EEE>", ...]
            size_t asset_index = 0;
            size_t asset_max = 0;
            yyjson_val* asset = nullptr;
            yyjson_arr_foreach(yyjson_obj_get(item, "assets"), asset_index, asset_max, asset) {
                if (yyjson_is_str(asset)) {
                    const int32_t found = Find_Or_Create_Known_Asset(yyjson_get_str(asset));
                    if (found >= 0) {
                        screen.assets.push_back(found);
                    }
                }
            }
        }
        for (int32_t startup : Screen_Indices(yyjson_obj_get(root, "startup"), by_name)) {
            screens[(size_t)startup].startup = true;
        }
        yyjson_doc_free(document);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: screen map with %d screens", (int)screens.size());
    }

    namespace {

        bool Screen_Kept(int32_t index) {
            const s_screen& screen = screens[(size_t)index];
            if (index == current_screen || (screen.resident && screen.queued) || (screen.startup && !screen.released)) {
                return true;
            }
            if (current_screen >= 0) {
                for (int32_t next : screens[(size_t)current_screen].next) {
                    if (next == index) {
                        return true;
                    }
                }
            }
            return false;
        }

        void Update_Keep() {
            for (const s_screen& screen : screens) {
                for (int32_t asset : screen.assets) {
                    assets[asset]->keep = false;
                }
            }
            for (int32_t i = 0; i < (int32_t)screens.size(); ++i) {
                if (Screen_Kept(i)) {
                    for (int32_t asset : screens[(size_t)i].assets) {
                        assets[asset]->keep = true;
                    }
                }
            }
        }

        void Queue_Screen(int32_t index, e_priority priority) {
            s_screen& screen = screens[(size_t)index];
            screen.queued = true;
            for (int32_t asset : screen.assets) {
                prefetch_pending.emplace_back(asset, priority);
            }
        }

        void Enter_Screen(int32_t index) {
            if (index == current_screen) {
                return;
            }
            current_screen = index;
            const s_screen& screen = screens[(size_t)index];
            if (trace_enabled) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd trace %llu screen %s", (unsigned long long)SDL_GetTicks(), screen.name.c_str());
            }
            for (int32_t released : screen.release) {
                screens[(size_t)released].released = true;
            }
            // The screen's own entries arrive through Register_Load; queue the next screens first.
            std::deque<std::pair<int32_t, e_priority>> next;
            for (int32_t following : screen.next) {
                screens[(size_t)following].queued = true;
                for (int32_t asset : screens[(size_t)following].assets) {
                    next.emplace_back(asset, PRIORITY_NEXT);
                }
            }
            prefetch_pending.insert(prefetch_pending.begin(), next.begin(), next.end());
            Update_Keep();
            for (int32_t released : screen.release) {
                for (int32_t asset : screens[(size_t)released].assets) {
                    if (!assets[asset]->keep && assets[asset]->frames != nullptr) {
                        Free_Asset_Frames(assets[asset]);
                    }
                }
            }
        }

    } // namespace

    // Queues pending prefetch assets until the time budget is used up.
    void Pump_Prefetch(uint64_t budget_ns) {
        const uint64_t start = SDL_GetTicksNS();
        while (!prefetch_pending.empty() && SDL_GetTicksNS() - start < budget_ns) {
            if (decoded_bytes.load() > cache_budget) {
                break; // full of art in use: wait for eviction
            }
            const auto [index, priority] = prefetch_pending.front();
            prefetch_pending.pop_front();
            s_hd_asset* asset = assets[index];
            if (!asset->keep) {
                continue; // released meanwhile
            }
            if (!asset->loaded) {
                Load_Asset(asset);
            }
            asset->last_used = publish_count;
            for (int32_t i = 0; i < asset->frame_count; ++i) {
                Request_Frame(&asset->frames[i], priority);
            }
        }
    }

    void Queue_Startup_Art() {
        if (!screens.empty()) {
            for (int32_t i = 0; i < (int32_t)screens.size(); ++i) {
                if (screens[(size_t)i].startup) {
                    Queue_Screen(i, PRIORITY_BACKGROUND);
                }
            }
            Update_Keep();
            Pump_Prefetch(20 * SDL_NS_PER_MS);
        } else {
            // No screen map: the title screen is the first thing shown, decode it right away.
            for (int32_t entry = 0; entry < 40; ++entry) {
                const int32_t index = Find_Or_Create_Asset("MAINMENU.LBX", entry);
                if (index >= 0) {
                    Prefetch_Asset(assets[index]);
                }
            }
        }
    }

    void Enter_Screen_Of(const char* lbx_name) {
        if (!screen_by_enter.empty()) {
            std::string lbx = lbx_name;
            for (char& c : lbx) {
                c = (char)SDL_toupper((unsigned char)c);
            }
            const auto screen = screen_by_enter.find(lbx);
            if (screen != screen_by_enter.end()) {
                Enter_Screen(screen->second);
            }
        }
    }

    void Preload_Asset(const char* lbx_name, int32_t entry, int32_t first_frames) {
        if (!enabled) {
            return;
        }
        const int32_t index = Find_Or_Create_Asset(lbx_name, entry);
        if (index < 0) {
            return;
        }
        s_hd_asset* asset = assets[index];
        if (!asset->loaded) {
            Load_Asset(asset);
        }
        asset->last_used = publish_count;
        const bool was_preloading = preloading;
        for (int32_t i = 0; i < asset->frame_count; ++i) {
            // The frames waited for go ahead of everything queued; the rest follow in order.
            preloading = was_preloading && i < first_frames;
            Request_Frame(&asset->frames[i], i < first_frames ? PRIORITY_URGENT : PRIORITY_SCREEN);
        }
        preloading = was_preloading;
    }

    void Read_Ahead(s_hd_asset* asset, int32_t frame, int32_t count) {
        // Animations loop, so the frames after the last one are the first ones again.
        for (int32_t i = 1; i <= count && i < asset->frame_count; ++i) {
            const s_hd_frame* ahead = Resolve_Frame(asset, (frame + i) % asset->frame_count);
            if (ahead != nullptr && !Frame_Ready(ahead)) {
                Request_Frame(const_cast<s_hd_frame*>(ahead), PRIORITY_SCREEN);
            }
        }
    }

    void Begin_Preload() {
        if (!enabled) {
            return;
        }
        preloading = true;
        preload_frames.clear();
    }

    void End_Preload(uint32_t max_ms) {
        if (!enabled || !preloading) {
            preloading = false;
            return;
        }
        const uint64_t start = SDL_GetTicks();
        // Screens entered meanwhile queued their assets for the pump: queue them all now.
        Pump_Prefetch((uint64_t)max_ms * SDL_NS_PER_MS);
        preloading = false;
        size_t waiting = 0;
        for (;;) {
            waiting = 0;
            for (s_hd_frame* frame : preload_frames) {
                const uint8_t state = frame->load.load(std::memory_order_acquire);
                waiting += state == LOAD_QUEUED || state == LOAD_LOADING ? 1 : 0;
            }
            if (waiting == 0 || SDL_GetTicks() - start >= max_ms) {
                break;
            }
            SDL_Delay(5);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: preloaded %zu frames in %llu ms%s", preload_frames.size(), (unsigned long long)(SDL_GetTicks() - start), waiting != 0 ? " (gave up waiting)" : "");
        preload_frames.clear();
    }

    void Shutdown() {
        if (workers_started) {
            Stop_Workers();
        }
    }

} // namespace render
