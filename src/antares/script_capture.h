#pragma once

#include <cstdint>

// Screenshot helpers for the test input script (WP0.4). The script runs on the platform thread
// and captures from the present path, so both helpers are called there.
namespace antares::script_capture {
    /** Writes ARGB8888 pixels (SDL's in-memory B,G,R,A order) as an RGB PNG. */
    bool Save_Argb_Png(const char* path, const uint32_t* pixels, int32_t width, int32_t height, int32_t stride_pixels);
} // namespace antares::script_capture
