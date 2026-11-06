#include "pch.h"

#include "antares/native.h"

// The request calls the generic dialogs make (WP3.2b) for tests that link those dialogs without
// the native bridge: no frontend, so every dialog keeps its legacy path.
namespace antares::native {
    bool Message(const char*, const char*, uint16_t) {
        return false;
    }
    bool Confirm(const char*, int16_t*) {
        return false;
    }
    bool Warning(const char*) {
        return false;
    }
    bool Choose(const char*, const char*, int32_t, const uint8_t*, int8_t*, int16_t*) {
        return false;
    }
} // namespace antares::native
