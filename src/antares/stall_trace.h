#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

// Logs long gaps between published frames on the game thread, with what the thread did in the
// gap: file I/O through the VFS (time, calls, the slowest file), waiting for the presenter, and
// its own CPU time. Anything left over is sleeping (SDL_Delay) or waiting for input. Added
// 2026-11-02 for the owner's one combat where every ship's turn and every shot paused before it
// started, late in a long session. Header-only, like lbx_trace.h, so the VFS and video code
// share it without build changes. Game thread only: other threads' calls are ignored.
namespace antares::stall_trace {
    inline constexpr uint64_t k_stall_ns = 400 * SDL_NS_PER_MS;

    inline std::thread::id g_game_thread;
    inline bool g_have_game_thread = false;
    inline uint64_t g_last_publish_ns = 0;
    inline uint64_t g_last_cpu_ns = 0;
    inline uint64_t g_io_ns = 0;
    inline uint32_t g_io_calls = 0;
    inline uint64_t g_slowest_io_ns = 0;
    inline std::string g_slowest_io;
    inline std::string g_last_opened;

    inline bool On_Game_Thread_() {
        return g_have_game_thread && std::this_thread::get_id() == g_game_thread;
    }

    inline uint64_t Thread_Cpu_Ns_() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
        timespec now{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0) {
            return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
        }
#endif
        return 0;
    }

    /** Times one VFS call; `path` names the file for opens (nullptr: the file opened last). */
    struct s_io_scope {
        uint64_t start = 0;
        const char* path = nullptr;
        explicit s_io_scope(const char* opened = nullptr) {
            if (On_Game_Thread_()) {
                start = SDL_GetTicksNS();
                path = opened;
            }
        }
        ~s_io_scope() {
            if (start == 0) {
                return;
            }
            const uint64_t spent = SDL_GetTicksNS() - start;
            if (path != nullptr) {
                g_last_opened = path;
            }
            g_io_ns += spent;
            g_io_calls++;
            if (spent > g_slowest_io_ns) {
                g_slowest_io_ns = spent;
                g_slowest_io = g_last_opened;
            }
        }
    };

    /** Called by video::Publish_Off_Page; `wait_ns` is how long it waited for the presenter. */
    inline void Note_Publish(uint64_t start_ns, uint64_t wait_ns) {
        if (!g_have_game_thread) {
            g_game_thread = std::this_thread::get_id();
            g_have_game_thread = true;
        }
        const uint64_t cpu = Thread_Cpu_Ns_();
        if (g_last_publish_ns != 0 && start_ns - g_last_publish_ns >= k_stall_ns) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "stall: %.0f ms between frames: cpu %.0f ms, file I/O %.0f ms in %u calls (slowest %.0f ms, %s), presenter wait %.0f ms",
                (double)(start_ns - g_last_publish_ns) / 1e6,
                (double)(cpu - g_last_cpu_ns) / 1e6,
                (double)g_io_ns / 1e6,
                g_io_calls,
                (double)g_slowest_io_ns / 1e6,
                g_slowest_io.empty() ? "-" : g_slowest_io.c_str(),
                (double)wait_ns / 1e6);
        }
        g_last_publish_ns = SDL_GetTicksNS();
        g_last_cpu_ns = Thread_Cpu_Ns_();
        g_io_ns = 0;
        g_io_calls = 0;
        g_slowest_io_ns = 0;
        g_slowest_io.clear();
    }
} // namespace antares::stall_trace
