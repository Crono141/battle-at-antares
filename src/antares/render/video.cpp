#include "pch.h"

#include "antares/render/render.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "webp/decode.h"
#include "yyjson.h"

namespace render {

    namespace {
        // HD frames read and decoded ahead of the one shown (about a second at 26 fps).
        constexpr int32_t LOOKAHEAD = 24;
        constexpr int32_t WORKER_COUNT = 3;

        struct s_job {
            int32_t index;
            std::vector<uint8_t> bytes;
        };

        struct s_state {
            bool active = false;
            std::string directory;
            int32_t width = 0; // source frame size in page pixels
            int32_t height = 0;
            int32_t ratio = 1; // HD frames per source frame (1 or 2)
            int32_t hd_count = 0;
            int32_t requested = 0; // HD frames [0, requested) were handed to the workers

            // The source frame shown last and its page pixels (restored under the in-between frame).
            int32_t shown = -1;
            int32_t left = 0;
            int32_t top = 0;
            std::vector<uint8_t> shown_pixels;

            std::mutex mutex;
            std::condition_variable work_ready;
            std::condition_variable frame_ready;
            std::deque<s_job> jobs;
            std::map<int32_t, std::vector<uint8_t>> decoded; // empty vector: failed
            bool stop = false;
            std::vector<std::thread> workers;

            // Logged when the video closes.
            int32_t frames_hd = 0;
            int32_t frames_sd = 0;
            int32_t between_shown = 0;
            int32_t between_skipped = 0;
        };

        s_state state;

        bool Read_Vfs_File(const char* path, std::vector<uint8_t>* bytes) {
            s_vfs_file* file = vfs::fopen(path, "rb");
            if (file == nullptr) {
                return false;
            }
            const int32_t size = vfs::fsize(file);
            bool ok = size > 0;
            if (ok) {
                bytes->resize((size_t)size);
                ok = vfs::fread(bytes->data(), 1, bytes->size(), file) == bytes->size();
            }
            vfs::fclose(file);
            return ok;
        }

