#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include "antares/identity.h"

#include <algorithm>

// Twin planes, WP1.3b: the core drawing twins.
//
// A draw (sprite, glyph, file animation frame, image) is set up by the Begin_* hooks into
// current_draw (stamps.cpp, shared with attempt 2). The blitters report the pixels they write
// (Stamp_Run, Stamp_Glass, Stamp_Bitmap): this is the 1x operation's own record of which pixels it
// touched, a decision input (D7). When the draw ends, its art is drawn at 4x over those pixels,
// spilling its soft edge onto untouched neighbours inside the clip window; without art (or while
// it decodes) each touched pixel is bound to the index the sprite wrote there. The open scopes
// are then told those pixels are handled, so their Unported fallback leaves them alone.
namespace render::twin {

    namespace {

        struct s_touch {
            s_plane* plane;
            const void* target; // scope key: a page's 1x pixels or an image header
            const uint8_t* pixels;
            int32_t offset;
            int8_t level; // glass level, -1 for a plain pixel
        };

        std::vector<s_touch> touched;
        uint32_t generation = 1;

        // Per 1x pixel of a plane: the generation of the draw that last touched it.
        std::unordered_map<const s_plane*, std::vector<uint32_t>> marks;

        std::vector<uint32_t>& Marks(const s_plane& plane) {
            std::vector<uint32_t>& mark = marks[&plane];
            if (mark.size() != (size_t)plane.width * plane.height) {
                mark.assign((size_t)plane.width * plane.height, 0u);
            }
            return mark;
        }

        // Per 1x pixel of a plane: the generation of the draw that last rendered it.
        std::unordered_map<const s_plane*, std::vector<uint32_t>> dones;

        std::vector<uint32_t>& Done(const s_plane& plane) {
            std::vector<uint32_t>& done = dones[&plane];
            if (done.size() != (size_t)plane.width * plane.height) {
                done.assign((size_t)plane.width * plane.height, 0u);
            }
            return done;
        }

        void Mark_Row(s_plane& plane, int32_t y) {
            if (!plane.row_dirty.empty()) {
                plane.row_dirty[(size_t)y] = 1;
            }
        }

        // ---------------------------------------------------------------- pixel operations

        uint32_t Base_Color(const s_px& px) {
            if (Kind(px) != KIND_INDEX) {
                return px.base & 0xFFFFFFu;
            }
            const uint32_t alpha = (px.mask >> 16) & 0xFF;
            const uint32_t fg = Base_Rgb((uint8_t)px.mask);
            if (alpha >= 255) {
                return fg;
            }
            const uint32_t bg = (px.mask & MASK_CLEAR) != 0 ? 0u : Base_Rgb((uint8_t)(px.mask >> 8));
            return Mix(fg, bg, alpha);
        }

        // Palette index `index` with coverage `alpha` over `dest`; one level of blending is kept, as
        // in attempt 2: the destination's dominant index becomes the background.
        s_px Over_Index(const s_px& dest, uint8_t index, uint32_t alpha) {
            if (alpha >= 255) {
                return Index_Px(index);
            }
            if (Kind(dest) != KIND_INDEX) {
                return Literal_Px(Mix(Base_Rgb(index), dest.base & 0xFFFFFFu, alpha));
            }
            const uint32_t cover = (dest.mask >> 16) & 0xFF;
            const uint32_t under = cover >= 128 ? (dest.mask & 0xFF) : ((dest.mask >> 8) & 0xFF);
            const uint32_t clear = cover >= 128 ? 0u : (dest.mask & MASK_CLEAR);
            return {0u, index | (under << 8) | (alpha << 16) | (KIND_INDEX << KIND_SHIFT) | clear};
        }

        s_px Over_Rgb(const s_px& dest, uint32_t rgb, uint32_t alpha) {
            return Literal_Px(alpha >= 255 ? rgb : Mix(rgb, Base_Color(dest), alpha));
        }

        // Full-colour pixels under glass follow the game's glass table too: the original maps every
        // index through it (a red light under the build screen's dark glass turns dark blue), so a
        // true-colour tint (Glass_Rgb) left saturated details showing. The pixel's colour is matched
        // to its nearest palette index, that index goes through the table, and the pixel keeps its
        // own brightness relative to the index, so the art's detail survives.
        std::vector<uint8_t> nearest_lut; // 15-bit RGB -> nearest palette index; 0xFF00 marks unset
        std::vector<uint8_t> nearest_ready;
        uint8_t lut_palette[768];

