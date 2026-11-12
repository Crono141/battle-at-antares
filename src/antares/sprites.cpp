#include "pch.h"

#include "antares/sprites.h"

#include "antares/render/render.h"

#include <cstring>

namespace antares::sprites {
    namespace {
        // How long Export waits for HD frames to decode before it falls back to the 1x pixels.
        constexpr uint32_t k_decode_wait_ms = 3000;
        // Larger entries are refused (the biggest the galaxy uses is a 640x480 backdrop).
        constexpr int32_t k_max_pixels = 640 * 480;
        constexpr size_t k_bitmap_data_offset = sizeof(s_animation_header) + sizeof(uint32_t);

        // Reads an LBX entry into `bytes` (8-byte aligned storage) the way farload does, without
        // its error popups.
        bool Read_Entry(const std::string& lbx, int32_t entry, std::vector<uint64_t>* bytes, size_t* size) {
            s_vfs_file* file = vfs::fopen_lbx(lbx.c_str(), entry);
            if (file == nullptr) {
                return false;
            }
            const int32_t length = vfs::fsize(file);
            bool ok = length >= (int32_t)sizeof(s_animation_header);
            if (ok) {
                bytes->assign(((size_t)length + 7) / 8, 0);
                ok = vfs::fread(bytes->data(), (size_t)length, 1, file) == 1;
                *size = (size_t)length;
            }
            vfs::fclose(file);
            return ok;
        }

        // The original pixels of every frame, drawn by the game's own decoder into a scratch
        // bitmap (index 0 is transparent).
        void Export_1x(s_animation_header* anim, const uint32_t palette_rgb[256], s_sprite* out) {
            const int32_t width = anim->width;
            const int32_t height = anim->height;
            std::vector<uint64_t> storage((k_bitmap_data_offset + (size_t)width * height + 7) / 8 + 1, 0);
            auto* bitmap = reinterpret_cast<s_animation_header*>(storage.data());
            const int16_t saved_frame = anim->current_frame;
            const int32_t count = anim->frame_count > 0 ? anim->frame_count : 1;
            for (int32_t f = 0; f < count; ++f) {
                animate::Set_Animation_Frame(anim, f);
                bitmap::Draw_Picture_To_Bitmap(bitmap, anim);
                const uint8_t* pixels = reinterpret_cast<const uint8_t*>(bitmap) + k_bitmap_data_offset;
                s_frame frame;
                frame.width = width;
                frame.height = height;
                frame.rgba.resize((size_t)width * height * 4);
                for (size_t i = 0; i < (size_t)width * height; ++i) {
                    const uint32_t rgb = palette_rgb[pixels[i]];
                    frame.rgba[i * 4] = (uint8_t)(rgb >> 16);
                    frame.rgba[i * 4 + 1] = (uint8_t)(rgb >> 8);
                    frame.rgba[i * 4 + 2] = (uint8_t)rgb;
                    frame.rgba[i * 4 + 3] = pixels[i] != 0 ? 255 : 0;
                }
                out->frames.push_back(std::move(frame));
            }
            anim->current_frame = saved_frame;
            // The scratch bitmap got a twin plane when it was created: drop it with the memory.
            render::Forget_Images(storage.data(), storage.data() + storage.size());
            out->scale = 1;
            out->hd = false;
        }
    } // namespace

    namespace {
        // A FONTS.LBX palette (entry N + 1: 256 s_palette_entry {changed, r, g, b}, 6-bit), as
        // fonts::Load_Palette copies it.
        bool Game_Palette(int32_t number, uint32_t palette_rgb[256]) {
            std::vector<uint64_t> storage;
            size_t size = 0;
            if (number < 0 || !Read_Entry("FONTS.LBX", number + 1, &storage, &size) || size < 256 * sizeof(s_palette_entry)) {
                return false;
            }
            const auto* entries = reinterpret_cast<const s_palette_entry*>(storage.data());
            for (int32_t i = 0; i < 256; ++i) {
                const s_palette_entry& c = entries[i];
                palette_rgb[i] = ((uint32_t)((c.r & 0x3F) << 2) << 16) | ((uint32_t)((c.g & 0x3F) << 2) << 8) | (uint32_t)((c.b & 0x3F) << 2);
            }
            return true;
        }

