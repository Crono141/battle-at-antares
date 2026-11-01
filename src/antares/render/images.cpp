#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include <algorithm>

namespace render {

    void Bitmap_Reset(const void* bitmap, int32_t width, int32_t height) {
        if (twins_on) {
            twin::Attach_Image(bitmap, width, height);
            return;
        }
        if (!Legacy() || bitmap == nullptr || width <= 0 || height <= 0) {
            return;
        }
        Flush_Pending();
        s_surface& image = images[bitmap];
        image.width = width;
        image.height = height;
        image.hd.clear();
        image.shadow.clear();
        image.mark.clear();
    }

    void Bitmap_Flip(const void* dst, const void* src, bool vertical) {
        if (twins_on) {
            twin::Flip_Image(dst, src, vertical);
            return;
        }
        if (!Legacy()) {
            return;
        }
        Flush_Pending();
        s_scoped_timer timer(TIMER_COPY);
        const auto from = images.find(src);
        auto to = images.find(dst);
        if (to == images.end()) {
            return;
        }
        s_surface& target = to->second;
        if (from == images.end() || from->second.hd.empty() || from->second.width != target.width ||
            from->second.height != target.height) {
            target.hd.clear();
            target.shadow.clear();
            target.mark.clear();
            return;
        }
        // Mirrored copies (combat ships facing left or down are their right-facing frames flipped):
        // the HD image is mirrored the same way, then replaces the target's.
        const s_surface& source = from->second;
        const int32_t w = source.width;
        const int32_t h = source.height;
        const int32_t hw = w * S;
        const int32_t hh = h * S;
        if (&source == &target) {
            // In place: swap mirrored pairs.
            if (vertical) {
                for (int32_t y = 0; y < hh / 2; ++y) {
                    std::swap_ranges(target.hd.begin() + (ptrdiff_t)y * hw, target.hd.begin() + (ptrdiff_t)(y + 1) * hw, target.hd.begin() + (ptrdiff_t)(hh - 1 - y) * hw);
                }
                for (int32_t y = 0; y < h / 2; ++y) {
                    std::swap_ranges(target.shadow.begin() + (ptrdiff_t)y * w, target.shadow.begin() + (ptrdiff_t)(y + 1) * w, target.shadow.begin() + (ptrdiff_t)(h - 1 - y) * w);
                }
            } else {
                for (int32_t y = 0; y < hh; ++y) {
                    std::reverse(target.hd.begin() + (ptrdiff_t)y * hw, target.hd.begin() + (ptrdiff_t)(y + 1) * hw);
                }
                for (int32_t y = 0; y < h; ++y) {
                    std::reverse(target.shadow.begin() + (ptrdiff_t)y * w, target.shadow.begin() + (ptrdiff_t)(y + 1) * w);
                }
            }
        } else {
            // Rows are copied whole into the target's existing buffers: this runs for every mirrored
            // combat ship on every frame.
            target.hd.resize(source.hd.size());
            target.shadow.resize(source.shadow.size());
            for (int32_t y = 0; y < hh; ++y) {
                const auto row = source.hd.begin() + (ptrdiff_t)(vertical ? hh - 1 - y : y) * hw;
                const auto out = target.hd.begin() + (ptrdiff_t)y * hw;
                if (vertical) {
                    std::copy(row, row + hw, out);
                } else {
                    std::reverse_copy(row, row + hw, out);
                }
            }
            for (int32_t y = 0; y < h; ++y) {
                const auto row = source.shadow.begin() + (ptrdiff_t)(vertical ? h - 1 - y : y) * w;
                const auto out = target.shadow.begin() + (ptrdiff_t)y * w;
                if (vertical) {
                    std::copy(row, row + w, out);
                } else {
                    std::reverse_copy(row, row + w, out);
                }
            }
        }
        target.mark.assign(target.shadow.size(), 0u);
    }

    void Bitmap_Copy(const void* dst, const void* src) {
        if (twins_on) {
            twin::Copy_Image(dst, src);
            return;
        }
        if (!Legacy() || dst == src) {
            return;
        }
        Flush_Pending();
        const auto from = images.find(src);
        auto to = images.find(dst);
        if (to == images.end()) {
            return;
        }
        s_surface& target = to->second;
        if (from == images.end() || from->second.hd.empty() || from->second.width != target.width ||
            from->second.height != target.height) {
            target.hd.clear();
            target.shadow.clear();
            target.mark.clear();
            return;
        }
        target.hd = from->second.hd;
        target.shadow = from->second.shadow;
        target.mark.assign(target.shadow.size(), 0u);
    }