        void Sync_Glass_Lut() {
            uint8_t palette[768];
            for (int32_t i = 0; i < 256; ++i) {
                palette[i * 3] = video::current_palette[i].r;
                palette[i * 3 + 1] = video::current_palette[i].g;
                palette[i * 3 + 2] = video::current_palette[i].b;
            }
            if (nearest_lut.empty() || memcmp(palette, lut_palette, sizeof(palette)) != 0) {
                memcpy(lut_palette, palette, sizeof(palette));
                nearest_lut.assign(32768, 0);
                nearest_ready.assign(32768, 0);
            }
        }

        uint8_t Nearest_Index(uint32_t rgb) {
            const uint32_t key = (((rgb >> 19) & 0x1F) << 10) | (((rgb >> 11) & 0x1F) << 5) | ((rgb >> 3) & 0x1F);
            if (nearest_ready[key] == 0) {
                const int32_t r = (int32_t)((rgb >> 16) & 0xF8) + 4;
                const int32_t g = (int32_t)((rgb >> 8) & 0xF8) + 4;
                const int32_t b = (int32_t)(rgb & 0xF8) + 4;
                int32_t best = 0;
                int32_t best_distance = 0x7FFFFFFF;
                for (int32_t i = 0; i < 256; ++i) {
                    const uint32_t c = Base_Rgb((uint8_t)i);
                    const int32_t dr = (int32_t)((c >> 16) & 0xFF) - r;
                    const int32_t dg = (int32_t)((c >> 8) & 0xFF) - g;
                    const int32_t db = (int32_t)(c & 0xFF) - b;
                    const int32_t distance = dr * dr * 3 + dg * dg * 6 + db * db;
                    if (distance < best_distance) {
                        best_distance = distance;
                        best = i;
                    }
                }
                nearest_lut[key] = (uint8_t)best;
                nearest_ready[key] = 1;
            }
            return nearest_lut[key];
        }

        uint32_t Luma(uint32_t rgb) {
            return (((rgb >> 16) & 0xFF) * 77u + ((rgb >> 8) & 0xFF) * 150u + (rgb & 0xFF) * 29u) >> 8;
        }

        uint32_t Glass_Through_Table(uint32_t rgb, int32_t level) {
            if (nearest_lut.empty()) {
                Sync_Glass_Lut();
            }
            const uint8_t index = Nearest_Index(rgb);
            const uint32_t through = Base_Rgb(video::glass_remap_colors[level & 15][index]);
            const uint32_t base_luma = SDL_max(Luma(Base_Rgb(index)), 1u);
            const uint32_t ratio = SDL_clamp(Luma(rgb) * 256u / base_luma, 128u, 512u); // keep detail, within reason
            uint32_t out = 0;
            for (int32_t shift = 0; shift <= 16; shift += 8) {
                out |= SDL_min(255u, ((through >> shift) & 0xFF) * ratio / 256u) << shift;
            }
            return out;
        }

        // Glass of level `level` over `dest`: index-bound pixels go through the game's own glass
        // table (exactly what the 1x pixel gets); full-colour pixels are tinted in true colour.
        // The D20 `glass` kind (WP1.4) refines partial coverage.
        s_px Glass_Over(const s_px& dest, int32_t level) {
            if (Kind(dest) == KIND_INDEX) {
                const uint8_t* table = video::glass_remap_colors[level & 15];
                const uint32_t fg = table[dest.mask & 0xFF];
                const uint32_t bg = table[(dest.mask >> 8) & 0xFF];
                return {0u, (dest.mask & ~0xFFFFu) | (bg << 8) | fg};
            }
            return Literal_Px(Glass_Through_Table(dest.base & 0xFFFFFFu, level));
        }