        // The palette an art pack's sprite was made under, when the pack says (WP3.3f): views
        // outside the galaxy show the colony screen's icons and the like, whose indexed HD pixels
        // need that screen's palette, not the galaxy's. False: use the screen's palette.
        // Where the pack doesn't say ("inferred"): the palette of the screen that draws the entry.
        // The colony screen loads FONTS.LBX palette 1 and its planet backdrop's palette
        // (COLONY::Colony_Screen, colony.cpp:211; PLANETS.LBX); the leaders screen palette 8
        // (officer.cpp:790), except the colony screen's governor portraits (0xD2 on,
        // OFFICER::Load_Officer_Picture picture type 1).
        struct s_home_palette {
            const char* lbx;
            int32_t first;
            int32_t last;
            int32_t palette;
            bool planets; // the colony backdrop's palette over it
        };
        constexpr s_home_palette k_home_palettes[] = {
            {"OFFICER.LBX", 0xD2, 0x114, 1, true},
            {"OFFICER.LBX", 0, 0x7FFF, 8, false},
            {"COLONY.LBX", 0, 0x7FFF, 1, true},
            {"COLONY2.LBX", 0, 0x7FFF, 1, true},
            {"COLSYSDI.LBX", 0, 0x7FFF, 1, true},
            {"COLPUPS.LBX", 0, 0x7FFF, 1, true},
            {"RACEICON.LBX", 0, 0x7FFF, 1, true},
        };

        bool Home_Palette(const std::string& lbx, int32_t entry, std::vector<render::s_palette_layer>* layers) {
            for (const s_home_palette& home : k_home_palettes) {
                if (lbx == home.lbx && entry >= home.first && entry <= home.last) {
                    render::s_palette_layer base;
                    base.palette = home.palette;
                    layers->assign(1, base);
                    if (home.planets) {
                        render::s_palette_layer backdrop;
                        backdrop.lbx = "PLANETS.LBX";
                        backdrop.entry = 0;
                        layers->push_back(backdrop);
                    }
                    return true;
                }
            }
            return false;
        }

