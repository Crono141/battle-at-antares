#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include <algorithm>

// Attempt 2's draw model: hooked blitters stamp the page pixels they write, and the HD art of
// the current draw is rendered over them when the draw ends; unhooked writes are settled to the
// original pixel enlarged. WP1.3a replaces this with twins.
namespace render {

#define HD_GLASS_FIRST 0xF0

    namespace {

        void Settle_Draw_Area(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            if (page_off.hd.empty()) {
                return;
            }
            (void)Settle(page_off, video::g_page_off.pixels, x0, y0, x1, y1);
            (void)Settle(page_back, video::g_page_back.pixels, x0, y0, x1, y1);
        }

        // ------------------------------------------------------------- current draw

        enum e_source {
            SOURCE_NONE,  // no HD art: written pixels show the original enlarged
            SOURCE_ART,   // a sprite, glyph or file animation frame with HD art
            SOURCE_IMAGE, // an off-screen image with an HD surface
        };

        struct s_draw {
            e_source source = SOURCE_NONE;
            const s_hd_frame* frame = nullptr;
            bool glyph = false;
            bool glassed = false;
            bool redrawn = false;
            int32_t x = 0; // where the sprite's top-left lands in the target
            int32_t y = 0;
            bool remapped = false;
            uint8_t remap[256];
            uint8_t colors[8];
            std::vector<uint8_t> shade; // brightness percent per sprite pixel, empty = none
            int32_t shade_width = 0;
            int32_t shade_height = 0;
            const s_surface* image = nullptr;
            int32_t image_x = 0;
            int32_t image_y = 0;
            std::string name; // for the probe
            // The frame has art that is still being decoded: written pixels show the original
            // for now and are recorded so they can be upgraded when it arrives.
            bool waiting = false;
            // Clip window at the start of the draw (the fringe must stay inside it).
            int32_t clip_x1 = 0;
            int32_t clip_y1 = 0;
            int32_t clip_x2 = 0x7FFF;
            int32_t clip_y2 = 0x7FFF;
        };

        struct s_stamp {
            s_surface* target;
            int32_t offset;
            uint8_t value;
            int8_t level; // glass level, -1 for a plain pixel
        };

        s_draw draw;
        uint32_t generation = 1;
        // Art draws are rendered when the draw ends, once every written pixel is known.
        std::vector<s_stamp> pending;

        // Page draws that showed the original because their frame was still decoding. When the
        // frame is ready, pixels that no later draw touched (mark still holds the draw's
        // generation, page value unchanged) get the art.
        struct s_deferred {
            s_draw draw;
            uint32_t generation;
            uint32_t published; // publish count when recorded
            std::vector<s_stamp> stamps;
        };
        std::vector<s_deferred> deferred;
        bool upgrading = false; // inside Upgrade_Deferred: replaying a draw older than the page
        std::vector<s_stamp> waiting_stamps;

        void Probe(const s_surface& target, int32_t offset, uint8_t value, const char* what) {
            if (probe_x < 0 || target.width <= 0 || offset % target.width != probe_x || offset / target.width != probe_y) {
                return;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "hd probe (%d,%d) %s: value=%u source=%d (%s) at %d,%d %s", probe_x, probe_y, what, value, (int)draw.source, draw.name.c_str(), draw.x, draw.y, &target == &page_off ? "off" : &target == &page_back ? "back"
                                                                                                                                                                                                                                                           : "image");
        }

        // Darkens `rgb` by the shade mask sampled bilinearly at sub-pixel (sx, sy) of sprite pixel
        // (px, py), which turns the stepped 1x shadow edge into a smooth gradient.
        uint32_t Shade(uint32_t rgb, int32_t px, int32_t py, int32_t sx, int32_t sy) {
            const int32_t width = draw.shade_width;
            const int32_t height = draw.shade_height;
            const int32_t fx = SDL_clamp(px * S + sx - S / 2, 0, (width - 1) * S);
            const int32_t fy = SDL_clamp(py * S + sy - S / 2, 0, (height - 1) * S);
            const int32_t x0 = fx / S;
            const int32_t y0 = fy / S;
            const int32_t x1 = SDL_min(x0 + 1, width - 1);
            const int32_t y1 = SDL_min(y0 + 1, height - 1);
            const int32_t wx = fx - x0 * S;
            const int32_t wy = fy - y0 * S;
            const uint8_t* b = draw.shade.data();
            const int32_t top = b[y0 * width + x0] * (S - wx) + b[y0 * width + x1] * wx;
            const int32_t bottom = b[y1 * width + x0] * (S - wx) + b[y1 * width + x1] * wx;
            const uint32_t percent = (uint32_t)((top * (S - wy) + bottom * wy) / (S * S));
            if (percent >= 100) {
                return rgb;
            }
            return ((((rgb >> 16) & 0xFF) * percent / 100u) << 16) | ((((rgb >> 8) & 0xFF) * percent / 100u) << 8) |
                ((rgb & 0xFF) * percent / 100u);
        }