        // Darkens `rgb` by the shade mask sampled bilinearly (Begin_Masked_Remap_Draw).
        uint32_t Shade(uint32_t rgb, int32_t px, int32_t py, int32_t sx, int32_t sy) {
            const s_draw& draw = current_draw;
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

        // The draw's art at sub-pixel (sx, sy) of 1x pixel (x, y), over `dest`.
        // `fringe`: a pixel the 1x draw didn't write. Glass in the art never lands there: glass the
        // 1x draw left out was left out on purpose (Draw_No_Glass draws glassed sprites without it).
        s_px Apply_Art(const s_px& dest, int32_t x, int32_t y, int32_t sx, int32_t sy, bool fringe) {
            const s_draw& draw = current_draw;
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
                if (index < 1 || index > 8) {
                    return dest;
                }
                return Over_Index(dest, draw.colors[index - 1], alpha);
            }
            if (draw.glassed && index >= 0xF0) {
                return alpha >= 128 && !fringe ? Glass_Over(dest, index - 0xF0) : dest;
            }
            if (draw.remapped) {
                index = draw.remap[index];
            }
            if (!draw.shade.empty()) {
                return Over_Rgb(dest, Shade(Base_Rgb(index), x - draw.x, y - draw.y, sx, sy), alpha);
            }
            return Over_Index(dest, index, alpha);
        }

        // Whether the art at the centre of 1x pixel (x, y) is a glass level.
        bool Art_Is_Glass(int32_t x, int32_t y) {
            const s_draw& draw = current_draw;
            const s_hd_frame* frame = draw.frame;
            if (!draw.glassed || frame->index == nullptr) {
                return false;
            }
            const int32_t lx = (x - draw.x - frame->origin_x) * S + S / 2;
            const int32_t ly = (y - draw.y - frame->origin_y) * S + S / 2;
            if (lx < 0 || ly < 0 || lx >= frame->width || ly >= frame->height) {
                return false;
            }
            const size_t i = (size_t)ly * frame->width + lx;
            return frame->alpha[i] != 0 && frame->index[i] >= 0xF0;
        }

