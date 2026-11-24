#include "pch.h"
#include "antares/combat_native.h"

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
        void Export_1x(s_animation_header* anim, const uint32_t palette_rgb[256], s_sprite* out, const uint32_t* glass_argb = nullptr) {
            const int32_t width = anim->width;
            const int32_t height = anim->height;
            std::vector<uint64_t> storage((k_bitmap_data_offset + (size_t)width * height + 7) / 8 + 1, 0);
            auto* bitmap = reinterpret_cast<s_animation_header*>(storage.data());
            const int16_t saved_frame = anim->current_frame;
            const int32_t count = anim->frame_count > 0 ? anim->frame_count : 1;
            // Glass (indices 0xF0-0xFF, remapped against what lies under them when drawn): while
            // drawing here each level maps to itself, so its pixels stay recognisable and come out
            // as the level's colour over transparency (owner, 2026-11-19: the colony landing's
            // beams were solid magenta, the remap's colour over the bitmap's black).
            const bool glassed = (anim->flags & ANIMATION_FLAG_GLASSED) != 0 || glass_argb != nullptr; // a glass table: drawn glassed
            static uint8_t saved_remap[16][256];
            if (glassed) {
                memcpy(saved_remap, video::glass_remap_colors, sizeof(saved_remap));
                for (int32_t g = 0; g < 16; ++g) {
                    memset(video::glass_remap_colors[g], 0xF0 + g, 256);
                }
            }
            // A full-store animation's frames are whole pictures, each drawn alone on the restored
            // background (the colony landing's pods). Draw_Picture_To_Bitmap replays every frame
            // from the last keyframe, and with no keyframe interval that is all of them: the pods
            // smeared down the screen (owner, 2026-11-19). Every frame its own keyframe here.
            const bool whole_frames = (anim->flags & ANIMATION_FLAG_FULL_STORE) != 0 && count > 1;
            const auto saved_interval = anim->keyframe_interval;
            if (whole_frames) {
                anim->keyframe_interval = 1;
            }
            for (int32_t f = 0; f < count; ++f) {
                animate::Set_Animation_Frame(anim, f);
                bitmap::Draw_Picture_To_Bitmap(bitmap, anim);
                const uint8_t* pixels = reinterpret_cast<const uint8_t*>(bitmap) + k_bitmap_data_offset;
                s_frame frame;
                frame.width = width;
                frame.height = height;
                frame.rgba.resize((size_t)width * height * 4);
                for (size_t i = 0; i < (size_t)width * height; ++i) {
                    if (glassed && pixels[i] >= 0xF0 && glass_argb != nullptr) {
                        const uint32_t c = glass_argb[pixels[i] - 0xF0];
                        frame.rgba[i * 4] = (uint8_t)(c >> 16);
                        frame.rgba[i * 4 + 1] = (uint8_t)(c >> 8);
                        frame.rgba[i * 4 + 2] = (uint8_t)c;
                        frame.rgba[i * 4 + 3] = (uint8_t)(c >> 24);
                        continue;
                    }
                    if (glassed && pixels[i] >= 0xF0) {
                        const auto& glass = video::glass_colors[pixels[i] - 0xF0];
                        frame.rgba[i * 4] = (uint8_t)(glass.r << 2);
                        frame.rgba[i * 4 + 1] = (uint8_t)(glass.g << 2);
                        frame.rgba[i * 4 + 2] = (uint8_t)(glass.b << 2);
                        frame.rgba[i * 4 + 3] = (uint8_t)(glass.intensity >= 100 ? 255 : glass.intensity * 255 / 100);
                        continue;
                    }
                    const uint32_t rgb = palette_rgb[pixels[i]];
                    frame.rgba[i * 4] = (uint8_t)(rgb >> 16);
                    frame.rgba[i * 4 + 1] = (uint8_t)(rgb >> 8);
                    frame.rgba[i * 4 + 2] = (uint8_t)rgb;
                    frame.rgba[i * 4 + 3] = pixels[i] != 0 ? 255 : 0;
                }
                out->frames.push_back(std::move(frame));
            }
            if (glassed) {
                memcpy(video::glass_remap_colors, saved_remap, sizeof(saved_remap));
            }
            anim->keyframe_interval = saved_interval;
            anim->current_frame = saved_frame;
            // The scratch bitmap got a twin plane when it was created: drop it with the memory.
            render::Forget_Images(storage.data(), storage.data() + storage.size());
            out->scale = 1;
            out->hd = false;
        }

        // An RGBA image `factor` times smaller, each pixel the average of its block (colour
        // weighted by alpha, so transparent pixels don't darken the edges).
        void Shrink(const std::vector<uint8_t>& rgba, int32_t width, int32_t height, int32_t factor, s_frame* out) {
            out->width = width / factor;
            out->height = height / factor;
            out->rgba.assign((size_t)out->width * out->height * 4, 0);
            for (int32_t y = 0; y < out->height; ++y) {
                for (int32_t x = 0; x < out->width; ++x) {
                    uint32_t r = 0, g = 0, b = 0, a = 0;
                    for (int32_t dy = 0; dy < factor; ++dy) {
                        const uint8_t* p = rgba.data() + ((size_t)(y * factor + dy) * width + (size_t)x * factor) * 4;
                        for (int32_t dx = 0; dx < factor; ++dx, p += 4) {
                            r += p[0] * p[3];
                            g += p[1] * p[3];
                            b += p[2] * p[3];
                            a += p[3];
                        }
                    }
                    uint8_t* o = out->rgba.data() + ((size_t)y * out->width + x) * 4;
                    if (a > 0) {
                        o[0] = (uint8_t)(r / a);
                        o[1] = (uint8_t)(g / a);
                        o[2] = (uint8_t)(b / a);
                    }
                    o[3] = (uint8_t)(a / (uint32_t)(factor * factor));
                }
            }
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

    bool Export(const std::string& requested, int32_t entry, s_sprite* out) {
        if (requested == "capture") {
            return antares::combat::Export_Capture(entry, out); // spike 3.9: a captured beam volley
        }
        *out = s_sprite{};
        out->lbx = requested;
        out->entry = entry;
        // "LBX@N": the entry under the palette that entry N of the same file holds (a monster's
        // ship picture under its own palette, KEN::Load_Player_Ship_Palette's 413-419).
        std::string lbx = requested;
        // "LBX~B=RRGGBB.RRGGBB...": palette colours from index B on replaced (8-bit hex), and
        // the art's masked pixels follow them (WP3.9: a beam's muzzle flash and hit flare in its
        // weapon's colours, which the game puts in palette slots 144-175 as it fires).
        std::vector<std::pair<int32_t, uint32_t>> overrides;
        if (const size_t tilde = lbx.find('~'); tilde != std::string::npos) {
            const char* p = lbx.c_str() + tilde + 1;
            char* end = nullptr;
            int32_t index = (int32_t)strtol(p, &end, 10);
            if (end != nullptr && *end == '=') {
                p = end + 1;
                while (*p != 0 && index < 256) {
                    const uint32_t rgb = (uint32_t)strtoul(p, &end, 16);
                    if (end == p) {
                        break;
                    }
                    overrides.emplace_back(index++, rgb & 0xFFFFFF);
                    p = *end == '.' ? end + 1 : end;
                }
            }
            lbx.erase(tilde);
        }
        // "LBX!shield" or "LBX!glass=NAME": glass pixels kept as one of the combat's glass tables
        // (COMBINIT, combinit.cpp:745-860: level L tints toward a colour by a strength), as colour
        // and alpha for the frontend to blend: shield (also the stasis bubble), pulsar, warp,
        // tractor, bhg, web, stellar.
        uint32_t glass_argb[16] = {};
        bool glass = false;
        {
            std::string table;
            if (const size_t bang = lbx.find("!shield"); bang != std::string::npos) {
                table = "shield";
                lbx.erase(bang, 7);
            } else if (const size_t g = lbx.find("!glass="); g != std::string::npos) {
                size_t end = g + 7;
                while (end < lbx.size() && lbx[end] != '~' && lbx[end] != ':' && lbx[end] != '@' && lbx[end] != '!') {
                    ++end;
                }
                table = lbx.substr(g + 7, end - g - 7);
                lbx.erase(g, end - g);
            }
            if (!table.empty()) {
                glass = true;
                // A palette as the table was built under: the current one, under an entry's own.
                auto palette_of = [](const char* source_lbx, int32_t source_entry, uint32_t out[256]) {
                    std::vector<uint64_t> bytes;
                    size_t size = 0;
                    if (source_lbx != nullptr && Read_Entry(source_lbx, source_entry, &bytes, &size)) {
                        Palette_For(reinterpret_cast<const uint8_t*>(bytes.data()), size, out);
                    } else {
                        for (int i = 0; i < 256; ++i) {
                            const auto& c = video::current_palette[i];
                            out[i] = ((uint32_t)(c.r & 0x3F) << 18) | ((uint32_t)(c.g & 0x3F) << 10) | ((uint32_t)(c.b & 0x3F) << 2);
                        }
                    }
                };
                uint32_t pal[256] = {};
                if (table == "pulsar") {
                    palette_of("CMBTSFX.LBX", 52, pal);
                } else if (table == "warp") {
                    palette_of("SPHERSFX.LBX", 0, pal);
                } else if (table == "bhg") {
                    palette_of("CMBTSFX.LBX", 46, pal);
                } else if (table == "stellar") {
                    palette_of("CMBTSFX.LBX", 40, pal);
                } else {
                    palette_of(nullptr, 0, pal);
                }
                for (int32_t level = 1; level < 16; ++level) {
                    uint32_t rgb = 0;
                    int32_t pct = 20 + 3 * level;
                    if (table == "pulsar" || table == "warp") {
                        rgb = pal[144 + level];
                        pct = 50 + 3 * level;
                    } else if (table == "tractor") {
                        rgb = pal[80 + level / 2];
                    } else if (table == "bhg") {
                        rgb = pal[144 + level];
                        pct = 60 + 2 * level;
                    } else if (table == "web") {
                        rgb = pal[176 + level];
                        pct = SDL_min((3 * level + 15) * 2, 100);
                    } else if (table == "stellar") {
                        rgb = pal[SDL_min(144 + 2 * level, 255)];
                        pct = 50 + 3 * level;
                    } else { // shield, stasis
                        rgb = ((uint32_t)(2 * level + 13) * 255 / 63) << 16 | ((uint32_t)(2 * level + 30) * 255 / 63) << 8 | 255u;
                    }
                    const uint32_t a = (uint32_t)SDL_clamp(pct, 0, 100) * 255 / 100;
                    glass_argb[level] = (a << 24) | (rgb & 0xFFFFFF);
                }
            }
        }
        // "LBX:N": HD art at most N times the 1x size (the scene windows' full-screen
        // animations: 30-odd frames at 4x are 600 MB and more as textures).
        int32_t max_scale = 0;
        if (const size_t colon = lbx.find(':'); colon != std::string::npos) {
            max_scale = (int32_t)strtol(lbx.c_str() + colon + 1, nullptr, 10);
            lbx.erase(colon);
        }
        const std::string requested_lbx = lbx;
        int32_t palette_entry = -1;
        if (const size_t at = requested_lbx.find('@'); at != std::string::npos) {
            lbx = requested_lbx.substr(0, at);
            palette_entry = (int32_t)strtol(requested_lbx.c_str() + at + 1, nullptr, 10);
        }
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
        } else if (lbx == "CMBTPLNT.LBX" && !Declared_Palette_Rgb(lbx, entry, palette_rgb) && Game_Palette(0, palette_rgb)) {
            // A planet's picture (the orbit's invade/bomb window, MAINPUPS::
            // Load_Combat_Colony_Picture_To_Screen_Seg): the galaxy's palette under its climate's,
            // entry climate * 6 + 5, which the window loads with it.
            std::vector<uint64_t> climate_palette;
            size_t climate_size = 0;
            if (Read_Entry(lbx, entry / 6 * 6 + 5, &climate_palette, &climate_size)) {
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(climate_palette.data()), climate_size, palette_rgb);
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if (lbx == "APP_PICS.LBX") {
            // An application's picture (the Info panel's Tech Review): the Info screen's palette
            // (INFO.LBX 1, loaded by Info_Screen; the pack declares it) under the picture's own
            // part (colours 192-255), as _Tech_Review_Subscreen draws it. Without its own part the
            // masked pixels came out in the Info screen's colours.
            if (!Declared_Palette_Rgb(lbx, entry, palette_rgb)) {
                std::vector<uint64_t> info_palette;
                size_t info_size = 0;
                if (Read_Entry("INFO.LBX", 1, &info_palette, &info_size)) {
                    Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(info_palette.data()), info_size, palette_rgb);
                }
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if (lbx == "PLANETS.LBX" || lbx == "COLONY.LBX" || lbx == "COLONY2.LBX") {
            // The colony screen's art (WP3.11a, the colony landing): its palette, FONTS.LBX palette
            // 1 with the planet backdrop's colours (the pack's declaration, or the home palette),
            // then the entry's own. Under the galaxy's palette the planet came out black.
            if (!Declared_Palette_Rgb(lbx, entry, palette_rgb)) {
                Game_Palette(1, palette_rgb);
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if (lbx.rfind("BLDG", 0) == 0 || lbx == "COLROADS.LBX" || lbx == "COLVEGGI.LBX") {
            // The colony's landscape (owner, 2026-11-20: VIEW COLONY natively): the colony screen's
            // palette as Update_Colony_Palette sets it, FONTS.LBX palette 1 under the planet
            // picture's colours ("@N": PLANETS.LBX entry N, the colony's backdrop), then the
            // entry's own.
            if (!Declared_Palette_Rgb(lbx, entry, palette_rgb)) {
                Game_Palette(1, palette_rgb);
            }
            if (palette_entry >= 0) {
                std::vector<uint64_t> planet_palette;
                size_t planet_size = 0;
                if (Read_Entry("PLANETS.LBX", palette_entry, &planet_palette, &planet_size)) {
                    Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(planet_palette.data()), planet_size, palette_rgb);
                }
                palette_entry = -1; // not an entry of this file
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if (lbx == "RACESEL.LBX") {
            // The race select screen's pictures (the Info panel's portrait): the declared palette,
            // then the entry's own colours over it, as animate::Draw applies them (without them the
            // portrait's 128-255 came out black).
            Declared_Palette_Rgb(lbx, entry, palette_rgb);
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if (lbx == "DIPLOMAT.LBX" && entry >= 13 && palette_entry < 0 && !Declared_Palette_Rgb(lbx, entry, palette_rgb)) {
            // A diplomacy room (race * 2 + 13) or its ambassador (+ 14): the race's palette, the full
            // 256 colours of entry `race`, which Diplomacy_Mouse_Init loads (Draw_Palette of the
            // cursor picture); neither carries colours of its own. Without it the 1x frames came out
            // grey with a speckled ambassador (0.5.0 without the HD packs, Reddit report).
            palette_entry = (entry - 13) / 2;
        } else if (lbx == "EVENTS.LBX" && entry >= 1 && palette_entry < 0 && !Declared_Palette_Rgb(lbx, entry, palette_rgb)) {
            // The GNN room (entry 1) and its story pictures: EVENTS.LBX 0's full palette, which
            // Event_Fade_In loads, then the picture's own colours (184-247) as it is drawn.
            std::vector<uint64_t> gnn_palette;
            size_t gnn_size = 0;
            if (Read_Entry("EVENTS.LBX", 0, &gnn_palette, &gnn_size)) {
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(gnn_palette.data()), gnn_size, palette_rgb);
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else if ((lbx == "SCIENCE.LBX" || lbx.rfind("TANM_", 0) == 0) && !Declared_Palette_Rgb(lbx, entry, palette_rgb) &&
            Game_Palette(0, palette_rgb)) {
            // The research room's monitor, tube light and hologram: the galaxy's palette (Science_Room
            // loads none) under SCIENCE.LBX 0's colours 192-255 (its Draw_Palette), then the
            // picture's own (a hologram's 149-191, applied as it is drawn). Without them the monitor
            // and the tube came out as green noise (same report).
            std::vector<uint64_t> science_palette;
            size_t science_size = 0;
            if (Read_Entry("SCIENCE.LBX", 0, &science_palette, &science_size)) {
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(science_palette.data()), science_size, palette_rgb);
            }
            Apply_Embedded_Palette(bytes, size, palette_rgb);
        } else {
            // Other art its own, if declared.
            Declared_Palette_Rgb(lbx, entry, palette_rgb);
            // A combat effect with its own palette (the stasis beam, lightning, the retreat portal:
            // Draw_Palette before drawing) is drawn under it.
            std::string upper_lbx = lbx;
            for (char& ch : upper_lbx) {
                ch = (char)toupper((unsigned char)ch);
            }
            if (upper_lbx == "CMBTSFX.LBX" || upper_lbx == "SPHERSFX.LBX") {
                Apply_Embedded_Palette(bytes, size, palette_rgb);
            }
        }
        if (palette_entry >= 0) {
            std::vector<uint64_t> own_palette;
            size_t own_size = 0;
            if (Read_Entry(lbx, palette_entry, &own_palette, &own_size)) {
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(own_palette.data()), own_size, palette_rgb);
            }
        } else if (lbx == "SHIPS.LBX" && entry >= 0 && entry < 400 && entry % 50 != 49) {
            // A ship's player markings (0xC0-0xDF) take its colour set's palette, entry
            // colour * 50 + 49, as KEN::Load_Player_Ship_Palette loads it for the design screen
            // (owner, 2026-11-13: every player's ships came out red).
            std::vector<uint64_t> set_palette;
            size_t set_size = 0;
            if (Read_Entry(lbx, entry / 50 * 50 + 49, &set_palette, &set_size)) {
                Apply_Embedded_Palette(reinterpret_cast<const uint8_t*>(set_palette.data()), set_size, palette_rgb);
            }
        }

        for (const auto& [index, rgb] : overrides) {
            palette_rgb[index] = rgb;
        }

        render::Register_Load(bytes, (int32_t)size, lbx.c_str(), entry);
        std::vector<render::s_exported_frame> art;
        // Ships' markings follow their colour set's palette (the masks say which pixels).
        // Combat effects (CMBTSFX, SPHERSFX) too: their masked pixels take the palette the effect is
        // drawn under (its own, over the combat's), as the game draws them; under the pack's
        // palette the stasis beam, compressor, lightning and portal came out nearly black.
        std::string upper = lbx;
        for (char& ch : upper) {
            ch = (char)toupper((unsigned char)ch);
        }
        // Combat ships too (owner 2026-11-23: the player's blue ships came out red in a native
        // battle, the pack's art colour): each colour set's pixels are their own palette indices,
        // so the masked markings take the set's colours.
        const bool shade_masks = upper == "SHIPS.LBX" || upper == "CMBTSHP.LBX" || upper == "CMBTSFX.LBX" || upper == "SPHERSFX.LBX" ||
            !overrides.empty();
        bool still_decoding = false;
        // "LBX:1": the original pixels, not the pack's art (the colony landing's pods, whose HD
        // frames have the smear baked in).
        if (max_scale != 1 && render::Export_Art(lbx.c_str(), entry, anim, palette_rgb, &art, k_decode_wait_ms, shade_masks, &still_decoding, glass ? glass_argb : nullptr)) {
            const int32_t shrink = max_scale > 0 && max_scale < render::SCALE ? render::SCALE / max_scale : 1;
            for (render::s_exported_frame& frame : art) {
                s_frame copy;
                copy.origin_x = frame.origin_x;
                copy.origin_y = frame.origin_y;
                if (shrink > 1) {
                    Shrink(frame.rgba, frame.width, frame.height, shrink, &copy);
                    std::vector<uint8_t>().swap(frame.rgba); // freed as it goes
                } else {
                    copy.width = frame.width;
                    copy.height = frame.height;
                    copy.rgba = std::move(frame.rgba);
                }
                out->frames.push_back(std::move(copy));
            }
            out->scale = render::SCALE / shrink;
            out->hd = true;
        } else {
            Export_1x(anim, palette_rgb, out, glass ? glass_argb : nullptr);
            out->hd_pending = still_decoding;
        }
        render::Forget_Loads(bytes, bytes + size);
        return true;
    }

} // namespace antares::sprites
