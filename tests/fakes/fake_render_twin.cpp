#include "pch.h"

#include "antares/render/render.h"

// Strict test targets that compile drawing code without the HD renderer: no twin planes.
namespace render {
    bool enabled = false;

    namespace twin {
        void Scope::Open(const char*, const void*, int32_t, int32_t, int32_t, int32_t) {
        }
        void Scope::Open_Sprite(const char*, const void*, int32_t, int32_t, const s_animation_header*) {
        }
        void Scope::Close() {
        }
    } // namespace twin
} // namespace render