    void Bitmap_Scale(const void* dst, const void* src, const uint8_t* dst_pixels, int32_t width, int32_t height) {
        if (!Legacy() || dst == src || dst_pixels == nullptr) {
            return;
        }
        Flush_Pending();
        const auto from = images.find(src);
        auto to = images.find(dst);
        if (from == images.end() || to == images.end() || from->second.hd.empty() || to->second.width != width ||
            to->second.height != height) {
            return;
        }
        const s_surface& source = from->second;
        s_surface& target = to->second;
        Ensure_Hd(target);
        const int32_t src_w = source.width * S;
        const int32_t src_h = source.height * S;
        const int32_t dst_w = width * S;
        const int32_t dst_h = height * S;
        // Box filter over the source HD pixels each target HD pixel covers: coverage is averaged,
        // the colour comes from the most covered source pixel.
        for (int32_t y = 0; y < dst_h; ++y) {
            const int32_t sy0 = y * src_h / dst_h;
            const int32_t sy1 = SDL_max(sy0 + 1, (y + 1) * src_h / dst_h);
            for (int32_t x = 0; x < dst_w; ++x) {
                const int32_t sx0 = x * src_w / dst_w;
                const int32_t sx1 = SDL_max(sx0 + 1, (x + 1) * src_w / dst_w);
                uint32_t coverage = 0;
                uint32_t samples = 0;
                uint32_t best = HD_CLEAR;
                uint32_t best_alpha = 0;
                for (int32_t sy = sy0; sy < sy1 && sy < src_h; ++sy) {
                    for (int32_t sx = sx0; sx < sx1 && sx < src_w; ++sx) {
                        const uint32_t pixel = source.hd[(size_t)sy * src_w + sx];
                        const uint32_t alpha = (pixel & HD_LITERAL) != 0 || (pixel & HD_CLEAR) == 0 ? 255u : (pixel >> 16) & 0xFF;
                        coverage += alpha;
                        samples++;
                        if (alpha > best_alpha) {
                            best_alpha = alpha;
                            best = pixel;
                        }
                    }
                }
                const uint32_t alpha = samples != 0 ? coverage / samples : 0;
                uint32_t out;
                if (alpha >= 255 && (best & HD_CLEAR) == 0) {
                    out = best;
                } else if ((best & HD_LITERAL) != 0) {
                    out = alpha >= 128 ? best : (HD_CLEAR | (alpha << 16));
                } else {
                    out = (best & 0xFFu) | (alpha << 16) | HD_CLEAR;
                }
                target.hd[(size_t)y * dst_w + x] = out;
            }
        }
        for (int32_t i = 0; i < width * height; ++i) {
            target.shadow[(size_t)i] = dst_pixels[i];
        }
    }

