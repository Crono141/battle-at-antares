#include "pch.h"

// The farload/buffer tests do not exercise HD sprite composition.
namespace render {
    void Register_Load(const void*, int32_t, const char*, int32_t) {
    }
    void Forget_Loads(const void*, const void*) {
    }
    void Move_Loads(const void*, const void*, ptrdiff_t) {
    }
} // namespace render
