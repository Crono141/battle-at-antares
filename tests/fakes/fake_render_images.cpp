#include "pch.h"

// The allocator tells the HD layer when it rewinds (image planes past the new cursor go).
namespace render {
    void Forget_Images(const void*, const void*) {
    }
} // namespace render
