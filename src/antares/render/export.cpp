#include "pch.h"

#include "antares/render/internal.h"

// WP3.3a: HD art handed to a frontend as plain images (render.h, Export_Art). The native galaxy
// in Godot draws the game's own star, nebula and planet art; this reads the decoded HD frames
// directly, so their soft edges keep their alpha (drawing them into an image plane would blend the
// edges with colour 0).

namespace render {

    bool Export_Art(const char* lbx_name, int32_t entry, const s_animation_header* loaded_entry, const uint32_t* palette_rgb, std::vector<s_exported_frame>* frames, uint32_t max_ms) {
        frames->clear();
        if (!enabled || loaded_entry == nullptr) {
            return false;
        }
        const int32_t index = Find_Or_Create_Asset(lbx_name, entry);
        if (index < 0) {
            return false;
        }
        s_hd_asset* asset = assets[index];
        if (!asset->loaded) {
            Load_Asset(asset, reinterpret_cast<const uint8_t*>(loaded_entry));
        }
        const int32_t count = loaded_entry->frame_count > 0 ? loaded_entry->frame_count : 1;
        if (asset->frames == nullptr || asset->frame_count <= 0) {
            return false;
        }
        // Decode every frame now, ahead of whatever else is queued, and wait for them.
        Begin_Preload();
        Preload_Asset(lbx_name, entry, asset->frame_count);
        End_Preload(max_ms);

        frames->resize((size_t)count);
        for (int32_t f = 0; f < count; ++f) {
            const s_hd_frame* frame = Resolve_Frame(asset, f < asset->frame_count ? f : asset->frame_count - 1);
            if (frame == nullptr || !Frame_Ready(frame) || frame->alpha == nullptr) {
                frames->clear();
                return false;
            }
            s_exported_frame& out = (*frames)[(size_t)f];
            out.width = frame->width;
            out.height = frame->height;
            out.origin_x = frame->origin_x;
            out.origin_y = frame->origin_y;
            const size_t pixels = (size_t)frame->width * (size_t)frame->height;
            out.rgba.resize(pixels * 4);
            for (size_t i = 0; i < pixels; ++i) {
                uint32_t rgb = 0;
                if (frame->rgb != nullptr) {
                    rgb = ((uint32_t)frame->rgb[i * 3] << 16) | ((uint32_t)frame->rgb[i * 3 + 1] << 8) | frame->rgb[i * 3 + 2];
                } else if (frame->index != nullptr) {
                    rgb = palette_rgb[frame->index[i]];
                }
                out.rgba[i * 4] = (uint8_t)(rgb >> 16);
                out.rgba[i * 4 + 1] = (uint8_t)(rgb >> 8);
                out.rgba[i * 4 + 2] = (uint8_t)rgb;
                out.rgba[i * 4 + 3] = frame->alpha[i];
            }
            asset->last_used = publish_count;
        }
        return true;
    }

} // namespace render