        bool Declared_Palette_Rgb(const std::string& lbx, int32_t entry, uint32_t palette_rgb[256]) {
            std::vector<render::s_palette_layer> layers;
            if (!render::Declared_Palette(lbx.c_str(), entry, &layers) && !Home_Palette(lbx, entry, &layers)) {
                return false;
            }
            for (const render::s_palette_layer& layer : layers) {
                if (layer.palette >= 0) {
                    if (!Game_Palette(layer.palette, palette_rgb)) {
                        return false;
                    }
                    continue;
                }
                std::vector<uint64_t> storage;
                size_t size = 0;
                if (!Read_Entry(layer.lbx, layer.entry, &storage, &size)) {
                    return false;
                }
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(storage.data()), size, palette_rgb);
            }
            return true;
        }
    } // namespace

    void Palette_For(const uint8_t* entry_bytes, size_t size, uint32_t palette_rgb[256]) {
        for (int32_t i = 0; i < 256; ++i) {
            const s_palette_entry& c = video::current_palette[i];
            palette_rgb[i] = ((uint32_t)(c.r << 2) << 16) | ((uint32_t)(c.g << 2) << 8) | (uint32_t)(c.b << 2);
        }
        Apply_Embedded_Palette(entry_bytes, size, palette_rgb);
    }

    void Apply_Embedded_Palette(const uint8_t* entry_bytes, size_t size, uint32_t palette_rgb[256]) {
        if (entry_bytes == nullptr || size < sizeof(s_animation_header)) {
            return;
        }
        auto* anim = reinterpret_cast<s_animation_header*>(const_cast<uint8_t*>(entry_bytes));
        if ((anim->flags & ANIMATION_FLAG_HAS_PALETTE) == 0 || anim->frame_count < 0) {
            return;
        }
        const size_t header_at = sizeof(s_animation_header) + ((size_t)anim->frame_count + 1) * sizeof(uint32_t);
        if (header_at + sizeof(s_palette_header) > size) {
            return;
        }
        s_palette_header header;
        memcpy(&header, entry_bytes + header_at, sizeof(header));
        const uint8_t* entries = entry_bytes + header_at + sizeof(s_palette_header);
        for (int32_t i = 0; i < header.count; ++i) {
            const int32_t index = header.start_index + i;
            if (index < 0 || index >= 256 || header_at + sizeof(s_palette_header) + (size_t)(i + 1) * 4 > size) {
                break;
            }
            const uint8_t* c = entries + (size_t)i * 4; // {changed, r, g, b}, 6-bit
            palette_rgb[index] = ((uint32_t)(c[1] << 2) << 16) | ((uint32_t)(c[2] << 2) << 8) | (uint32_t)(c[3] << 2);
        }
    }

    bool Export(const std::string& lbx, int32_t entry, s_sprite* out) {
        *out = s_sprite{};
        out->lbx = lbx;
        out->entry = entry;
        std::vector<uint64_t> storage;
        size_t size = 0;
        if (!Read_Entry(lbx, entry, &storage, &size)) {
            out->error = "no such entry";
            return false;
        }
        auto* bytes = reinterpret_cast<uint8_t*>(storage.data());
        auto* anim = reinterpret_cast<s_animation_header*>(bytes);
        const int32_t mode = anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK;
        if (anim->width <= 0 || anim->height <= 0 || (int32_t)anim->width * anim->height > k_max_pixels ||
            anim->frame_count < 0 || anim->frame_count > 256 ||
            (mode != ANIMATION_DRAW_MODE_ANIMATED_SPRITE && mode != ANIMATION_DRAW_MODE_BITMAP)) {
            out->error = "not a picture the frontend can use";
            return false;
        }
        out->width = anim->width;
        out->height = anim->height;
        uint32_t palette_rgb[256];
        Palette_For(bytes, size, palette_rgb);
        if (lbx == "BUFFER0.LBX" || lbx == "STARBG.LBX") {
            // Galaxy art: the galaxy's palette (FONTS.LBX palette 0, all of it, as
            // Fade_Into_Main_Screen loads it) under the entry's own, not whatever is loaded now:
            // exported after a legacy screen, the upper entries were still that screen's
            // (owner, 2026-11-12: nebulae came back from Colonies with green edges).
            if (Game_Palette(0, palette_rgb)) {
                Apply_Embedded_Palette(bytes, size, palette_rgb);
            }
        } else {
            // Other art its own, if declared.
            Declared_Palette_Rgb(lbx, entry, palette_rgb);
        }

        render::Register_Load(bytes, (int32_t)size, lbx.c_str(), entry);
        std::vector<render::s_exported_frame> art;
        if (render::Export_Art(lbx.c_str(), entry, anim, palette_rgb, &art, k_decode_wait_ms)) {
            for (render::s_exported_frame& frame : art) {
                s_frame copy;
                copy.width = frame.width;
                copy.height = frame.height;
                copy.origin_x = frame.origin_x;
                copy.origin_y = frame.origin_y;
                copy.rgba = std::move(frame.rgba);
                out->frames.push_back(std::move(copy));
            }
            out->scale = render::SCALE;
            out->hd = true;
        } else {
            Export_1x(anim, palette_rgb, out);
        }
        render::Forget_Loads(bytes, bytes + size);
        return true;
    }

} // namespace antares::sprites
