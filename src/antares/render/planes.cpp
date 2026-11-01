#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
namespace render {

    s_surface page_off;
    s_surface page_back;
    s_surface page_published;
    s_surface page_present;
    // Guards allocating/freeing the page surfaces (game thread) against the presenter
    // reading the published/present surfaces.
    std::mutex planes_mutex;
    // Factor (0-256) applied to literal pixels so they follow palette fades.
    uint32_t published_fade = 256;
    uint32_t present_fade = 256;
    // Off-screen images keyed by their header. Nodes stay put, so pointers remain valid.
    std::unordered_map<const void*, s_surface> images;

    void Ensure_Hd(s_surface& surface) {
        if (!surface.hd.empty() || surface.width <= 0 || surface.height <= 0) {
            return;
        }
        const size_t count = (size_t)surface.width * (size_t)surface.height;
        surface.hd.assign(count * S * S, HD_CLEAR);
        surface.shadow.assign(count, SHADOW_NONE);
        surface.mark.assign(count, 0u);
    }

    namespace {

        void Allocate_Page(s_surface& surface, int32_t width, int32_t height) {
            surface.width = width;
            surface.height = height;
            surface.hd.assign((size_t)width * height * S * S, Indexed(0));
            surface.shadow.assign((size_t)width * height, SHADOW_NONE);
            surface.mark.assign((size_t)width * height, 0u);
            surface.row_dirty.assign((size_t)height, 1);
            surface.fringe.assign((size_t)width * height, 0);
        }

        void Free_Surface(s_surface& surface) {
            surface = s_surface();
        }

    } // namespace

    void Fill_Block(s_surface& surface, int32_t x, int32_t y, uint32_t pixel) {
        if (!surface.fringe.empty()) {
            surface.fringe[(size_t)y * surface.width + x] = 0;
        }
        uint32_t* block = Block(surface, x, y);
        const size_t stride = (size_t)surface.width * S;
        for (int32_t sy = 0; sy < S; ++sy) {
            for (int32_t sx = 0; sx < S; ++sx) {
                block[sy * stride + sx] = pixel;
            }
        }
        Mark_Row(surface, y);
    }

    s_surface* Page_Surface(const uint8_t* page) {
        if (page == nullptr || page_off.hd.empty()) {
            return nullptr;
        }
        if (page == video::g_page_off.pixels) {
            return &page_off;
        }
        if (page == video::g_page_back.pixels) {
            return &page_back;
        }
        return nullptr;
    }

    void Allocate_Planes(int32_t width, int32_t height) {
        if (!enabled) {
            return;
        }
        if (twins_on) {
            twin::Allocate_Pages(width, height);
            return;
        }
        Free_Planes();
        std::lock_guard<std::mutex> lock(planes_mutex);
        Allocate_Page(page_off, width, height);
        Allocate_Page(page_back, width, height);
        Allocate_Page(page_published, width, height);
        Allocate_Page(page_present, width, height);
    }

    void Free_Planes() {
        twin::Free_Pages();
        std::lock_guard<std::mutex> lock(planes_mutex);
        Reset_Stamps();
        Free_Surface(page_off);
        Free_Surface(page_back);
        Free_Surface(page_published);
        Free_Surface(page_present);
    }

    void Copy_Plane(const uint8_t* src_page, const uint8_t* dst_page) {
        if (twins_on) {
            twin::Copy_Page(src_page, dst_page);
            return;
        }
        s_surface* src = Page_Surface(src_page);
        s_surface* dst = Page_Surface(dst_page);
        if (src == nullptr || dst == nullptr || src == dst) {
            return;
        }
        Flush_Pending();
        s_scoped_timer timer(TIMER_COPY);
        dst->hd = src->hd;
        dst->shadow = src->shadow;
        dst->fringe = src->fringe;
        std::fill(dst->row_dirty.begin(), dst->row_dirty.end(), (uint8_t)1);
    }

    void Copy_Plane_Span(const uint8_t* src_page, const uint8_t* dst_page, int32_t offset, int32_t count) {
        if (twins_on) {
            twin::Copy_Page_Span(src_page, dst_page, offset, count);
            return;
        }
        s_surface* src = Page_Surface(src_page);
        s_surface* dst = Page_Surface(dst_page);
        if (src == nullptr || dst == nullptr || src == dst || count <= 0 || offset < 0 ||
            offset + count > src->width * src->height) {
            return;
        }
        Flush_Pending();
        s_scoped_timer timer(TIMER_COPY);
        const size_t stride = (size_t)src->width * S;
        while (count > 0) {
            const int32_t x = offset % src->width;
            const int32_t y = offset / src->width;
            const int32_t run = SDL_min(count, src->width - x);
            memcpy(&dst->shadow[(size_t)offset], &src->shadow[(size_t)offset], (size_t)run * sizeof(uint16_t));
            memcpy(&dst->fringe[(size_t)offset], &src->fringe[(size_t)offset], (size_t)run);
            for (int32_t sy = 0; sy < S; ++sy) {
                const size_t at = ((size_t)y * S + sy) * stride + (size_t)x * S;
                memcpy(&dst->hd[at], &src->hd[at], (size_t)run * S * sizeof(uint32_t));
            }
            dst->row_dirty[(size_t)y] = 1;
            offset += run;
            count -= run;
        }
    }

} // namespace render