        // The current draw's HD art at sub-pixel (sx, sy) of target pixel (x, y), over `dest`.
        uint32_t Apply_Art(uint32_t dest, int32_t x, int32_t y, int32_t sx, int32_t sy) {
            const s_hd_frame* frame = draw.frame;
            const int32_t lx = (x - draw.x - frame->origin_x) * S + sx;
            const int32_t ly = (y - draw.y - frame->origin_y) * S + sy;
            if (lx < 0 || ly < 0 || lx >= frame->width || ly >= frame->height) {
                return dest;
            }
            const size_t i = (size_t)ly * frame->width + lx;
            const uint32_t alpha = frame->alpha[i];
            if (alpha == 0) {
                return dest;
            }
            if (frame->rgb != nullptr) {
                uint32_t rgb = ((uint32_t)frame->rgb[i * 3] << 16) | ((uint32_t)frame->rgb[i * 3 + 1] << 8) | frame->rgb[i * 3 + 2];
                if (!draw.shade.empty()) {
                    rgb = Shade(rgb, x - draw.x, y - draw.y, sx, sy);
                }
                return Over_Rgb(dest, rgb, alpha);
            }
            uint8_t index = frame->index[i];
            if (draw.glyph) {
                // Glyph art stores colour roles; the style supplies the indices.
                if (index < 1 || index > 8) {
                    return dest;
                }
                return Over_Index(dest, draw.colors[index - 1], alpha);
            }
            if (draw.glassed && index >= HD_GLASS_FIRST) {
                return Glass_Tint(dest, index - HD_GLASS_FIRST, alpha);
            }
            if (draw.remapped) {
                index = draw.remap[index];
            }
            if (!draw.shade.empty()) {
                return Over_Rgb(dest, Shade(Base_Rgb(index), x - draw.x, y - draw.y, sx, sy), alpha);
            }
            return Over_Index(dest, index, alpha);
        }

