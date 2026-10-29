#pragma once

#include <atomic>
#include <cstdint>

// Counts how often a screen with fields polls for input (fields::Get_Input). A screen that polls
// is ready for clicks, so test scripts wait on it (`wait_input`) instead of guessing a delay.
// Header-only so targets that link fields.cpp need no build changes.
namespace antares::input_trace {
    inline std::atomic<uint64_t> g_polls{0};
    // Bumped when the polling screen's field count changes (a dialog opens or closes).
    inline std::atomic<uint64_t> g_layout{0};
    inline int32_t g_last_field_count = -1; // game thread only

    inline void Note_Poll(int32_t field_count) {
        if (field_count != g_last_field_count) {
            g_last_field_count = field_count;
            ++g_layout;
        }
        ++g_polls;
    }

    inline uint64_t Layout() {
        return g_layout.load();
    }

    inline uint64_t Polls() {
        return g_polls.load();
    }
} // namespace antares::input_trace