        void Apply_Art_Block(s_plane& plane, int32_t x, int32_t y, bool fringe) {
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    block[sy * stride + sx] = Apply_Art(block[sy * stride + sx], x, y, sx, sy, fringe);
                }
            }
            Mark_Row(plane, y);
        }

        void Glass_Block(s_plane& plane, int32_t x, int32_t y, int32_t level) {
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    block[sy * stride + sx] = Glass_Over(block[sy * stride + sx], level);
                }
            }
            Mark_Row(plane, y);
        }

        // An image with a plane drawn at (draw.x, draw.y): its block at (x, y), through the draw's
        // remap. Partly covered pixels over the image's transparent background blend over the page.
        bool Image_Block(s_plane& plane, int32_t x, int32_t y, const s_plane& image) {
            const s_draw& draw = current_draw;
            const int32_t ix = x - draw.x;
            const int32_t iy = y - draw.y;
            if (ix < 0 || iy < 0 || ix >= image.width || iy >= image.height) {
                return false;
            }
            const s_px* src = &image.px[(size_t)iy * S * image.width * S + (size_t)ix * S];
            const size_t src_stride = (size_t)image.width * S;
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    s_px px = src[sy * src_stride + sx];
                    s_px& out = block[sy * stride + sx];
                    if (Kind(px) == KIND_INDEX) {
                        if (draw.remapped) {
                            px.mask = (px.mask & ~0xFFFFu) | ((uint32_t)draw.remap[(px.mask >> 8) & 0xFF] << 8) | draw.remap[px.mask & 0xFF];
                        }
                        if ((px.mask & MASK_CLEAR) != 0 && ((px.mask >> 16) & 0xFF) < 255) {
                            const uint32_t alpha = (px.mask >> 16) & 0xFF;
                            if (alpha != 0) {
                                out = Over_Index(out, (uint8_t)px.mask, alpha);
                            }
                            continue;
                        }
                    } else if (((px.base >> 24) & 0xFF) < 255) {
                        const uint32_t alpha = (px.base >> 24) & 0xFF;
                        if (alpha != 0) {
                            out = Over_Rgb(out, px.base & 0xFFFFFFu, alpha);
                        }
                        continue;
                    }
                    out = px;
                }
            }
            Mark_Row(plane, y);
            return true;
        }

        // ANTARES_TWIN_CHECK=1 (debug): an image drawn as a source must be in step with its 1x
        // pixels; logs the first mismatches of each image with the operations last run on it.
        void Check_Image(const void* header, const s_plane& image) {
            static int32_t enabled_check = -1;
            if (enabled_check < 0) {
                enabled_check = antares::Get_Env("TWIN_CHECK") != nullptr ? 1 : 0;
            }
            if (enabled_check == 0) {
                return;
            }
            static std::unordered_map<const void*, uint32_t> reported;
            const uint8_t* pixels = (const uint8_t*)header + 0x10;
            uint32_t mismatches = 0;
            int32_t first = -1;
            for (int32_t i = 0; i < image.width * image.height; ++i) {
                const s_px& px = image.px[(size_t)(i / image.width) * S * image.width * S + (size_t)(i % image.width) * S];
                const bool bound = Kind(px) == KIND_INDEX && ((px.mask >> 16) & 0xFF) >= 128 && (uint8_t)px.mask == pixels[i];
                const bool clear = pixels[i] == 0 && Kind(px) == KIND_INDEX && ((px.mask >> 16) & 0xFF) < 128;
                if (!bound && !clear) {
                    mismatches++;
                    first = first < 0 ? i : first;
                }
            }
            if (mismatches != 0 && reported[header]++ < 3) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "twin check: image %p %dx%d drawn with %u stale pixels (first %d,%d: plane %08x, 1x %u); recent ops: %s", header, image.width, image.height, mismatches, first % image.width, first / image.width, image.px[(size_t)(first / image.width) * S * image.width * S + (size_t)(first % image.width) * S].mask, pixels[first], Recent_Ops(header).c_str());
            }
        }

        const s_plane* Image_Source() {
            const s_draw& draw = current_draw;
            if (draw.anim == nullptr) {
                return nullptr;
            }
            const auto found = images.find(draw.anim);
            if (found == images.end() || found->second.px.empty()) {
                return nullptr;
            }
            Check_Image(draw.anim, found->second);
            return &found->second;
        }

        // Late art (PLAN.md section 3.3): an art draw whose frame was still decoding showed its 1x
        // pixels enlarged; it is replayed when the frame is ready, onto the pixels it drew that no
        // later draw has claimed and whose 1x value is unchanged.
        struct s_late_pixel {
            int32_t offset;
            uint8_t value;
            int8_t level;
        };
        struct s_late {
            s_draw draw;
            uint32_t generation;
            uint32_t published; // publish count when drawn
            const void* target;
            std::vector<s_late_pixel> pixels;
        };
        std::vector<s_late> late_draws;
        bool replaying;
        // Stats window (ANTARES_HD_STATS): replayed entries, upgraded and skipped pixels.
        uint32_t late_replayed, late_upgraded, late_drawn_over, late_changed, late_no_plane;
        uint32_t late_on_off, late_on_back, late_on_image, late_twins;

        bool Marked(const s_plane& plane, const std::vector<uint32_t>& mark, int32_t x, int32_t y) {
            return x >= 0 && y >= 0 && x < plane.width && y < plane.height && mark[(size_t)y * plane.width + x] == generation;
        }

        // Renders the touched pixels of one plane.
        void Render_Plane(s_plane& plane, const void* target, const uint8_t* pixels) {
            Sync_Glass_Lut();
            const s_draw& draw = current_draw;
            // Glyph frames are decoded on the spot when first used (glyphs.cpp), so they are ready
            // once they exist; sprite frames come from the decoding workers.
            const bool art = draw.source == SOURCE_ART && draw.frame != nullptr && !draw.waiting &&
                (draw.glyph || Frame_Ready(draw.frame));
            const bool late = draw.source == SOURCE_ART && draw.frame != nullptr && !art && !replaying;
            s_late* record = nullptr;
            if (late) {
                if (late_draws.size() >= 4096) {
                    late_draws.erase(late_draws.begin(), late_draws.begin() + 1024);
                }
                late_draws.push_back({draw, generation, publish_count, target, {}});
                record = &late_draws.back();
            }
            const s_plane* image = art ? nullptr : Image_Source();
            if (plane.row_dirty.empty() && plane.width >= 40) {
                static const bool check = antares::Get_Env("TWIN_CHECK") != nullptr;
                static uint32_t logged;
                if (check && logged++ < 20) {
                    size_t count = 0;
                    for (const s_touch& t : touched) {
                        count += t.plane == &plane ? 1 : 0;
                    }
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin check: draw into image %p %dx%d: %zu pixels, source %d art %d waiting %d frame %dx%d at %d,%d (%s)", target, plane.width, plane.height, count, (int)draw.source, art ? 1 : 0, draw.waiting ? 1 : 0, draw.frame != nullptr ? draw.frame->width : 0, draw.frame != nullptr ? draw.frame->height : 0, draw.x, draw.y, draw.name.c_str());
                }
            }
            std::vector<uint32_t>& mark = Marks(plane);
            int32_t x0 = plane.width;
            int32_t y0 = plane.height;
            int32_t x1 = -1;
            int32_t y1 = -1;
            for (const s_touch& touch : touched) {
                if (touch.plane == &plane) {
                    mark[(size_t)touch.offset] = generation;
                }
            }
            // Keyed by render pass, not by draw generation: a late-art replay renders again under
            // the original draw's generation.
            static uint32_t render_pass;
            if (++render_pass == 0) {
                render_pass = 1;
            }
            std::vector<uint32_t>& done = Done(plane);
            for (const s_touch& touch : touched) {
                if (touch.plane != &plane || done[(size_t)touch.offset] == render_pass) {
                    continue; // a pixel reported twice renders once (glass must not tint twice)
                }
                done[(size_t)touch.offset] = render_pass;
                const int32_t x = touch.offset % plane.width;
                const int32_t y = touch.offset / plane.width;
                if (probe_x >= 0 && x == probe_x && y == probe_y) {
                    char what[160];
                    snprintf(what, sizeof(what), "%s (anim %p at %d,%d remapped=%d shade=%d glyph=%d)", art ? "draw art" : image != nullptr ? "draw image"
                            : touch.level >= 0                                                                                              ? "draw glass"
                                                                                                                                            : "draw",
                        draw.anim,
                        draw.x,
                        draw.y,
                        draw.remapped ? 1 : 0,
                        draw.shade.empty() ? 0 : 1,
                        draw.glyph ? 1 : 0);
                    Probe(plane, x, y, what, pixels[touch.offset]);
                }
                x0 = SDL_min(x0, x);
                y0 = SDL_min(y0, y);
                x1 = SDL_max(x1, x);
                y1 = SDL_max(y1, y);
                if (art) {
                    // Inside the sprite the art goes over the pixel's own index; at its edges over
                    // what is there, so the art's soft outline shows.
                    const bool interior = touch.level < 0 && Marked(plane, mark, x - 1, y) && Marked(plane, mark, x + 1, y) &&
                        Marked(plane, mark, x, y - 1) && Marked(plane, mark, x, y + 1);
                    // Glass in the art tints what was there, so it must not be pre-filled with the
                    // pixel the 1x glass produced (the glass would apply twice).
                    if (interior && !Art_Is_Glass(x, y)) {
                        Fill_Block_Index(plane, x, y, pixels[touch.offset]);
                    }
                    Apply_Art_Block(plane, x, y, false);
                } else if (touch.level >= 0) {
                    Glass_Block(plane, x, y, touch.level);
                } else if (image == nullptr || !Image_Block(plane, x, y, *image)) {
                    Fill_Block_Index(plane, x, y, pixels[touch.offset]);
                    Mark_Row(plane, y);
                }
                if (record != nullptr) {
                    record->pixels.push_back({touch.offset, pixels[touch.offset], touch.level});
                }
                Accept(target, x, y);
            }
            if (!art || x1 < 0 || plane.row_dirty.empty() || replaying) {
                return; // images get no fringe: their transparent pixels are never drawn
            }
            // The art's soft edge past the 1x outline, over untouched neighbours, inside the clip.
            const int32_t clip_x1 = SDL_max(0, draw.clip_x1);
            const int32_t clip_y1 = SDL_max(0, draw.clip_y1);
            const int32_t clip_x2 = SDL_min(plane.width - 1, draw.clip_x2);
            const int32_t clip_y2 = SDL_min(plane.height - 1, draw.clip_y2);
            for (int32_t y = SDL_max(y0 - 1, clip_y1); y <= SDL_min(y1 + 1, clip_y2); ++y) {
                for (int32_t x = SDL_max(x0 - 1, clip_x1); x <= SDL_min(x1 + 1, clip_x2); ++x) {
                    if (Marked(plane, mark, x, y)) {
                        continue;
                    }
                    if (Marked(plane, mark, x - 1, y) || Marked(plane, mark, x + 1, y) || Marked(plane, mark, x, y - 1) ||
                        Marked(plane, mark, x, y + 1)) {
                        Apply_Art_Block(plane, x, y, true);
                    }
                }
            }
        }

        void Touch(const void* target, int32_t offset, int32_t level) {
            const uint8_t* pixels = nullptr;
            int32_t width = 0;
            int32_t height = 0;
            s_plane* plane = Resolve_Target(target, &pixels, &width, &height);
            if (plane == nullptr || offset < 0 || offset >= width * height) {
                return;
            }
            if (plane->px.empty()) {
                Fill_From_1x(*plane, pixels); // an image's first write: start from its 1x pixels
            }
            touched.push_back({plane, target, pixels, offset, (int8_t)level});
        }

    } // namespace

    void Replay_Late_Art() {
        if (late_draws.empty()) {
            return;
        }
        const s_draw saved = current_draw;
        size_t kept = 0;
        for (size_t i = 0; i < late_draws.size(); ++i) {
            s_late& entry = late_draws[i];
            const uint8_t state = entry.draw.frame->load.load(std::memory_order_acquire);
            if (state != LOAD_READY) {
                // Still decoding (or evicted and queued again): keep it a while.
                if (state != LOAD_FAILED && publish_count - entry.published < 600) {
                    if (kept != i) {
                        late_draws[kept] = std::move(entry);
                    }
                    kept++;
                }
                continue;
            }
            const uint8_t* pixels = nullptr;
            int32_t width = 0;
            int32_t height = 0;
            s_plane* plane = Resolve_Target(entry.target, &pixels, &width, &height);
            if (plane == nullptr || plane->px.empty() || width != plane->width) {
                late_no_plane++;
                continue;
            }
            late_replayed++;
            (plane == &plane_off ? late_on_off : plane == &plane_back ? late_on_back
                                                                      : late_on_image)++;
            std::vector<uint32_t>& mark = Marks(*plane);
            // The other page may hold the same plain blocks (screens copy the finished page
            // across and restore from it): they get the art too.
            s_plane* other = plane == &plane_off ? &plane_back : plane == &plane_back ? &plane_off
                                                                                      : nullptr;
            const uint8_t* other_pixels = other == &plane_off ? video::g_page_off.pixels : other == &plane_back ? video::g_page_back.pixels
                                                                                                                : nullptr;
            current_draw = entry.draw;
            current_draw.waiting = false;
            replaying = true;
            const uint32_t saved_generation = generation;
            generation = entry.generation;
            touched.clear();
            // The other page is judged on its own: screens copy the finished page across and
            // restore from it, so it may still hold the plain blocks where this page was drawn over.
            std::vector<s_touch> other_touched;
            for (const s_late_pixel& pixel : entry.pixels) {
                if (mark[(size_t)pixel.offset] != entry.generation) {
                    late_drawn_over++;
                } else if (pixels[pixel.offset] != pixel.value) {
                    late_changed++;
                } else {
                    late_upgraded++;
                    touched.push_back({plane, entry.target, pixels, pixel.offset, pixel.level});
                }
                if (other != nullptr && !other->px.empty() && other_pixels[pixel.offset] == pixel.value && pixel.level < 0) {
                    const s_px* block = Block(*other, pixel.offset % width, pixel.offset / width);
                    const size_t stride = (size_t)width * S;
                    const uint32_t plain_mask = Index_Px(pixel.value).mask;
                    bool plain = true;
                    for (int32_t sy = 0; sy < S && plain; ++sy) {
                        for (int32_t sx = 0; sx < S && plain; ++sx) {
                            plain = block[sy * stride + sx].mask == plain_mask;
                        }
                    }
                    if (plain) {
                        other_touched.push_back({other, other_pixels, other_pixels, pixel.offset, pixel.level});
                    }
                }
            }
            if (!touched.empty()) {
                Render_Plane(*plane, entry.target, pixels);
            }
            touched = std::move(other_touched);
            late_twins += (uint32_t)touched.size();
            if (!touched.empty()) {
                Render_Plane(*other, other_pixels, other_pixels);
            }
            touched.clear();
            generation = saved_generation;
            replaying = false;
        }
        late_draws.resize(kept);
        current_draw = saved;
    }

    size_t Late_Count() {
        return late_draws.size();
    }

    std::string Late_Stats() {
        char text[256];
        snprintf(text, sizeof(text), "late art: %u draws replayed (off %u, back %u, images %u; %u pixels, %u copied to the other page; %u drawn over, %u changed), %u without plane", late_replayed, late_on_off, late_on_back, late_on_image, late_upgraded, late_twins, late_drawn_over, late_changed, late_no_plane);
        late_replayed = late_upgraded = late_drawn_over = late_changed = late_no_plane = 0;
        late_on_off = late_on_back = late_on_image = late_twins = 0;
        return text;
    }

    void Reset_Late_Art() {
        late_draws.clear();
    }

    void Touch_Page(const uint8_t* page, const uint8_t* dst, int32_t count, int32_t level) {
        for (int32_t i = 0; i < count; ++i) {
            Touch(page, (int32_t)(dst - page) + i, level);
        }
    }

    void Touch_Image(const void* header, int32_t index) {
        Touch(header, index, -1);
    }

    void End_Draw() {
        if (touched.empty()) {
            return;
        }
        // Group by plane (a draw writes one target, but keep it general).
        while (!touched.empty()) {
            s_plane* plane = touched.front().plane;
            const void* target = touched.front().target;
            const uint8_t* pixels = touched.front().pixels;
            Render_Plane(*plane, target, pixels);
            touched.erase(std::remove_if(touched.begin(), touched.end(), [plane](const s_touch& t) { return t.plane == plane; }),
                touched.end());
        }
        if (++generation == 0) {
            generation = 1;
        }
    }

    void Recolor_Image(const void* header, int32_t first, int32_t count, const uint8_t* map) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Resolve_Target(header, &pixels, &width, &height);
        if (plane == nullptr || map == nullptr || count <= 0) {
            return;
        }
        if (!plane->px.empty()) {
            // Index-bound pixels take the same map as the 1x pixels (pixel-tests.md: index 0 is
            // never rewritten); full-colour pixels keep their colour.
            const auto recolor = [&](uint32_t index) {
                return index != 0 && (int32_t)index >= first && (int32_t)index < first + count ? (uint32_t)map[index - first] : index;
            };
            for (s_px& px : plane->px) {
                if (Kind(px) != KIND_INDEX) {
                    continue;
                }
                const uint32_t fg = recolor(px.mask & 0xFF);
                const uint32_t bg = (px.mask & MASK_CLEAR) != 0 ? (px.mask >> 8) & 0xFF : recolor((px.mask >> 8) & 0xFF);
                px.mask = (px.mask & ~0xFFFFu) | (bg << 8) | fg;
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Copy_Image(const void* dst, const void* src) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* to = Resolve_Target(dst, &pixels, &width, &height);
        if (to == nullptr || dst == src) {
            return;
        }
        int32_t src_width = 0;
        int32_t src_height = 0;
        const uint8_t* src_pixels = nullptr;
        const s_plane* from = Resolve_Target(src, &src_pixels, &src_width, &src_height);
        if (from != nullptr && !from->px.empty() && src_width == width && src_height == height) {
            to->px = from->px;
        } else {
            to->px.clear(); // stands for its 1x pixels
        }
        Accept_Rect(dst, 0, 0, width, height);
    }

    void Glass_Image(const void* header, int32_t level) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Resolve_Target(header, &pixels, &width, &height);
        if (plane == nullptr) {
            return;
        }
        if (!plane->px.empty()) {
            Sync_Glass_Lut();
            // bitmap::Tint_Bitmap_Pixels tints the non-zero pixels through the glass table.
            const uint8_t* table = video::glass_remap_colors[level & 15];
            for (s_px& px : plane->px) {
                if (Kind(px) == KIND_INDEX) {
                    const uint32_t fg = px.mask & 0xFF;
                    const uint32_t bg = (px.mask >> 8) & 0xFF;
                    const uint32_t new_fg = fg != 0 ? table[fg] : 0u;
                    const uint32_t new_bg = (px.mask & MASK_CLEAR) != 0 || bg == 0 ? bg : table[bg];
                    px.mask = (px.mask & ~0xFFFFu) | (new_bg << 8) | new_fg;
                } else if (((px.base >> 24) & 0xFF) != 0) {
                    px.base = (px.base & 0xFF000000u) | Glass_Through_Table(px.base & 0xFFFFFFu, level);
                }
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Flip_Image(const void* dst, const void* src, bool vertical) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* to = Resolve_Target(dst, &pixels, &width, &height);
        if (to == nullptr) {
            return;
        }
        const uint8_t* src_pixels = nullptr;
        int32_t src_width = 0;
        int32_t src_height = 0;
        s_plane* from = Resolve_Target(src, &src_pixels, &src_width, &src_height);
        if (from == nullptr || from->px.empty() || src_width != width || src_height != height) {
            to->px.clear(); // stands for its (mirrored) 1x pixels
            Accept_Rect(dst, 0, 0, width, height);
            return;
        }
        // Mirrored copies: combat ships facing left or down are their right-facing frames flipped.
        const int32_t hw = width * S;
        const int32_t hh = height * S;
        std::vector<s_px> mirrored((size_t)hw * hh);
        for (int32_t y = 0; y < hh; ++y) {
            const s_px* row = &from->px[(size_t)(vertical ? hh - 1 - y : y) * hw];
            s_px* out = &mirrored[(size_t)y * hw];
            if (vertical) {
                std::copy(row, row + hw, out);
            } else {
                std::reverse_copy(row, row + hw, out);
            }
        }
        to->px = std::move(mirrored);
        Accept_Rect(dst, 0, 0, width, height);
    }

    void Overwrite_Rect(const uint8_t* page, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Resolve_Target(page, &pixels, &width, &height);
        if (plane == nullptr || plane->px.empty()) {
            return;
        }
        x1 = SDL_max(x1, 0);
        y1 = SDL_max(y1, 0);
        x2 = SDL_min(x2, width - 1);
        y2 = SDL_min(y2, height - 1);
        for (int32_t y = y1; y <= y2; ++y) {
            for (int32_t x = x1; x <= x2; ++x) {
                Fill_Block_Index(*plane, x, y, pixels[(size_t)y * width + x]);
            }
            if (y1 <= y2 && x1 <= x2) {
                Mark_Row(*plane, y);
            }
        }
        Accept_Rect(page, x1, y1, x2 + 1, y2 + 1);
    }

    void Tint_Rect(const uint8_t* page, int32_t x, int32_t y, int32_t width, int32_t height, int32_t level) {
        Sync_Glass_Lut();
        const uint8_t* pixels = nullptr;
        int32_t page_width = 0;
        int32_t page_height = 0;
        s_plane* plane = Resolve_Target(page, &pixels, &page_width, &page_height);
        if (plane == nullptr || plane->px.empty()) {
            return;
        }
        const int32_t x0 = SDL_max(x, 0);
        const int32_t y0 = SDL_max(y, 0);
        const int32_t x1 = SDL_min(x + width, page_width);
        const int32_t y1 = SDL_min(y + height, page_height);
        if (probe_x >= x0 && probe_x < x1 && probe_y >= y0 && probe_y < y1) {
            Probe(*plane, probe_x, probe_y, "tint rect", level);
        }
        for (int32_t py = y0; py < y1; ++py) {
            for (int32_t px = x0; px < x1; ++px) {
                Glass_Block(*plane, px, py, level);
            }
        }
        Accept_Rect(page, x0, y0, x1, y1);
    }

    void Copy_Page(const uint8_t* src_page, const uint8_t* dst_page) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* src = Resolve_Target(src_page, &pixels, &width, &height);
        s_plane* dst = Resolve_Target(dst_page, &pixels, &width, &height);
        if (src == nullptr || dst == nullptr || src == dst || src->px.empty()) {
            return;
        }
        if (probe_x >= 0) {
            Probe(*dst, probe_x, probe_y, "copy page", -1);
        }
        dst->px = src->px;
        std::fill(dst->row_dirty.begin(), dst->row_dirty.end(), (uint8_t)1);
        Accept_Rect(dst_page, 0, 0, width, height);
    }

    void Copy_Page_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* src = Resolve_Target(src_page, &pixels, &width, &height);
        s_plane* dst = Resolve_Target(dst_page, &pixels, &width, &height);
        if (src == nullptr || dst == nullptr || src == dst || src->px.empty() || count <= 0 || offset < 0 ||
            offset + count > width * height) {
            return;
        }
        const size_t stride = (size_t)width * S;
        while (count > 0) {
            const int32_t x = offset % width;
            const int32_t y = offset / width;
            const int32_t run = SDL_min(count, width - x);
            for (int32_t sy = 0; sy < S; ++sy) {
                const size_t at = ((size_t)y * S + sy) * stride + (size_t)x * S;
                memcpy(&dst->px[at], &src->px[at], (size_t)run * S * sizeof(s_px));
            }
            Mark_Row(*dst, y);
            if (probe_x >= 0 && probe_y == y && probe_x >= x && probe_x < x + run) {
                Probe(*dst, probe_x, y, "copy span", -1);
            }
            Accept_Rect(dst_page, x, y, x + run, y + 1);
            offset += run;
            count -= run;
        }
    }

} // namespace render::twin