        void Worker() {
            const int32_t width = state.width * SCALE;
            const int32_t height = state.height * SCALE;
            for (;;) {
                s_job job;
                {
                    std::unique_lock<std::mutex> lock(state.mutex);
                    state.work_ready.wait(lock, [] { return !state.jobs.empty() || state.stop; });
                    if (state.stop) {
                        return;
                    }
                    job = std::move(state.jobs.front());
                    state.jobs.pop_front();
                }
                std::vector<uint8_t> rgb((size_t)width * height * 3);
                int decoded_width = 0;
                int decoded_height = 0;
                const bool ok = WebPGetInfo(job.bytes.data(), job.bytes.size(), &decoded_width, &decoded_height) &&
                    decoded_width == width && decoded_height == height &&
                    WebPDecodeRGBInto(job.bytes.data(), job.bytes.size(), rgb.data(), rgb.size(), width * 3) != nullptr;
                if (!ok) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: unable to decode video frame %s/%06d.webp", state.directory.c_str(), job.index);
                    rgb.clear();
                }
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.decoded[job.index] = std::move(rgb);
                }
                state.frame_ready.notify_all();
            }
        }

        // Reads the files of the HD frames up to `last` (game thread: the VFS is not thread safe)
        // and queues them for decoding.
        void Request_Up_To(int32_t last) {
            last = SDL_min(last, state.hd_count - 1);
            while (state.requested <= last) {
                s_job job;
                job.index = state.requested++;
                char path[512];
                snprintf(path, sizeof(path), "%s/%06d.webp", state.directory.c_str(), job.index);
                if (!Read_Vfs_File(path, &job.bytes)) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.decoded[job.index] = std::vector<uint8_t>();
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.jobs.push_back(std::move(job));
                }
                state.work_ready.notify_one();
            }
        }

        // The decoded HD frame `index`, waiting for it until `deadline_ns` at most; nullptr when
        // it is not ready (or failed). Frames before it are dropped.
        const std::vector<uint8_t>* Take_Frame(int32_t index, uint64_t deadline_ns) {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.decoded.erase(state.decoded.begin(), state.decoded.lower_bound(index));
            for (;;) {
                auto found = state.decoded.find(index);
                if (found != state.decoded.end()) {
                    return found->second.empty() ? nullptr : &found->second;
                }
                const uint64_t now = SDL_GetTicksNS();
                if (now >= deadline_ns) {
                    return nullptr;
                }
                state.frame_ready.wait_for(lock, std::chrono::nanoseconds(deadline_ns - now));
            }
        }

        void Copy_Rect_From_Page(std::vector<uint8_t>* pixels) {
            const uint8_t* page = (const uint8_t*)video::g_page_off.pixels;
            const int32_t pitch = video::g_screen_res_x;
            pixels->resize((size_t)state.width * state.height);
            for (int32_t y = 0; y < state.height; ++y) {
                memcpy(pixels->data() + (size_t)y * state.width, page + (size_t)(state.top + y) * pitch + state.left, (size_t)state.width);
            }
        }

        void Copy_Rect_To_Page(const std::vector<uint8_t>& pixels) {
            uint8_t* page = (uint8_t*)video::g_page_off.pixels;
            const int32_t pitch = video::g_screen_res_x;
            for (int32_t y = 0; y < state.height; ++y) {
                memcpy(page + (size_t)(state.top + y) * pitch + state.left, pixels.data() + (size_t)y * state.width, (size_t)state.width);
            }
        }

        bool Rect_On_Page(int32_t left, int32_t top) {
            return left >= 0 && top >= 0 && left + state.width <= video::g_screen_res_x && top + state.height <= video::g_screen_res_y;
        }
    } // namespace

    void Video_Open(const char* file_name, int32_t width, int32_t height, int32_t frame_count) {
        Video_Close();
        if (!Enabled() || file_name == nullptr || width <= 0 || height <= 0 || frame_count <= 0) {
            return;
        }
        // hd/VIDEO/<file name without directory and extension, upper case>
        const char* base = file_name;
        for (const char* c = file_name; *c != '\0'; ++c) {
            if (*c == '/' || *c == '\\') {
                base = c + 1;
            }
        }
        std::string name;
        for (const char* c = base; *c != '\0' && *c != '.'; ++c) {
            name += (char)SDL_toupper((unsigned char)*c);
        }
        // art/ first, then its alias hd/ (PLAN.md section 5; the pack moved to art/ in WP2.4).
        std::string directory;
        std::vector<uint8_t> json;
        for (const char* root : {"art", "hd"}) {
            const std::string candidate = std::string(root) + "/VIDEO/" + name;
            if (Read_Vfs_File((candidate + "/video.json").c_str(), &json)) {
                directory = candidate;
                break;
            }
        }
        if (directory.empty()) {
            return;
        }
        yyjson_doc* document = yyjson_read((const char*)json.data(), json.size(), 0);
        yyjson_val* root = document != nullptr ? yyjson_doc_get_root(document) : nullptr;
        const int64_t meta_width = yyjson_get_int(yyjson_obj_get(root, "width"));
        const int64_t meta_height = yyjson_get_int(yyjson_obj_get(root, "height"));
        const int64_t meta_frames = yyjson_get_int(yyjson_obj_get(root, "frames"));
        const int64_t meta_scale = yyjson_get_int(yyjson_obj_get(root, "scale"));
        const int64_t hd_frames = yyjson_get_int(yyjson_obj_get(root, "hd_frames"));
        yyjson_doc_free(document);
        if (meta_width != width || meta_height != height || meta_frames != frame_count || meta_scale != SCALE ||
            (hd_frames != frame_count && hd_frames != 2 * (int64_t)frame_count)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "hd: %s/video.json does not match the video (%dx%d, %d frames)", directory.c_str(), width, height, frame_count);
            return;
        }
        state.directory = directory;
        state.width = width;
        state.height = height;
        state.ratio = (int32_t)(hd_frames / frame_count);
        state.hd_count = (int32_t)hd_frames;
        state.requested = 0;
        state.shown = -1;
        state.frames_hd = state.frames_sd = state.between_shown = state.between_skipped = 0;
        state.stop = false;
        state.active = true;
        static bool exit_registered;
        if (!exit_registered) {
            exit_registered = true;
            std::atexit(Video_Close); // quitting during a video: join the workers before static destruction
        }
        for (int32_t i = 0; i < WORKER_COUNT; ++i) {
            state.workers.emplace_back(Worker);
        }
        Request_Up_To(LOOKAHEAD);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: playing %s in HD (%d frames)", name.c_str(), state.hd_count);
    }

    void Video_Frame(int32_t left, int32_t top, int32_t width, int32_t height, int32_t frame) {
        // A frame shown without HD art: its 1x pixels, bound to their indices (a fill's twin).
        const auto plain = [&]() {
            Overwrite_Rect((const uint8_t*)video::g_page_off.pixels, left, top, left + width - 1, top + height - 1);
        };
        if (!state.active || !Rect_On_Page(left, top)) {
            plain();
            return;
        }
        const int32_t index = frame * state.ratio;
        Request_Up_To(index + LOOKAHEAD);
        // A short wait covers the first frames; later ones were decoded well ahead.
        const std::vector<uint8_t>* rgb = Take_Frame(index, SDL_GetTicksNS() + 40 * SDL_NS_PER_MS);
        state.left = left;
        state.top = top;
        if (rgb == nullptr) {
            state.shown = -1;
            state.frames_sd++;
            plain();
            return;
        }
        state.frames_hd++;
        Literal_Rect((const uint8_t*)video::g_page_off.pixels, left, top, state.width, state.height, rgb->data());
        state.shown = frame;
        if (state.ratio > 1) {
            Copy_Rect_From_Page(&state.shown_pixels);
        }
    }

    void Video_Between_Frames(uint64_t next_frame_ns, uint64_t frame_duration_ns) {
        if (!state.active || state.ratio < 2 || state.shown < 0 || frame_duration_ns == 0) {
            return;
        }
        const int32_t index = state.shown * state.ratio + 1;
        state.shown = -1;
        const uint64_t due = next_frame_ns - frame_duration_ns / 2;
        if (SDL_GetTicksNS() >= due || index >= state.hd_count) {
            state.between_skipped++; // running late
            return;
        }
        Request_Up_To(index + LOOKAHEAD);
        const std::vector<uint8_t>* rgb = Take_Frame(index, due);
        if (rgb == nullptr) {
            state.between_skipped++;
            return;
        }
        state.between_shown++;
        const uint64_t now = SDL_GetTicksNS();
        if (now < due) {
            SDL_DelayNS(due - now);
        }
        // The off page still shows the source frame (or was partly restored meanwhile): put its
        // pixels back so the in-between HD pixels belong to them, then publish just this frame.
        Copy_Rect_To_Page(state.shown_pixels);
        Literal_Rect((const uint8_t*)video::g_page_off.pixels, state.left, state.top, state.width, state.height, rgb->data());
        video::Publish_Off_Page();
    }

    void Video_Close() {
        if (!state.active) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.stop = true;
            state.jobs.clear();
        }
        state.work_ready.notify_all();
        for (std::thread& worker : state.workers) {
            worker.join();
        }
        state.workers.clear();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd: %s closed: %d frames in HD, %d without art; %d in-between frames shown, %d skipped", state.directory.c_str(), state.frames_hd, state.frames_sd, state.between_shown, state.between_skipped);
        state.decoded.clear();
        state.shown_pixels.clear();
        state.active = false;
    }

} // namespace render