    void Bitmap_Glass(const void* bitmap, const uint8_t* pixels, int32_t level) {
        if (twins_on) {
            twin::Glass_Image(bitmap, level);
            return;
        }
        if (!Legacy() || pixels == nullptr) {
            return;
        }
        Flush_Pending();
        auto found = images.find(bitmap);
        if (found == images.end() || found->second.hd.empty()) {
            return;
        }
        s_surface& image = found->second;
        const uint8_t* table = video::glass_remap_colors[level & 15];
        const size_t stride = (size_t)image.width * S;
        for (int32_t i = 0; i < image.width * image.height; ++i) {
            const uint16_t old = image.shadow[(size_t)i];
            if (old == SHADOW_NONE) {
                continue;
            }
            if (table[old] != pixels[i]) {
                image.shadow[(size_t)i] = SHADOW_NONE;
                continue;
            }
            image.shadow[(size_t)i] = pixels[i];
            uint32_t* block = &image.hd[(size_t)(i / image.width) * S * stride + (size_t)(i % image.width) * S];
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    uint32_t& pixel = block[sy * stride + sx];
                    if ((pixel & HD_LITERAL) == 0 && (pixel & HD_CLEAR) != 0) {
                        // Partly covered over transparency: tint the colour, keep the coverage.
                        pixel = (pixel & ~0xFFu) | table[pixel & 0xFF];
                    } else {
                        pixel = Glass_Tint(pixel, level, 255u);
                    }
                }
            }
        }
    }

    void Bitmap_Recolor(const void* bitmap, int32_t first, int32_t count, const uint8_t* map) {
        if (twins_on) {
            twin::Recolor_Image(bitmap, first, count, map);
            return;
        }
        if (!Legacy() || map == nullptr || count <= 0) {
            return;
        }
        Flush_Pending();
        s_scoped_timer timer(TIMER_COPY);
        auto found = images.find(bitmap);
        if (found == images.end() || found->second.hd.empty()) {
            return;
        }
        s_surface& image = found->second;
        const auto recolor = [&](uint32_t index) {
            return index != 0 && (int32_t)index >= first && (int32_t)index < first + count ? (uint32_t)map[index - first] : index;
        };
        const auto recolor_pixel = [&](uint32_t& pixel) {
            if ((pixel & HD_LITERAL) == 0 && pixel != HD_CLEAR) {
                // Partly covered pixels over a drawn background keep its index too (bits 8-15).
                const uint32_t under = (pixel & HD_CLEAR) != 0 ? (pixel >> 8) & 0xFFu : recolor((pixel >> 8) & 0xFFu);
                pixel = (pixel & ~0xFFFFu) | (under << 8) | recolor(pixel & 0xFFu);
            }
        };
        // Sprite-sized bitmaps (the build screen's ship) remap every HD pixel, since the art reaches
        // past the 1x silhouette. Big ones (combat_bitmap, recoloured every frame) only remap the
        // blocks whose page pixel was in the range.
        const bool whole = image.width * image.height <= 256 * 256;
        const size_t stride = (size_t)image.width * S;
        for (int32_t i = 0; i < image.width * image.height; ++i) {
            const uint16_t old = image.shadow[(size_t)i];
            const bool hit = old != SHADOW_NONE && recolor(old) != old;
            if (hit) {
                image.shadow[(size_t)i] = (uint16_t)recolor(old);
            }
            if (!hit && !whole) {
                continue;
            }
            uint32_t* block = &image.hd[(size_t)(i / image.width) * S * stride + (size_t)(i % image.width) * S];
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    recolor_pixel(block[sy * stride + sx]);
                }
            }
        }
    }

    void Bitmap_Sync(const void* bitmap, const uint8_t* pixels) {
        if (!Legacy() || pixels == nullptr) {
            return;
        }
        Flush_Pending();
        auto found = images.find(bitmap);
        if (found == images.end() || found->second.hd.empty()) {
            return;
        }
        s_surface& image = found->second;
        const size_t stride = (size_t)image.width * S;
        for (int32_t i = 0; i < image.width * image.height; ++i) {
            const uint16_t old = image.shadow[(size_t)i];
            if (old == SHADOW_NONE || old == pixels[i]) {
                continue;
            }
            image.shadow[(size_t)i] = pixels[i];
            uint32_t* block = &image.hd[(size_t)(i / image.width) * S * stride + (size_t)(i % image.width) * S];
            const uint32_t from = Base_Rgb((uint8_t)old);
            const uint32_t to = Base_Rgb(pixels[i]);
            for (int32_t sy = 0; sy < S; ++sy) {
                for (int32_t sx = 0; sx < S; ++sx) {
                    uint32_t& pixel = block[sy * stride + sx];
                    if (pixels[i] == 0) {
                        pixel = HD_CLEAR;
                    } else if ((pixel & HD_LITERAL) == 0 && (pixel & HD_CLEAR) != 0) {
                        pixel = (pixel & ~0xFFu) | pixels[i];
                    } else {
                        // Same per-channel change as the page pixel went through.
                        const uint32_t c = Base_Color(pixel);
                        uint32_t out = 0;
                        for (int32_t shift = 0; shift <= 16; shift += 8) {
                            const uint32_t f = SDL_max((from >> shift) & 0xFF, 1u);
                            out |= SDL_min(255u, ((c >> shift) & 0xFF) * ((to >> shift) & 0xFF) / f) << shift;
                        }
                        pixel = Literal(out);
                    }
                }
            }
        }
    }

    void Bitmap_Capture(const void* bitmap, int32_t x1, int32_t y1, int32_t width, int32_t height) {
        if (!Legacy()) {
            return;
        }
        Flush_Pending();
        auto to = images.find(bitmap);
        s_surface* page = Page_Surface(video::current_video_page);
        if (to == images.end() || page == nullptr || to->second.width != width || to->second.height != height) {
            return;
        }
        s_surface& image = to->second;
        Ensure_Hd(image);
        const uint8_t* pixels = video::current_video_page;
        const size_t page_stride = (size_t)page->width * S;
        const size_t image_stride = (size_t)width * S;
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                const int32_t px = x1 + x;
                const int32_t py = y1 + y;
                const size_t i = (size_t)y * width + x;
                if (px < 0 || py < 0 || px >= page->width || py >= page->height) {
                    image.shadow[i] = SHADOW_NONE;
                    continue;
                }
                const size_t p = (size_t)py * page->width + px;
                image.shadow[i] = page->shadow[p] == pixels[p] ? page->shadow[p] : SHADOW_NONE;
                const uint32_t* src = &page->hd[(size_t)py * S * page_stride + (size_t)px * S];
                uint32_t* dst = &image.hd[(size_t)y * S * image_stride + (size_t)x * S];
                for (int32_t sy = 0; sy < S; ++sy) {
                    memcpy(dst + sy * image_stride, src + sy * page_stride, S * sizeof(uint32_t));
                }
            }
        }
    }

} // namespace render