        void Apply_Art_Block(s_surface& target, int32_t x, int32_t y) {
            uint32_t* block = Block(target, x, y);
            const size_t stride = (size_t)target.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    block[sy * stride + sx] = Apply_Art(block[sy * stride + sx], x, y, sx, sy);
                }
            }
            Mark_Row(target, y);
        }

        // A pixel written by a draw without HD art (or an image draw), rendered right away.
        void Render_Now(s_surface& target, int32_t offset, uint8_t value, int32_t level) {
            if (target.hd.empty()) {
                return;
            }
            const int32_t x = offset % target.width;
            const int32_t y = offset / target.width;
            target.shadow[(size_t)offset] = value;
            if (level >= 0) {
                // Glass over whatever HD pixels are underneath.
                uint32_t* block = Block(target, x, y);
                const size_t stride = (size_t)target.width * S;
                for (int32_t sy = 0; sy < S; ++sy) {
                    for (int32_t sx = 0; sx < S; ++sx) {
                        block[sy * stride + sx] = Glass_Tint(block[sy * stride + sx], level, 255u);
                    }
                }
                Mark_Row(target, y);
                return;
            }
            if (draw.source == SOURCE_IMAGE) {
                const s_surface& image = *draw.image;
                const int32_t ix = x - draw.image_x;
                const int32_t iy = y - draw.image_y;
                const uint16_t made_for = !image.hd.empty() && ix >= 0 && iy >= 0 && ix < image.width && iy < image.height
                    ? image.shadow[(size_t)iy * image.width + ix]
                    : SHADOW_NONE;
                // A remapped image draw (e.g. the hovered colony building flashing) recolours
                // the image's HD pixels with the same table.
                if (made_for != SHADOW_NONE && (draw.remapped ? draw.remap[made_for] : made_for) == value) {
                    const uint32_t* src = &image.hd[(size_t)iy * S * image.width * S + (size_t)ix * S];
                    const size_t src_stride = (size_t)image.width * S;
                    uint32_t* block = Block(target, x, y);
                    const size_t stride = (size_t)target.width * S;
                    for (int32_t sy = 0; sy < S; ++sy) {
                        for (int32_t sx = 0; sx < S; ++sx) {
                            const uint32_t pixel = src[sy * src_stride + sx];
                            uint32_t& out = block[sy * stride + sx];
                            if ((pixel & HD_CLEAR) != 0 && (pixel & HD_LITERAL) == 0) {
                                // Partly covered over the image's transparent background.
                                if (((pixel >> 16) & 0xFF) != 0) {
                                    out = Over_Index(out, draw.remapped ? draw.remap[pixel & 0xFF] : (uint8_t)pixel, (pixel >> 16) & 0xFF);
                                }
                            } else if (draw.remapped && (pixel & HD_LITERAL) == 0) {
                                out = (pixel & 0xFFFF0000u) | ((uint32_t)draw.remap[(pixel >> 8) & 0xFF] << 8) |
                                    draw.remap[pixel & 0xFF];
                            } else {
                                out = pixel;
                            }
                        }
                    }
                    Mark_Row(target, y);
                    return;
                }
            }
            Fill_Block(target, x, y, Indexed(value));
        }

        bool Marked(const s_surface& target, int32_t x, int32_t y) {
            return x >= 0 && y >= 0 && x < target.width && y < target.height &&
                target.mark[(size_t)y * target.width + x] == generation;
        }

        bool Interior(const s_surface& target, const s_stamp& stamp) {
            const int32_t x = stamp.offset % target.width;
            const int32_t y = stamp.offset / target.width;
            return stamp.level < 0 && Marked(target, x - 1, y) && Marked(target, x + 1, y) && Marked(target, x, y - 1) &&
                Marked(target, x, y + 1);
        }

        // Whether the art agrees with what the game drew: inside the sprite, the art's colour at
        // each pixel centre must mostly be close to the pixel written. Art made with the wrong
        // palette, for another frame or for different source pixels fails, and the draw shows
        // the original pixels instead. Redrawn art is new art by design, so only upscales are
        // checked: a redrawn combat ship differs from its pixels by about half and would flicker
        // between HD and original as it turns.
        bool Art_Matches(const s_surface& target) {
            if (draw.redrawn) {
                return true;
            }
            int32_t samples = 0;
            int32_t mismatches = 0;
            for (const s_stamp& stamp : pending) {
                if (stamp.target != &target || !Interior(target, stamp)) {
                    continue;
                }
                const int32_t x = stamp.offset % target.width;
                const int32_t y = stamp.offset / target.width;
                const uint32_t art = Base_Color(Apply_Art(Indexed(stamp.value), x, y, S / 2, S / 2));
                const uint32_t drawn = Base_Rgb(stamp.value);
                const int32_t distance = abs((int32_t)((art >> 16) & 0xFF) - (int32_t)((drawn >> 16) & 0xFF)) +
                    abs((int32_t)((art >> 8) & 0xFF) - (int32_t)((drawn >> 8) & 0xFF)) +
                    abs((int32_t)(art & 0xFF) - (int32_t)(drawn & 0xFF));
                samples++;
                mismatches += distance > 150 ? 1 : 0;
            }
            return samples < 12 || mismatches * 10 < samples * 6;
        }

        // Renders the pending stamps of an art draw into one target.
        void Render_Pending_Target(s_surface& target) {
            if (!Art_Matches(target)) {
                if (stats_enabled) {
                    Stats_Missing("art-mismatch:" + draw.name);
                }
                for (const s_stamp& stamp : pending) {
                    if (stamp.target == &target) {
                        Render_Now(target, stamp.offset, stamp.value, stamp.level);
                    }
                }
                return;
            }
            int32_t x0 = target.width;
            int32_t y0 = target.height;
            int32_t x1 = -1;
            int32_t y1 = -1;
            for (const s_stamp& stamp : pending) {
                if (stamp.target != &target) {
                    continue;
                }
                const int32_t x = stamp.offset % target.width;
                const int32_t y = stamp.offset / target.width;
                x0 = SDL_min(x0, x);
                y0 = SDL_min(y0, y);
                x1 = SDL_max(x1, x);
                y1 = SDL_max(y1, y);
                // Inside the sprite the art goes over the original pixel; at its edges over the
                // HD pixels already there, so the art's own soft outline shows.
                if (Interior(target, stamp)) {
                    Fill_Block(target, x, y, Indexed(stamp.value));
                }
                Apply_Art_Block(target, x, y);
                target.shadow[(size_t)stamp.offset] = stamp.value;
                if (!target.fringe.empty()) {
                    target.fringe[(size_t)stamp.offset] = 0;
                }
            }
            if (x1 < 0 || target.row_dirty.empty()) {
                return; // images get no fringe: their transparent pixels are never drawn
            }
            // Fringe: the art's soft edge past the original outline, over unwritten neighbours.
            const int32_t clip_x1 = SDL_max(0, draw.clip_x1);
            const int32_t clip_y1 = SDL_max(0, draw.clip_y1);
            const int32_t clip_x2 = SDL_min(target.width - 1, draw.clip_x2);
            const int32_t clip_y2 = SDL_min(target.height - 1, draw.clip_y2);
            for (int32_t y = SDL_max(y0 - 1, clip_y1); y <= SDL_min(y1 + 1, clip_y2); ++y) {
                for (int32_t x = SDL_max(x0 - 1, clip_x1); x <= SDL_min(x1 + 1, clip_x2); ++x) {
                    if (Marked(target, x, y)) {
                        continue;
                    }
                    // A late upgrade (Upgrade_Deferred) runs after later draws: blocks they claimed
                    // are not free neighbours (text over a panel whose art was still loading).
                    if (upgrading && (int32_t)(target.mark[(size_t)y * target.width + x] - generation) > 0) {
                        continue;
                    }
                    if (Marked(target, x - 1, y) || Marked(target, x + 1, y) || Marked(target, x, y - 1) ||
                        Marked(target, x, y + 1)) {
                        Apply_Art_Block(target, x, y);
                        target.fringe[(size_t)y * target.width + x] = 1;
                    }
                }
            }
        }

    } // namespace

    void Flush_Pending() {
        if (pending.empty()) {
            return;
        }
        s_scoped_timer timer(TIMER_RENDER);
        while (!pending.empty()) {
            s_surface* target = pending.front().target;
            Render_Pending_Target(*target);
            pending.erase(std::remove_if(pending.begin(), pending.end(), [target](const s_stamp& stamp) { return stamp.target == target; }),
                pending.end());
        }
    }

    namespace {

        void Start_Draw() {
            Flush_Pending();
            if (draw.waiting && !waiting_stamps.empty()) {
                if (deferred.size() >= 4096) {
                    deferred.erase(deferred.begin(), deferred.begin() + 1024);
                }
                deferred.push_back({draw, generation, publish_count, std::move(waiting_stamps)});
                waiting_stamps.clear();
            }
            waiting_stamps.clear();
            draw.waiting = false;
            draw.source = SOURCE_NONE;
            draw.frame = nullptr;
            draw.glyph = false;
            draw.glassed = false;
            draw.redrawn = false;
            draw.remapped = false;
            draw.shade.clear();
            draw.image = nullptr;
            draw.name.clear();
            if (++generation == 0) {
                generation = 1;
            }
        }

        void Stamp(s_surface& target, int32_t offset, uint8_t value, int32_t level) {
            if (probe_x >= 0) {
                Probe(target, offset, value, "stamp");
            }
            if (draw.source == SOURCE_ART && draw.waiting) {
                Render_Now(target, offset, value, level);
                if (!target.row_dirty.empty()) {
                    target.mark[(size_t)offset] = generation;
                    waiting_stamps.push_back({&target, offset, value, (int8_t)level});
                }
                return;
            }
            if (draw.source == SOURCE_ART) {
                Ensure_Hd(target);
                target.mark[(size_t)offset] = generation;
                pending.push_back({&target, offset, value, (int8_t)level});
                return;
            }
            Render_Now(target, offset, value, level);
            // Claim the block for this draw too (glyphs, plain writes): a sprite drawn earlier
            // whose art arrives late must not repaint over it (Upgrade_Deferred checks the mark).
            if (!target.mark.empty()) {
                target.mark[(size_t)offset] = generation;
            }
        }

        // Settles the area the current art draw can touch, fringe included.
        void Settle_Art_Area() {
            if (animate::clipping_flag == 1) {
                draw.clip_x1 = video::screen_window_x1;
                draw.clip_y1 = video::screen_window_y1;
                draw.clip_x2 = video::screen_window_x2;
                draw.clip_y2 = video::screen_window_y2;
            } else {
                draw.clip_x1 = draw.clip_y1 = 0;
                draw.clip_x2 = draw.clip_y2 = 0x7FFF;
            }
            const s_hd_frame* frame = draw.frame;
            const int32_t left = draw.x + frame->origin_x;
            const int32_t top = draw.y + frame->origin_y;
            Settle_Draw_Area(left - 1, top - 1, left + (frame->width + S - 1) / S + 1, top + (frame->height + S - 1) / S + 1);
        }

        // Sets up the current draw for `anim` drawn with its top-left at (x, y).
        void Begin_Source(const s_animation_header* anim, int16_t x, int16_t y) {
            Start_Draw();
            if (!Legacy() || anim == nullptr) {
                return;
            }
            if (!capture_dir.empty() || probe_x >= 0 || stats_enabled) {
                const auto name = stats_names.find(anim);
                if (name != stats_names.end()) {
                    draw.name = name->second;
                    if (!capture_dir.empty()) {
                        Capture_Palette(name->second);
                    }
                }
            }
            const auto image = images.find(anim);
            if (image != images.end() && !image->second.hd.empty() && image->second.width == anim->width &&
                image->second.height == anim->height) {
                draw.source = SOURCE_IMAGE;
                draw.image = &image->second;
                draw.image_x = x;
                draw.image_y = y;
                Settle_Draw_Area(x, y, x + anim->width, y + anim->height);
                return;
            }
            const auto found = registrations.find(anim);
            if (found == registrations.end()) {
                if (stats_enabled) {
                    Stats_Missing(!draw.name.empty() ? draw.name
                                                     : "unregistered:" + std::to_string(anim->width) + "x" + std::to_string(anim->height));
                }
                return;
            }
            // The buffer may have been reused for other data since it was loaded; the header
            // (dimensions, frame count, flags) must still match, apart from the frame counter.
            uint8_t header[12];
            memcpy(header, anim, sizeof(header));
            memcpy(header + 4, found->second.header + 4, 2);
            if (memcmp(header, found->second.header, sizeof(header)) != 0) {
                return;
            }
            s_hd_asset* asset = assets[found->second.asset];
            if (!asset->loaded) {
                Load_Asset(asset);
            }
            if (asset->has_source_hash && asset->source_hash != found->second.data_hash) {
                if (stats_enabled) {
                    Stats_Missing("source-mismatch:" + draw.name);
                }
                return;
            }
            const s_hd_frame* frame = Resolve_Frame(asset, anim->current_frame);
            if (frame == nullptr) {
                return;
            }
            asset->last_used = publish_count;
            if (!Frame_Ready(frame)) {
                Request_Frame(const_cast<s_hd_frame*>(frame), true);
                draw.waiting = true;
            }
            draw.source = SOURCE_ART;
            draw.frame = frame;
            draw.x = x;
            draw.y = y;
            if (found->second.recolored) {
                memcpy(draw.remap, found->second.recolor, sizeof(draw.remap));
                draw.remapped = true;
            }
            draw.glassed = asset->glassed || (anim->flags & ANIMATION_FLAG_GLASSED) != 0;
            draw.redrawn = asset->redrawn;
            Settle_Art_Area();
        }

        uint32_t Settle_Fringe(s_surface& surface, const uint8_t* page, int32_t x, int32_t y) {
            if (x < 0 || y < 0 || x >= surface.width || y >= surface.height) {
                return 0;
            }
            const size_t i = (size_t)y * surface.width + x;
            if (surface.fringe[i] == 0) {
                return 0;
            }
            surface.shadow[i] = page[i];
            Fill_Block(surface, x, y, Indexed(page[i]));
            return 1;
        }

    } // namespace

    uint32_t Settle(s_surface& surface, const uint8_t* page, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
        if (surface.hd.empty() || page == nullptr) {
            return 0;
        }
        x0 = SDL_max(x0, 0);
        y0 = SDL_max(y0, 0);
        x1 = SDL_min(x1, surface.width);
        y1 = SDL_min(y1, surface.height);
        uint32_t settled = 0;
        for (int32_t y = y0; y < y1; ++y) {
            const size_t row = (size_t)y * surface.width;
            for (int32_t x = x0; x < x1; ++x) {
                if (surface.shadow[row + x] != page[row + x]) {
                    const uint8_t value = page[row + x];
                    surface.shadow[row + x] = value;
                    Fill_Block(surface, x, y, debug_sd ? Literal(Mix(0xFF0000u, Base_Rgb(value), 96)) : Indexed(value));
                    settled++;
                    // Art spilled from this pixel onto unchanged neighbours goes with it.
                    if (!surface.fringe.empty()) {
                        settled += Settle_Fringe(surface, page, x - 1, y) + Settle_Fringe(surface, page, x + 1, y) +
                            Settle_Fringe(surface, page, x, y - 1) + Settle_Fringe(surface, page, x, y + 1);
                    }
                }
            }
        }
        return settled;
    }

    // Gives deferred draws their art once it is decoded (game thread, between draws).
    void Upgrade_Deferred() {
        if (deferred.empty()) {
            return;
        }
        s_draw saved_draw = draw;
        const uint32_t saved_generation = generation;
        size_t kept = 0;
        for (size_t i = 0; i < deferred.size(); ++i) {
            s_deferred& entry = deferred[i];
            const uint8_t state = entry.draw.frame->load.load(std::memory_order_acquire);
            if (state != LOAD_READY) {
                // Still decoding (or evicted and queued again): keep it a while.
                if (state != LOAD_FAILED && publish_count - entry.published < 600) {
                    if (kept != i) {
                        deferred[kept] = std::move(entry);
                    }
                    kept++;
                }
                continue;
            }
            draw = entry.draw;
            draw.waiting = false;
            generation = entry.generation;
            // Screens often copy the finished page to the other page and restore from it, so
            // the other page may hold the same plain blocks: those get the upgraded art too.
            std::vector<s_stamp> twins;
            for (const s_stamp& stamp : entry.stamps) {
                s_surface& target = *stamp.target;
                if (target.mark[(size_t)stamp.offset] != entry.generation || target.shadow[(size_t)stamp.offset] != stamp.value) {
                    continue;
                }
                // A later draw's soft edge spilled into this block (e.g. text over a panel
                // whose art was still loading): repainting it would erase that edge.
                if (!target.fringe.empty() && target.fringe[(size_t)stamp.offset] != 0) {
                    continue;
                }
                pending.push_back(stamp);
                s_surface* other = &target == &page_off ? &page_back : &target == &page_back ? &page_off
                                                                                             : nullptr;
                if (other != nullptr && !other->hd.empty() && other->shadow[(size_t)stamp.offset] == stamp.value) {
                    const uint32_t* block = Block(*other, stamp.offset % other->width, stamp.offset / other->width);
                    const size_t stride = (size_t)other->width * S;
                    bool plain = true;
                    for (int32_t sy = 0; sy < S && plain; ++sy) {
                        for (int32_t sx = 0; sx < S && plain; ++sx) {
                            plain = block[sy * stride + sx] == Indexed(stamp.value);
                        }
                    }
                    if (plain) {
                        twins.push_back({other, stamp.offset, stamp.value, stamp.level});
                    }
                }
            }
            upgrading = true;
            Flush_Pending();
            upgrading = false;
            for (const s_stamp& twin : twins) {
                s_surface& from = twin.target == &page_off ? page_back : page_off;
                const int32_t x = twin.offset % from.width;
                const int32_t y = twin.offset / from.width;
                const size_t stride = (size_t)from.width * S;
                const uint32_t* src = Block(from, x, y);
                uint32_t* dst = Block(*twin.target, x, y);
                for (int32_t sy = 0; sy < S; ++sy) {
                    memcpy(dst + sy * stride, src + sy * stride, S * sizeof(uint32_t));
                }
                Mark_Row(*twin.target, y);
            }
        }
        deferred.resize(kept);
        draw = saved_draw;
        generation = saved_generation;
    }

    size_t Deferred_Count() {
        return deferred.size();
    }

    void Reset_Stamps() {
        pending.clear();
        deferred.clear();
        waiting_stamps.clear();
    }

    void Begin_Draw(const s_animation_header* anim, int16_t x, int16_t y) {
        Begin_Source(anim, x, y);
    }

    void Begin_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap) {
        Begin_Source(anim, x, y);
        if (remap == nullptr) {
            return;
        }
        if (draw.source == SOURCE_ART || draw.source == SOURCE_IMAGE) {
            if (draw.remapped) {
                // The art was recoloured in place first (Note_Recolor); this draw's remap applies after.
                for (int32_t i = 0; i < 256; ++i) {
                    draw.remap[i] = remap[draw.remap[i]];
                }
            } else {
                memcpy(draw.remap, remap, sizeof(draw.remap));
            }
            draw.remapped = true;
        }
    }

    void Begin_Masked_Remap_Draw(const s_animation_header* anim, int16_t x, int16_t y, const uint8_t* remap, const int8_t* mask, const int8_t* gray_percentages, int32_t level_count) {
        Begin_Remap_Draw(anim, x, y, remap);
        if (draw.source != SOURCE_ART || mask == nullptr || anim == nullptr) {
            return;
        }
        draw.shade_width = anim->width;
        draw.shade_height = anim->height;
        draw.shade.resize((size_t)anim->width * anim->height);
        for (size_t i = 0; i < draw.shade.size(); ++i) {
            const int32_t level = mask[i];
            // Level 0 is drawn unshaded; level n darkens by gray_percentages[n] percent.
            const int32_t percent = level <= 0 || level >= level_count ? 0 : gray_percentages[level];
            draw.shade[i] = (uint8_t)SDL_clamp(100 - percent, 0, 100);
        }
    }

    void Begin_Draw_To_Bitmap(const s_animation_header* src, int16_t x, int16_t y, const void* target_bitmap) {
        (void)target_bitmap;
        Begin_Source(src, x, y);
    }

    void Begin_Glyph(const char* font_file, int16_t font_index, uint8_t code, int16_t x, int16_t y, const uint8_t* colors) {
        if (!initialized) {
            Initialize();
        }
        Start_Draw();
        if (!Legacy() || font_file == nullptr || colors == nullptr) {
            return;
        }
        const s_hd_frame* glyph = Glyph_Art(font_file, font_index, code);
        if (glyph == nullptr) {
            return;
        }
        draw.source = SOURCE_ART;
        draw.frame = glyph;
        draw.glyph = true;
        draw.x = x;
        draw.y = y;
        memcpy(draw.colors, colors, sizeof(draw.colors));
        Settle_Art_Area();
        if (probe_x >= 0) {
            draw.name = std::string("glyph:") + font_file + "/" + std::to_string(font_index) + "/" + std::to_string(code);
        }
    }

    void Begin_Asset_Draw(const char* lbx_name, int32_t entry, int16_t frame, int16_t x, int16_t y) {
        if (!initialized) {
            Initialize();
        }
        Start_Draw();
        if (!Legacy() || lbx_name == nullptr) {
            return;
        }
        const int32_t asset_index = Find_Or_Create_Asset(lbx_name, entry);
        if (asset_index < 0) {
            return;
        }
        s_hd_asset* asset = assets[asset_index];
        if (!asset->loaded) {
            Load_Asset(asset);
        }
        const s_hd_frame* art = Resolve_Frame(asset, frame);
        if (art == nullptr) {
            return;
        }
        asset->last_used = publish_count;
        if (!Frame_Ready(art)) {
            Request_Frame(const_cast<s_hd_frame*>(art), true);
            draw.waiting = true;
        }
        draw.source = SOURCE_ART;
        draw.frame = art;
        draw.x = x;
        draw.y = y;
        draw.glassed = asset->glassed;
        draw.redrawn = asset->redrawn;
        Settle_Art_Area();
        if (probe_x >= 0) {
            draw.name = asset->directory;
        }
    }

    void End_Draw() {
        Start_Draw();
    }

    void Stamp_Run(const uint8_t* dst, int32_t count) {
        Stamp_Page(video::current_video_page, dst, count);
    }

    void Literal_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t* rgb) {
        if (twins_on) {
            twin::Literal_Rect(page, x, y, width, height, rgb);
            return;
        }
        if (!Legacy() || rgb == nullptr || width <= 0 || height <= 0) {
            return;
        }
        s_surface* target = Page_Surface(page);
        if (target == nullptr) {
            return;
        }
        Flush_Pending();
        const size_t stride = (size_t)target->width * S;
        const size_t rgb_stride = (size_t)width * S * 3;
        const int32_t x0 = SDL_max(x, 0);
        const int32_t y0 = SDL_max(y, 0);
        const int32_t x1 = SDL_min(x + width, target->width);
        const int32_t y1 = SDL_min(y + height, target->height);
        for (int32_t py = y0; py < y1; ++py) {
            for (int32_t px = x0; px < x1; ++px) {
                const size_t i = (size_t)py * target->width + px;
                // The block belongs to the page value just written, so settling keeps it.
                target->shadow[i] = page[i];
                uint32_t* block = Block(*target, px, py);
                const uint8_t* source = rgb + (size_t)(py - y) * S * rgb_stride + (size_t)(px - x) * S * 3;
                for (int32_t sy = 0; sy < S; ++sy) {
                    const uint8_t* in = source + (size_t)sy * rgb_stride;
                    for (int32_t sx = 0; sx < S; ++sx, in += 3) {
                        block[sy * stride + sx] = Literal(((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) | in[2]);
                    }
                }
            }
            Mark_Row(*target, py);
        }
    }

    void Stamp_Page(const uint8_t* page, const uint8_t* dst, int32_t count) {
        if (!Legacy() || count <= 0) {
            return;
        }
        s_surface* target = Page_Surface(page);
        if (target == nullptr) {
            return;
        }
        const ptrdiff_t offset = dst - page;
        if (offset < 0 || offset + count > (ptrdiff_t)target->width * target->height) {
            return;
        }
        for (int32_t i = 0; i < count; ++i) {
            Stamp(*target, (int32_t)offset + i, dst[i], -1);
        }
    }

    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        if (!Legacy()) {
            return;
        }
        s_surface* target = Page_Surface(page);
        if (target == nullptr) {
            return;
        }
        Flush_Pending();
        x1 = SDL_max(x1, 0);
        y1 = SDL_max(y1, 0);
        x2 = SDL_min(x2, target->width - 1);
        y2 = SDL_min(y2, target->height - 1);
        for (int32_t y = y1; y <= y2; ++y) {
            const size_t row = (size_t)y * target->width;
            for (int32_t x = x1; x <= x2; ++x) {
                target->shadow[row + x] = page[row + x];
                Fill_Block(*target, x, y, Indexed(page[row + x]));
            }
        }
    }

    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level) {
        if (!Legacy() || width <= 0 || height <= 0) {
            return;
        }
        s_surface* target = Page_Surface(page);
        if (target == nullptr) {
            return;
        }
        Flush_Pending();
        const uint8_t* table = video::glass_remap_colors[level & 15];
        const size_t stride = (size_t)target->width * S;
        const int32_t x0 = SDL_max(x, 0);
        const int32_t y0 = SDL_max(y, 0);
        const int32_t x1 = SDL_min(x + width, target->width);
        const int32_t y1 = SDL_min(y + height, target->height);
        for (int32_t py = y0; py < y1; ++py) {
            for (int32_t px = x0; px < x1; ++px) {
                const size_t i = (size_t)py * target->width + px;
                const uint16_t old = target->shadow[i];
                // Only blocks that showed the pixel before the tint can be tinted; others settle.
                if (old == SHADOW_NONE || table[old] != page[i]) {
                    target->shadow[i] = SHADOW_NONE;
                    continue;
                }
                target->shadow[i] = page[i];
                uint32_t* block = Block(*target, px, py);
                for (int32_t sy = 0; sy < S; ++sy) {
                    for (int32_t sx = 0; sx < S; ++sx) {
                        block[sy * stride + sx] = Glass_Tint(block[sy * stride + sx], level, 255u);
                    }
                }
            }
            Mark_Row(*target, py);
        }
    }

    void Stamp_Glass(const uint8_t* dst, uint8_t background_value, int32_t level) {
        (void)background_value;
        if (!Legacy()) {
            return;
        }
        s_surface* target = Page_Surface(video::current_video_page);
        if (target == nullptr) {
            return;
        }
        const ptrdiff_t offset = dst - video::current_video_page;
        if (offset < 0 || offset >= (ptrdiff_t)target->width * target->height) {
            return;
        }
        Stamp(*target, (int32_t)offset, *dst, level);
    }

    void Stamp_Bitmap(const void* bitmap, int32_t index, uint8_t value) {
        if (!Legacy()) {
            return;
        }
        const auto found = images.find(bitmap);
        if (found == images.end()) {
            return;
        }
        s_surface& image = found->second;
        if (index < 0 || index >= image.width * image.height) {
            return;
        }
        if (draw.source == SOURCE_IMAGE && draw.image != &image) {
            Ensure_Hd(image);
        }
        Stamp(image, index, value, -1);
    }

} // namespace render
