#include "pch.h"

#include "antares/render/art_format.h"
#include "antares/render/internal.h"

// WP3.3a: HD art handed to a frontend as plain images (render.h, Export_Art). The native galaxy
// in Godot draws the game's own star, nebula and planet art; this reads the decoded HD frames
// directly, so their soft edges keep their alpha (drawing them into an image plane would blend the
// edges with colour 0).

namespace render {

    bool Export_Art(const char* lbx_name, int32_t entry, const s_animation_header* loaded_entry, const uint32_t* palette_rgb, std::vector<s_exported_frame>* frames, uint32_t max_ms, bool shade_masks, bool* still_decoding, const uint32_t* glass_argb) {
        frames->clear();
        if (still_decoding != nullptr) {
            *still_decoding = false;
        }
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
                // The pack has the art but it isn't decoded yet (a slow disk: the owner plays from
                // a network share): the caller may ask again.
                if (still_decoding != nullptr && frame != nullptr && !Frame_Ready(frame)) {
                    *still_decoding = true;
                }
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
                    if (shade_masks && frame->mask != nullptr && frame->mask[i] != 0) {
                        const art::s_mask_meaning meaning = art::Mask_Meaning(frame->mask[i], false);
                        if (meaning.kind == art::MASK_SHADE) {
                            // The index's colour at the art's brightness relative to the frame's
                            // reference for that index (twin_draw.cpp, Shade_Ratio_Of).
                            const uint32_t base = palette_rgb[meaning.value];
                            const uint32_t luma = art::Luma(frame->rgb[i * 3], frame->rgb[i * 3 + 1], frame->rgb[i * 3 + 2]);
                            const uint32_t reference = frame->shade_luma != nullptr ? frame->shade_luma[meaning.value] : 0u;
                            const uint32_t ratio = reference == 0u ? 256u : SDL_clamp(luma * 256u / reference, 64u, 768u);
                            rgb = 0;
                            for (int32_t shift = 0; shift <= 16; shift += 8) {
                                rgb |= SDL_min(255u, ((base >> shift) & 0xFFu) * ratio / 256u) << shift;
                            }
                        }
                    }
                } else if (frame->index != nullptr) {
                    rgb = palette_rgb[frame->index[i]];
                }
                out.rgba[i * 4] = (uint8_t)(rgb >> 16);
                out.rgba[i * 4 + 1] = (uint8_t)(rgb >> 8);
                out.rgba[i * 4 + 2] = (uint8_t)rgb;
                uint8_t alpha = frame->alpha[i];
                const bool glass = asset->glassed &&
                    ((frame->mask != nullptr && art::Mask_Meaning(frame->mask[i], true).kind == art::MASK_GLASS) ||
                        (frame->rgb == nullptr && frame->index != nullptr && frame->index[i] >= 0xF0)); // indexed art
                if (glass && glass_argb != nullptr) {
                    // WP3.9: the tint and strength of its level, for the frontend to blend over
                    // what is under it (the shield hit).
                    int32_t level = 0;
                    if (frame->mask != nullptr && art::Mask_Meaning(frame->mask[i], true).kind == art::MASK_GLASS) {
                        level = frame->mask[i] - 240;
                    } else if (frame->index != nullptr) {
                        level = frame->index[i] - 0xF0;
                    }
                    const uint32_t c = glass_argb[SDL_clamp(level, 0, 15)];
                    out.rgba[i * 4] = (uint8_t)(c >> 16);
                    out.rgba[i * 4 + 1] = (uint8_t)(c >> 8);
                    out.rgba[i * 4 + 2] = (uint8_t)c;
                    alpha = (uint8_t)((uint32_t)alpha * (c >> 24) / 255u);
                } else if (glass) {
                    // Glass tints what is under it, which an exported picture can't: left out, as
                    // the colony screen's build box masks 240-255 out (owner, 2026-11-13: housing
                    // showed its shadow in solid magenta).
                    alpha = 0;
                }
                out.rgba[i * 4 + 3] = alpha;
            }
            asset->last_used = publish_count;
        }
        return true;
    }

} // namespace render
