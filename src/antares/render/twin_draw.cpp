#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include "antares/render/art_format.h"
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
        // Replaying a late-art draw (Replay_Late_Art): its glass was already applied when it was
        // first drawn, through the game's table, and must not tint the block a second time.
        bool replaying;

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

        uint32_t Through_Table(uint32_t rgb, const uint8_t* table);

        uint32_t Glass_Through_Table(uint32_t rgb, int32_t level) {
            return Through_Table(rgb, video::glass_remap_colors[level & 15]);
        }

        // A full-colour pixel through an index table: its nearest index goes through the table and
        // the pixel keeps its brightness relative to that index.
        uint32_t Through_Table(uint32_t rgb, const uint8_t* table) {
            if (nearest_lut.empty()) {
                Sync_Glass_Lut();
            }
            const uint8_t index = Nearest_Index(rgb);
            const uint32_t through = Base_Rgb(table[index]);
            const uint32_t base_luma = SDL_max(Luma(Base_Rgb(index)), 1u);
            const uint32_t ratio = SDL_clamp(Luma(rgb) * 256u / base_luma, 128u, 512u); // keep detail, within reason
            uint32_t out = 0;
            for (int32_t shift = 0; shift <= 16; shift += 8) {
                out |= SDL_min(255u, ((through >> shift) & 0xFF) * ratio / 256u) << shift;
            }
            return out;
        }

        // A full-colour pixel painted as index `painted` (its mask), shown as index `target`: the
        // target's colour at the pixel's brightness relative to the painted index's colour.
        uint32_t Shade_Index(uint32_t rgb, uint8_t painted, uint8_t target) {
            const uint32_t through = Base_Rgb(target);
            const uint32_t base_luma = SDL_max(Luma(Base_Rgb(painted)), 1u);
            const uint32_t ratio = SDL_clamp(Luma(rgb) * 256u / base_luma, 64u, 768u);
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
        // `in_image`: drawn into an image, glass keeps its level index, as the 1x pixel does; it
        // tints the page when the image is drawn there (Stamp_Glass).
        s_px Apply_Art(const s_px& dest, int32_t x, int32_t y, int32_t sx, int32_t sy, bool fringe, bool in_image) {
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
                if (frame->mask != nullptr && frame->mask[i] != 0) {
                    // D20 mask image (art::Mask_Meaning): glass tints what is under it, as the
                    // game's glass does; a shade follows its index through the draw's remap
                    // (player colours, recolours) and keeps the art's detail as brightness
                    // relative to that index's colour. Resolved here, at draw time: later palette
                    // changes reach it as whole-screen fades only.
                    const art::s_mask_meaning meaning = art::Mask_Meaning(frame->mask[i], draw.glassed);
                    if (meaning.kind == art::MASK_GLASS) {
                        if (alpha < 128 || fringe || replaying) {
                            return dest;
                        }
                        return in_image ? Index_Px((uint8_t)(0xF0 + meaning.value)) : Glass_Over(dest, meaning.value);
                    }
                    const uint8_t target = draw.remapped ? draw.remap[meaning.value] : meaning.value;
                    rgb = Shade_Index(rgb, meaning.value, target);
                }
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
                if (alpha < 128 || fringe || replaying) {
                    return dest;
                }
                return in_image ? Index_Px(index) : Glass_Over(dest, index - 0xF0);
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
            if (!draw.glassed || (frame->index == nullptr && frame->mask == nullptr)) {
                return false;
            }
            const int32_t lx = (x - draw.x - frame->origin_x) * S + S / 2;
            const int32_t ly = (y - draw.y - frame->origin_y) * S + S / 2;
            if (lx < 0 || ly < 0 || lx >= frame->width || ly >= frame->height) {
                return false;
            }
            const size_t i = (size_t)ly * frame->width + lx;
            // Indexed art: a glass index; full-colour art: a glass value in its mask (D20).
            const uint8_t value = frame->index != nullptr ? frame->index[i] : frame->mask[i];
            return frame->alpha[i] != 0 && value >= 0xF0;
        }

        // Whether the draw's art covers any of 1x pixel (x, y).
        bool Art_Covers(int32_t x, int32_t y) {
            const s_draw& draw = current_draw;
            const s_hd_frame* frame = draw.frame;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    const int32_t lx = (x - draw.x - frame->origin_x) * S + sx;
                    const int32_t ly = (y - draw.y - frame->origin_y) * S + sy;
                    if (lx >= 0 && ly >= 0 && lx < frame->width && ly < frame->height && frame->alpha[(size_t)ly * frame->width + lx] != 0) {
                        return true;
                    }
                }
            }
            return false;
        }

        void Clear_Block(s_plane& plane, int32_t x, int32_t y) {
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                std::fill(block + sy * stride, block + sy * stride + S, Clear_Px());
            }
        }

        void Apply_Art_Block(s_plane& plane, int32_t x, int32_t y, bool fringe) {
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    block[sy * stride + sx] = Apply_Art(block[sy * stride + sx], x, y, sx, sy, fringe, plane.row_dirty.empty());
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
                            // Geometry drawn in the image (a stroke, a shield) stays geometry where
                            // it shows the page under it: parity counts it apart (D21).
                            out.mask |= px.mask & MASK_STROKE;
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
        // Stats window (ANTARES_HD_STATS): replayed entries, upgraded and skipped pixels.
        uint32_t late_replayed, late_upgraded, late_drawn_over, late_changed, late_no_plane;
        uint32_t late_on_off, late_on_back, late_on_image, late_twins;

        bool Marked(const s_plane& plane, const std::vector<uint32_t>& mark, int32_t x, int32_t y) {
            return x >= 0 && y >= 0 && x < plane.width && y < plane.height && mark[(size_t)y * plane.width + x] == generation;
        }

        // D21: a diagonal line as a stroke one 1x pixel wide through the centres of its end pixels,
        // with coverage from each sub-pixel's distance to the segment. Its colour at each place is
        // the 1x pixel the line wrote there (the nearest touched pixel to the closest point of the
        // segment), so ramps and patterns stay. Drawn inside the clip window, over what is there.
        void Render_Stroke(s_plane& plane, const void* target, const uint8_t* pixels) {
            const s_draw& draw = current_draw;
            std::unordered_map<int32_t, uint8_t> values;
            int32_t bx0 = plane.width;
            int32_t by0 = plane.height;
            int32_t bx1 = -1;
            int32_t by1 = -1;
            for (const s_touch& touch : touched) {
                if (touch.plane != &plane) {
                    continue;
                }
                const int32_t x = touch.offset % plane.width;
                const int32_t y = touch.offset / plane.width;
                values[touch.offset] = pixels[touch.offset];
                bx0 = SDL_min(bx0, x);
                by0 = SDL_min(by0, y);
                bx1 = SDL_max(bx1, x);
                by1 = SDL_max(by1, y);
            }
            if (bx1 < 0) {
                return;
            }
            const float half = S / 2.0f;
            const float ax = (float)draw.line_x1 * S + half;
            const float ay = (float)draw.line_y1 * S + half;
            const float dx = (float)(draw.line_x2 - draw.line_x1) * S;
            const float dy = (float)(draw.line_y2 - draw.line_y1) * S;
            const float length2 = dx * dx + dy * dy;
            const int32_t clip_x1 = SDL_max(0, draw.clip_x1);
            const int32_t clip_y1 = SDL_max(0, draw.clip_y1);
            const int32_t clip_x2 = SDL_min(plane.width - 1, draw.clip_x2);
            const int32_t clip_y2 = SDL_min(plane.height - 1, draw.clip_y2);
            const size_t stride = (size_t)plane.width * S;
            // Only the blocks beside the line's own pixels can be reached by a stroke one pixel wide
            // (a long diagonal's bounding box is mostly empty: the bombardment screen draws ~28 a frame).
            const int32_t box_w = bx1 - bx0 + 3;
            const int32_t box_h = by1 - by0 + 3;
            std::vector<uint8_t> near((size_t)box_w * box_h, 0);
            for (const auto& item : values) {
                const int32_t x = item.first % plane.width;
                const int32_t y = item.first / plane.width;
                for (int32_t ny = y - 1; ny <= y + 1; ++ny) {
                    for (int32_t nx = x - 1; nx <= x + 1; ++nx) {
                        near[(size_t)(ny - by0 + 1) * box_w + (nx - bx0 + 1)] = 1;
                    }
                }
            }
            for (int32_t y = SDL_max(by0 - 1, clip_y1); y <= SDL_min(by1 + 1, clip_y2); ++y) {
                bool row_changed = false;
                for (int32_t x = SDL_max(bx0 - 1, clip_x1); x <= SDL_min(bx1 + 1, clip_x2); ++x) {
                    if (near[(size_t)(y - by0 + 1) * box_w + (x - bx0 + 1)] == 0) {
                        continue;
                    }
                    s_px* block = Block(plane, x, y);
                    for (int32_t sy = 0; sy < S; ++sy) {
                        for (int32_t sx = 0; sx < S; ++sx) {
                            const float px = (float)(x * S + sx) + 0.5f;
                            const float py = (float)(y * S + sy) + 0.5f;
                            float t = length2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / length2 : 0.0f;
                            t = SDL_clamp(t, 0.0f, 1.0f);
                            const float qx = ax + t * dx;
                            const float qy = ay + t * dy;
                            const float distance = SDL_sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
                            const float cover = SDL_clamp(half + 0.5f - distance, 0.0f, 1.0f);
                            if (cover <= 0.0f) {
                                continue;
                            }
                            // The line's colour at the closest point: the touched pixel there, or the
                            // nearest touched neighbour.
                            const int32_t cx = (int32_t)(qx / S);
                            const int32_t cy = (int32_t)(qy / S);
                            int32_t value = -1;
                            int32_t best = 1 << 30;
                            for (int32_t ny = cy - 1; ny <= cy + 1; ++ny) {
                                for (int32_t nx = cx - 1; nx <= cx + 1; ++nx) {
                                    if (nx < 0 || ny < 0 || nx >= plane.width || ny >= plane.height) {
                                        continue;
                                    }
                                    const auto found = values.find(ny * plane.width + nx);
                                    const int32_t d = (nx - cx) * (nx - cx) + (ny - cy) * (ny - cy);
                                    if (found != values.end() && d < best) {
                                        best = d;
                                        value = found->second;
                                    }
                                }
                            }
                            if (value < 0) {
                                continue; // a part of the segment the 1x line didn't draw (clipped)
                            }
                            s_px& out = block[sy * stride + sx];
                            const uint32_t alpha = (uint32_t)(cover * 255.0f + 0.5f);
                            if ((out.mask & MASK_STROKE) != 0 && Kind(out) == KIND_INDEX) {
                                // Another stroke's soft edge (a bolt's parallel strands), over the same
                                // background: the strands meet without a dark seam.
                                const uint32_t old_alpha = (out.mask >> 16) & 0xFF;
                                // Side by side, two strands split the pixel between them: coverages add.
                                const uint32_t combined = SDL_min(255u, old_alpha + alpha);
                                const uint32_t fg = alpha >= old_alpha ? (uint32_t)value : (out.mask & 0xFF);
                                out.mask = (out.mask & ~0x00FF00FFu) | fg | (combined << 16);
                            } else {
                                out = Over_Index(out, (uint8_t)value, alpha);
                                out.mask |= MASK_STROKE;
                            }
                            row_changed = true;
                        }
                    }
                }
                if (row_changed) {
                    Mark_Row(plane, y);
                }
            }
            // A pixel the 1x line wrote is the stroke's, also where the stroke doesn't reach it (the
            // stair-step corner keeps what was under it): parity counts it with the stroke.
            for (const auto& item : values) {
                const int32_t x = item.first % plane.width;
                const int32_t y = item.first / plane.width;
                s_px* block = Block(plane, x, y);
                for (int32_t sy = 0; sy < S; ++sy) {
                    for (int32_t sx = 0; sx < S; ++sx) {
                        block[sy * stride + sx].mask |= MASK_STROKE;
                    }
                }
                Accept(target, x, y);
            }
        }

        // Renders the touched pixels of one plane.
        void Render_Plane(s_plane& plane, const void* target, const uint8_t* pixels) {
            s_scoped_timer timer(TIMER_TWIN_RENDER);
            if (current_draw.source == SOURCE_LINE && current_draw.line_x1 != current_draw.line_x2 &&
                current_draw.line_y1 != current_draw.line_y2 && !plane.row_dirty.empty()) {
                Render_Stroke(plane, target, pixels);
                return;
            }
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
                    if (draw.art_outline) {
                        // The art is the outline: where it is empty, the 1x pixel (a wing the
                        // static art doesn't flap) doesn't show. A page keeps what was under it;
                        // an image becomes transparent there.
                        if (plane.row_dirty.empty()) {
                            Clear_Block(plane, x, y);
                        }
                    } else if ((interior && !Art_Is_Glass(x, y)) || (plane.row_dirty.empty() && draw.glassed && pixels[touch.offset] >= 0xF0) ||
                        (plane.row_dirty.empty() && !Art_Covers(x, y))) {
                        // In an image, a glass pixel is its level index (see Apply_Art), and a pixel
                        // the art doesn't reach (a keyframe pixel outside the drawn frame's art) is
                        // its 1x index: there is nothing under it to show.
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
            if (image != nullptr && image->geometry && x1 >= 0 && !replaying) {
                // The image's geometry past its 1x pixels (a shield's round edge where the 1x rings
                // cut corners): drawn over the untouched pixels it reaches, inside the clip.
                const int32_t gx1 = SDL_max(SDL_max(0, draw.clip_x1), draw.x);
                const int32_t gy1 = SDL_max(SDL_max(0, draw.clip_y1), draw.y);
                const int32_t gx2 = SDL_min(SDL_min(plane.width - 1, draw.clip_x2), draw.x + image->width - 1);
                const int32_t gy2 = SDL_min(SDL_min(plane.height - 1, draw.clip_y2), draw.y + image->height - 1);
                const size_t src_stride = (size_t)image->width * S;
                for (int32_t y = gy1; y <= gy2; ++y) {
                    for (int32_t x = gx1; x <= gx2; ++x) {
                        if (Marked(plane, mark, x, y)) {
                            continue;
                        }
                        const s_px* src = &image->px[(size_t)(y - draw.y) * S * src_stride + (size_t)(x - draw.x) * S];
                        bool shows = false;
                        for (int32_t sy = 0; sy < S && !shows; ++sy) {
                            for (int32_t sx = 0; sx < S; ++sx) {
                                const s_px& px = src[sy * src_stride + sx];
                                if ((px.mask & MASK_STROKE) != 0 && !((px.mask & MASK_CLEAR) != 0 && ((px.mask >> 16) & 0xFF) == 0)) {
                                    shows = true;
                                    break;
                                }
                            }
                        }
                        if (shows) {
                            Image_Block(plane, x, y, *image);
                        }
                    }
                }
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
                if (pixel.level >= 0) {
                    continue; // glass: applied when first drawn
                }
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

    void Table_Rows(const uint8_t* page, int32_t first_row, const uint8_t* table) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Resolve_Target(page, &pixels, &width, &height);
        if (plane == nullptr || plane->px.empty() || table == nullptr) {
            return;
        }
        Sync_Glass_Lut();
        const size_t stride = (size_t)width * S;
        for (int32_t y = first_row; y < height; y += 2) {
            s_px* row = &plane->px[(size_t)y * S * stride];
            for (size_t i = 0; i < stride * S; ++i) {
                s_px& px = row[i];
                if (Kind(px) == KIND_INDEX) {
                    const uint32_t bg = (px.mask & MASK_CLEAR) != 0 ? (px.mask >> 8) & 0xFF : table[(px.mask >> 8) & 0xFF];
                    px.mask = (px.mask & ~0xFFFFu) | (bg << 8) | table[px.mask & 0xFF];
                } else {
                    px.base = (px.base & 0xFF000000u) | Through_Table(px.base & 0xFFFFFFu, table);
                }
            }
            Mark_Row(*plane, y);
            Accept_Rect(page, 0, y, width, y + 1);
        }
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

    // ---------------------------------------------------------------- bitmap twins (WP1.4)
    //
    // They run after the 1x operation, inside its scope: the scope's snapshot (Scope_Before) holds
    // the image's 1x pixels before it, so each twin sees which pixels changed and how, and does the
    // same to the plane. Then the image is accepted whole.

    namespace {

        bool Covered(const s_px& px) {
            if (Kind(px) == KIND_INDEX) {
                return (px.mask & MASK_CLEAR) == 0 || ((px.mask >> 16) & 0xFF) >= 128;
            }
            return ((px.base >> 24) & 0xFF) >= 128;
        }

        // A plane pixel through an index table: bound indices take it (a transparent background
        // stays transparent), full-colour pixels go through their nearest index.
        s_px Px_Through_Table(const s_px& px, const uint8_t* table) {
            if (Kind(px) == KIND_INDEX) {
                const uint32_t bg = (px.mask & MASK_CLEAR) != 0 ? (px.mask >> 8) & 0xFF : table[(px.mask >> 8) & 0xFF];
                return {px.base, (px.mask & ~0xFFFFu) | (bg << 8) | table[px.mask & 0xFF]};
            }
            return {(px.base & 0xFF000000u) | Through_Table(px.base & 0xFFFFFFu, table), px.mask};
        }

        void Block_Through_Table(s_plane& plane, int32_t x, int32_t y, const uint8_t* table) {
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    block[sy * stride + sx] = Px_Through_Table(block[sy * stride + sx], table);
                }
            }
        }

        void Copy_Block(s_plane& to, int32_t x, int32_t y, const s_plane& from, int32_t fx, int32_t fy) {
            s_px* dst = Block(to, x, y);
            const s_px* src = &from.px[(size_t)fy * S * from.width * S + (size_t)fx * S];
            const size_t dst_stride = (size_t)to.width * S;
            const size_t src_stride = (size_t)from.width * S;
            for (int32_t sy = 0; sy < S; ++sy) {
                std::copy(src + sy * src_stride, src + sy * src_stride + S, dst + sy * dst_stride);
            }
        }

        // The image `header` with its plane and 1x pixels, or null when it has no plane pixels
        // (it stands for its 1x pixels, which the operation already changed).
        s_plane* Image_Plane(const void* header, const uint8_t** pixels, int32_t* width, int32_t* height) {
            s_plane* plane = Resolve_Target(header, pixels, width, height);
            return plane != nullptr && !plane->px.empty() ? plane : nullptr;
        }

    } // namespace

    void Sync_Cleared(const void* header) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &width, &height);
        const uint8_t* before = Scope_Before(header);
        if (plane != nullptr && before != nullptr) {
            for (int32_t i = 0; i < width * height; ++i) {
                if (pixels[i] == before[i]) {
                    continue;
                }
                // Masked out (0): transparent; anything else (not expected) bound to its index.
                if (pixels[i] == 0) {
                    Clear_Block(*plane, i % width, i / width);
                } else {
                    Fill_Block_Index(*plane, i % width, i / width, pixels[i]);
                }
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Vanish_Image(const void* dst, const void* src) {
        if (dst == src) {
            Sync_Cleared(dst);
            return;
        }
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* to = Resolve_Target(dst, &pixels, &width, &height);
        const uint8_t* src_pixels = nullptr;
        int32_t src_width = 0;
        int32_t src_height = 0;
        const s_plane* from = Image_Plane(src, &src_pixels, &src_width, &src_height);
        if (to == nullptr) {
            return;
        }
        if (from == nullptr || src_width != width || src_height != height) {
            to->px.clear(); // stands for its 1x pixels
        } else {
            to->px = from->px;
            for (int32_t i = 0; i < width * height; ++i) {
                if (pixels[i] == 0 && src_pixels[i] != 0) {
                    Clear_Block(*to, i % width, i / width);
                } else if (pixels[i] != src_pixels[i]) {
                    Fill_Block_Index(*to, i % width, i / width, pixels[i]);
                }
            }
        }
        Accept_Rect(dst, 0, 0, width, height);
    }

    void Table_Image(const void* header, const uint8_t* table) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &width, &height);
        if (plane != nullptr && table != nullptr) {
            Sync_Glass_Lut();
            for (s_px& px : plane->px) {
                px = Px_Through_Table(px, table);
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Brighten_Image(const void* header, int32_t dst_offset, const uint8_t* src, int32_t src_skip, int32_t dst_skip, int32_t width, int32_t height, const uint8_t* tables, int32_t start_color) {
        const uint8_t* pixels = nullptr;
        int32_t image_width = 0;
        int32_t image_height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &image_width, &image_height);
        const uint8_t* before = Scope_Before(header);
        if (plane != nullptr && before != nullptr && tables != nullptr && width > 0 && height > 0) {
            Sync_Glass_Lut();
            // The same walk as shear::Mask_Brighten_Bitmap_Pixels: where both pixels are set, the
            // destination goes through the table row the source pixel picks.
            int32_t d = dst_offset;
            for (int32_t row = 0; row < height; ++row) {
                for (int32_t col = 0; col < width; ++col, ++src, ++d) {
                    if (*src != 0 && d >= 0 && d < image_width * image_height && before[d] != 0) {
                        const uint8_t* table = tables + ((uint32_t)(uint8_t)(*src - start_color) << 8);
                        Block_Through_Table(*plane, d % image_width, d / image_width, table);
                    }
                }
                src += src_skip;
                d += dst_skip;
            }
        }
        Accept_Rect(header, 0, 0, image_width, image_height);
    }

    void Capture_Image(const void* header, const uint8_t* page, int32_t x1, int32_t y1, int32_t width, int32_t height) {
        const uint8_t* pixels = nullptr;
        int32_t image_width = 0;
        int32_t image_height = 0;
        s_plane* image = Resolve_Target(header, &pixels, &image_width, &image_height);
        const uint8_t* page_pixels = nullptr;
        int32_t page_width = 0;
        int32_t page_height = 0;
        const s_plane* from = Resolve_Target(page, &page_pixels, &page_width, &page_height);
        if (image == nullptr) {
            return;
        }
        if (from == nullptr || from->px.empty() || image_width != width || image_height != height || x1 < 0 || y1 < 0 ||
            x1 + width > page_width || y1 + height > page_height) {
            image->px.clear(); // stands for its 1x pixels
        } else {
            image->px.resize((size_t)width * height * S * S);
            for (int32_t y = 0; y < height; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    Copy_Block(*image, x, y, *from, x1 + x, y1 + y);
                }
            }
        }
        Accept_Rect(header, 0, 0, image_width, image_height);
    }

    void Scale_Image(const void* dst, const void* src) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* to = Resolve_Target(dst, &pixels, &width, &height);
        const uint8_t* src_pixels = nullptr;
        int32_t src_width = 0;
        int32_t src_height = 0;
        const s_plane* from = Image_Plane(src, &src_pixels, &src_width, &src_height);
        if (to == nullptr || dst == src) {
            return; // in place: the scope's fallback (the header was resized under it)
        }
        if (from == nullptr || width <= 0 || height <= 0) {
            to->px.clear();
        } else {
            // Nearest HD pixel of the source; a pixel bound to an index other than the 1x pixel the
            // scaler chose for its block takes that index (the scalers pick their own samples).
            const int32_t hw = width * S;
            const int32_t hh = height * S;
            const int32_t src_hw = src_width * S;
            const int32_t src_hh = src_height * S;
            std::vector<s_px> scaled((size_t)hw * hh);
            for (int32_t y = 0; y < hh; ++y) {
                const int32_t fy = SDL_min(src_hh - 1, (int32_t)(((int64_t)y * 2 + 1) * src_hh / (hh * 2)));
                const uint8_t* row = pixels + (size_t)(y / S) * width;
                for (int32_t x = 0; x < hw; ++x) {
                    const int32_t fx = SDL_min(src_hw - 1, (int32_t)(((int64_t)x * 2 + 1) * src_hw / (hw * 2)));
                    s_px px = from->px[(size_t)fy * src_hw + fx];
                    const uint8_t value = row[x / S];
                    if (Kind(px) == KIND_INDEX && (uint8_t)px.mask != value) {
                        px = value == 0 ? Clear_Px() : Index_Px(value);
                    } else if (Kind(px) != KIND_INDEX && value == 0) {
                        px = Clear_Px();
                    }
                    scaled[(size_t)y * hw + x] = px;
                }
            }
            to->px = std::move(scaled);
        }
        Accept_Rect(dst, 0, 0, width, height);
    }

    void Outline_Image(const void* header, uint8_t color, bool no_glass) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &width, &height);
        const uint8_t* before = Scope_Before(header);
        if (plane != nullptr && before != nullptr && width > 0) {
            // D17: the outline follows the art's own edge. A sub-pixel is solid when its 1x pixel is
            // (not 0, the outline colour or, without glass, a glass level) and the art covers it;
            // the outline is every other sub-pixel within one 1x pixel of a solid one along its row
            // or column, as the 1x passes mark the empty pixels beside solid ones. The 1x passes
            // cover width x width pixels (shear.cpp), so rows past min(width, height) are left.
            const int32_t rows = SDL_min(width, height);
            const int32_t hw = width * S;
            const int32_t hh = rows * S;
            auto solid_1x = [&](uint8_t v) {
                return v != 0 && v != color && !(no_glass && v >= 0xF0);
            };
            std::vector<uint8_t> solid((size_t)hw * hh, 0);
            for (int32_t y = 0; y < hh; ++y) {
                for (int32_t x = 0; x < hw; ++x) {
                    solid[(size_t)y * hw + x] = solid_1x(pixels[(size_t)(y / S) * width + x / S]) && Covered(plane->px[(size_t)y * hw + x]) ? 1 : 0;
                }
            }
            // Pixels the 1x outline wrote start transparent: only the band shows there.
            for (int32_t y = 0; y < rows; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    const size_t i = (size_t)y * width + x;
                    if (pixels[i] != before[i]) {
                        Clear_Block(*plane, x, y);
                    }
                }
            }
            // Distance to the nearest solid sub-pixel along rows, then columns (at most S).
            std::vector<uint8_t> near((size_t)hw * hh, 0);
            for (int32_t y = 0; y < hh; ++y) {
                const uint8_t* row = &solid[(size_t)y * hw];
                int32_t last = -1000;
                for (int32_t x = 0; x < hw; ++x) {
                    last = row[x] ? x : last;
                    near[(size_t)y * hw + x] |= x - last <= S ? 1 : 0;
                }
                last = 1000000;
                for (int32_t x = hw - 1; x >= 0; --x) {
                    last = row[x] ? x : last;
                    near[(size_t)y * hw + x] |= last - x <= S ? 1 : 0;
                }
            }
            for (int32_t x = 0; x < hw; ++x) {
                int32_t last = -1000;
                for (int32_t y = 0; y < hh; ++y) {
                    last = solid[(size_t)y * hw + x] ? y : last;
                    near[(size_t)y * hw + x] |= y - last <= S ? 1 : 0;
                }
                last = 1000000;
                for (int32_t y = hh - 1; y >= 0; --y) {
                    last = solid[(size_t)y * hw + x] ? y : last;
                    near[(size_t)y * hw + x] |= last - y <= S ? 1 : 0;
                }
            }
            const s_px band = Index_Px(color);
            for (size_t i = 0; i < near.size(); ++i) {
                if (near[i] != 0 && solid[i] == 0) {
                    plane->px[i] = band;
                }
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Line_Image(const uint8_t* page, const void* header, int32_t mid_x, int32_t mid_y, int32_t angle, int32_t ext_x1, int32_t ext_x2, int32_t img_x1, int32_t img_y1, int32_t img_y2, int32_t scale_percent, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        const uint8_t* pixels = nullptr;
        int32_t page_width = 0;
        int32_t page_height = 0;
        s_plane* plane = Resolve_Target(page, &pixels, &page_width, &page_height);
        const uint8_t* before = Scope_Before(page);
        const uint8_t* image_pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        const s_plane* image = Resolve_Target(header, &image_pixels, &width, &height);
        x1 = SDL_max(x1, 0);
        y1 = SDL_max(y1, 0);
        x2 = SDL_min(x2, page_width - 1);
        y2 = SDL_min(y2, page_height - 1);
        if (plane == nullptr || plane->px.empty() || before == nullptr || image_pixels == nullptr || scale_percent <= 0 || x1 > x2 ||
            y1 > y2) {
            return; // the scope's fallback binds what the 1x loop wrote
        }
        if (image != nullptr && image->px.empty()) {
            image = nullptr; // no plane: sample its 1x pixels
        }
        // The 1x loop maps page pixel (x, y) back into the image by rotating it by -angle about
        // (mid_x, mid_y) and undoing the stretch. Done here per sub-pixel, with page pixel x at
        // x + 0.5 and image pixel n at n + 0.5, so the beam's edges and texture follow the line
        // instead of the 640x480 grid (D21's tolerance: the pixels are marked MASK_STROKE).
        const double radians = -(double)angle * 3.14159265358979323846 / 180.0;
        const double c = SDL_cos(radians);
        const double sn = SDL_sin(radians);
        const size_t stride = (size_t)plane->width * S;
        const size_t image_stride = (size_t)width * S;
        for (int32_t y = y1; y <= y2; ++y) {
            bool row_changed = false;
            for (int32_t x = x1; x <= x2; ++x) {
                const size_t i = (size_t)y * page_width + x;
                const bool wrote = pixels[i] != before[i];
                s_px* block = Block(*plane, x, y);
                for (int32_t sy = 0; sy < S; ++sy) {
                    for (int32_t sx = 0; sx < S; ++sx) {
                        const double fx = x + (sx + 0.5) / S - 0.5 - mid_x;
                        const double fy = y + (sy + 0.5) / S - 0.5 - mid_y;
                        const double rx = fx * c - fy * sn + mid_x;
                        const double ry = fx * sn + fy * c + mid_y;
                        s_px& out = block[sy * stride + sx];
                        if (rx < ext_x1 - 0.5 || rx > ext_x2 + 0.5 || ry < img_y1 - 0.5 || ry > img_y2 + 0.5) {
                            if (wrote) {
                                out.mask |= MASK_STROKE; // the 1x footprint, outside the beam's true edge
                            }
                            continue;
                        }
                        const double col = (rx - mid_x) * 100.0 / scale_percent + mid_x - img_x1;
                        const double row = ry - img_y1;
                        const int32_t pcol = (int32_t)SDL_floor(col + 0.5);
                        const int32_t prow = (int32_t)SDL_floor(row + 0.5);
                        if (pcol < 0 || prow < 0 || pcol >= width || prow >= height || image_pixels[(size_t)prow * width + pcol] == 0) {
                            if (wrote) {
                                out.mask |= MASK_STROKE;
                            }
                            continue;
                        }
                        s_px px = Index_Px(image_pixels[(size_t)prow * width + pcol]);
                        if (image != nullptr) {
                            const int32_t hx = SDL_clamp((int32_t)SDL_floor((col + 0.5) * S), 0, width * S - 1);
                            const int32_t hy = SDL_clamp((int32_t)SDL_floor((row + 0.5) * S), 0, height * S - 1);
                            px = image->px[(size_t)hy * image_stride + hx];
                        }
                        if (Kind(px) == KIND_INDEX && (px.mask & MASK_CLEAR) != 0 && ((px.mask >> 16) & 0xFF) < 255) {
                            const uint32_t alpha = (px.mask >> 16) & 0xFF;
                            out = alpha != 0 ? Over_Index(out, (uint8_t)px.mask, alpha) : out;
                        } else if (Kind(px) != KIND_INDEX && ((px.base >> 24) & 0xFF) < 255) {
                            const uint32_t alpha = (px.base >> 24) & 0xFF;
                            out = alpha != 0 ? Over_Rgb(out, px.base & 0xFFFFFFu, alpha) : out;
                        } else {
                            out = px;
                        }
                        out.mask |= MASK_STROKE;
                        row_changed = true;
                    }
                }
                if (wrote) {
                    row_changed = true;
                }
            }
            if (row_changed) {
                Mark_Row(*plane, y);
            }
        }
        Accept_Rect(page, x1, y1, x2 + 1, y2 + 1);
    }

    void Jumble_Image(const void* dst, const void* src, uint32_t mask, uint32_t threshold, uint32_t noise_start, uint32_t toggle_after, const uint8_t* noise_table) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* to = Resolve_Target(dst, &pixels, &width, &height);
        if (to == nullptr) {
            return;
        }
        const bool in_place = dst == src;
        const uint8_t* src_pixels = nullptr;
        int32_t src_width = 0;
        int32_t src_height = 0;
        const s_plane* from = Image_Plane(src, &src_pixels, &src_width, &src_height);
        const uint8_t* before = in_place ? Scope_Before(dst) : src_pixels;
        const int32_t count = width * height - (int32_t)mask * 2;
        if (from == nullptr || before == nullptr || src_width != width || src_height != height || noise_table == nullptr || count <= 0) {
            Accept_Rect(dst, 0, 0, width, height);
            return; // no plane to move: the image stands for its 1x pixels (to->px as the copy left it)
        }
        // D16: the 1x call's own noise (start index, table, mask) replayed on block ids. Which
        // steps swap doesn't depend on the direction toggle, so the toggle at the start follows
        // from the one after and the number of swaps.
        uint32_t swaps = 0;
        for (int32_t i = 0, idx = (int32_t)(noise_start & 0xFF); i < count; ++i, idx = (idx + 1) & 0xFF) {
            swaps += noise_table[idx] < (threshold & 0xFF) ? 1 : 0;
        }
        uint32_t toggle = (swaps & 1) != 0 ? ~toggle_after : toggle_after;
        const int32_t total = width * height;
        std::vector<int32_t> source_ids(total);
        for (int32_t k = 0; k < total; ++k) {
            source_ids[k] = k;
        }
        std::vector<int32_t> ids = source_ids; // the destination starts as the source (copied)
        std::vector<int32_t>& reads = in_place ? ids : source_ids;
        for (int32_t i = 0, idx = (int32_t)(noise_start & 0xFF); i < count; ++i, idx = (idx + 1) & 0xFF) {
            const uint32_t noise = noise_table[idx];
            if (noise >= (threshold & 0xFF)) {
                continue;
            }
            int32_t offset = (int32_t)(noise & mask) + 1;
            if (toggle != 0) {
                offset = -offset;
            }
            toggle = ~toggle;
            const int32_t p = (int32_t)mask + i;
            if (p + offset < 0 || p + offset >= total) {
                continue;
            }
            const int32_t moved = reads[p];
            ids[p] = reads[p + offset];
            ids[p + offset] = moved;
        }
        const s_plane source = in_place ? *to : *from;
        to->px.resize((size_t)total * S * S);
        for (int32_t k = 0; k < total; ++k) {
            const int32_t id = ids[k];
            if (before[id] == pixels[k]) {
                Copy_Block(*to, k % width, k / width, source, id % width, id / width);
            } else {
                Fill_Block_Index(*to, k % width, k / width, pixels[k]); // the replay went astray
            }
        }
        Accept_Rect(dst, 0, 0, width, height);
    }

    void Map_Image_Rect(const void* header, int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t* table) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &width, &height);
        if (plane != nullptr && table != nullptr) {
            Sync_Glass_Lut();
            for (int32_t y = SDL_max(y0, 0); y < SDL_min(y1, height); ++y) {
                for (int32_t x = SDL_max(x0, 0); x < SDL_min(x1, width); ++x) {
                    Block_Through_Table(*plane, x, y, table);
                }
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

    void Plasma_Image(const void* header, const uint8_t* darken, int32_t darken_width, int32_t darken_height, int32_t darkness, const uint8_t* darken_map) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Image_Plane(header, &pixels, &width, &height);
        const uint8_t* before = Scope_Before(header);
        if (plane != nullptr && before != nullptr && darken != nullptr && darken_map != nullptr && darkness != 0) {
            Sync_Glass_Lut();
            // The same walk as CMBTDRW1::Plasma_Darken: each pixel goes through the darken map's
            // column for the plasma level at its place (ship_darken_map[pixel][level]).
            uint8_t tables[16][256];
            bool built[16] = {};
            for (int32_t y = 0; y < SDL_min(height, darken_height); ++y) {
                for (int32_t x = 0; x < SDL_min(width, darken_width); ++x) {
                    const uint32_t d = darken[(size_t)y * darken_width + x];
                    uint32_t level = darkness < 5 ? (d * (uint32_t)darkness) >> 2 : d + (uint32_t)darkness - 4;
                    level = SDL_min(level, 15u);
                    if (!built[level]) {
                        for (int32_t i = 0; i < 256; ++i) {
                            tables[level][i] = darken_map[i * 16 + level];
                        }
                        built[level] = true;
                    }
                    Block_Through_Table(*plane, x, y, tables[level]);
                }
            }
        }
        Accept_Rect(header, 0, 0, width, height);
    }

} // namespace render::twin
