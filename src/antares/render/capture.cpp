#include "pch.h"

#include "antares/render/capture.h"
#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"

#include <cstring>

// Spike 3.9: see capture.h.
namespace render::twin {

    bool capture_active = false;
    std::unordered_map<std::string, uint32_t> capture_ops;

    namespace {
        std::vector<std::array<uint8_t, 256>> capture_tables;
    }

    int32_t Capture_Table_Id(const uint8_t* table) {
        for (size_t i = 0; i < capture_tables.size(); ++i) {
            if (memcmp(capture_tables[i].data(), table, 256) == 0) {
                return (int32_t)i;
            }
        }
        std::array<uint8_t, 256> copy;
        memcpy(copy.data(), table, 256);
        capture_tables.push_back(copy);
        return (int32_t)capture_tables.size() - 1;
    }

} // namespace render::twin

namespace render::capture {

    using namespace render::twin;

    namespace {
        int32_t window_w = 0;
        int32_t window_h = 0;
        std::vector<s_px> saved_plane;
        std::vector<uint8_t> saved_1x;
        std::unordered_map<std::string, uint32_t> unported;

        std::string Joined(const std::unordered_map<std::string, uint32_t>& counts) {
            std::vector<std::pair<std::string, uint32_t>> items(counts.begin(), counts.end());
            std::sort(items.begin(), items.end());
            std::string out;
            for (const auto& item : items) {
                out += (out.empty() ? "" : ",") + item.first + "=" + std::to_string(item.second);
            }
            return out;
        }

        void Collect_Unported() {
            std::string names;
            uint32_t pixels = 0;
            End_Frame_Unported(&names, &pixels);
            size_t start = 0;
            while (start < names.size()) {
                size_t end = names.find(',', start);
                if (end == std::string::npos) {
                    end = names.size();
                }
                std::string item = names.substr(start, end - start);
                const size_t eq = item.find('=');
                const std::string name = eq == std::string::npos ? item : item.substr(0, eq);
                const uint32_t count = eq == std::string::npos ? 1u : (uint32_t)atoi(item.c_str() + eq + 1);
                if (!name.empty()) {
                    unported[name] += count;
                }
                start = end + 1;
            }
        }

        // One plane pixel as straight RGBA (alpha 0 when it shows nothing).
        void Pixel_Rgba(const s_px& px, uint8_t* out, uint8_t* glass) {
            *glass = 0;
            uint32_t rgb = 0;
            uint32_t alpha = 0;
            const uint32_t kind = Kind(px);
            if (kind == KIND_GLASS) {
                *glass = (uint8_t)(px.base + 1);
            } else if (Bound(px)) {
                const uint32_t cover = (px.mask >> 16) & 0xFF;
                const uint32_t fg = Scale_Rgb(Base_Rgb((uint8_t)px.mask), Shade_Ratio(px));
                if ((px.mask & MASK_CLEAR) != 0) {
                    rgb = fg;
                    alpha = cover;
                } else {
                    rgb = cover < 255 ? Mix(fg, Base_Rgb((uint8_t)(px.mask >> 8)), cover) : fg;
                    alpha = 255;
                }
            } else {
                rgb = px.base & 0xFFFFFFu;
                alpha = (px.mask & MASK_CLEAR) != 0 ? (px.base >> 24) : 255u;
            }
            out[0] = (uint8_t)(rgb >> 16);
            out[1] = (uint8_t)(rgb >> 8);
            out[2] = (uint8_t)rgb;
            out[3] = (uint8_t)alpha;
        }
    } // namespace

    bool Available() {
        return !plane_off.px.empty();
    }

    int32_t Scale() {
        return S;
    }

    bool Active() {
        return capture_active;
    }

    void Begin(int32_t width, int32_t height) {
        if (!Available() || capture_active) {
            return;
        }
        render::twin::End_Draw();
        window_w = SDL_min(width, plane_off.width);
        window_h = SDL_min(height, plane_off.height);
        const size_t plane_count = (size_t)window_h * S * plane_off.width * S;
        saved_plane.assign(plane_off.px.begin(), plane_off.px.begin() + (ptrdiff_t)plane_count);
        saved_1x.assign(video::g_page_off.pixels, video::g_page_off.pixels + (size_t)window_h * plane_off.width);
        capture_ops.clear();
        unported.clear();
        capture_tables.clear();
        Collect_Unported(); // what came before isn't the effect's
        unported.clear();
        capture_active = true;
    }

    void Clear() {
        if (!capture_active) {
            return;
        }
        render::twin::End_Draw();
        const s_px clear = Clear_Px();
        const size_t row = (size_t)plane_off.width * S;
        for (int32_t y = 0; y < window_h * S; ++y) {
            std::fill(plane_off.px.begin() + (ptrdiff_t)(y * row), plane_off.px.begin() + (ptrdiff_t)(y * row + (size_t)window_w * S), clear);
        }
        for (int32_t y = 0; y < window_h; ++y) {
            memset(video::g_page_off.pixels + (size_t)y * plane_off.width, 0, (size_t)window_w);
            if (!plane_off.row_dirty.empty()) {
                plane_off.row_dirty[(size_t)y] = 1;
            }
        }
    }

    bool Frame(s_frame* out) {
        if (!capture_active) {
            return false;
        }
        render::twin::End_Draw();
        Collect_Unported();
        const size_t row = (size_t)plane_off.width * S;
        int32_t x0 = window_w * S, y0 = window_h * S, x1 = -1, y1 = -1;
        uint8_t rgba[4];
        uint8_t glass;
        for (int32_t y = 0; y < window_h * S; ++y) {
            const s_px* line = &plane_off.px[(size_t)y * row];
            for (int32_t x = 0; x < window_w * S; ++x) {
                Pixel_Rgba(line[x], rgba, &glass);
                if (rgba[3] != 0 || glass != 0) {
                    x0 = SDL_min(x0, x);
                    x1 = SDL_max(x1, x);
                    y0 = SDL_min(y0, y);
                    y1 = SDL_max(y1, y);
                }
            }
        }
        if (x1 < 0) {
            return false;
        }
        // Whole 1x pixels, so the crop's place is a 1x position.
        x0 = x0 / S * S;
        y0 = y0 / S * S;
        x1 = (x1 / S + 1) * S - 1;
        y1 = (y1 / S + 1) * S - 1;
        out->x = x0 / S;
        out->y = y0 / S;
        out->width = x1 - x0 + 1;
        out->height = y1 - y0 + 1;
        out->rgba.assign((size_t)out->width * out->height * 4, 0);
        out->glass.assign((size_t)out->width * out->height, 0);
        for (int32_t y = y0; y <= y1; ++y) {
            const s_px* line = &plane_off.px[(size_t)y * row];
            for (int32_t x = x0; x <= x1; ++x) {
                const size_t i = (size_t)(y - y0) * out->width + (x - x0);
                Pixel_Rgba(line[x], &out->rgba[i * 4], &out->glass[i]);
            }
        }
        return true;
    }

    void End() {
        if (!capture_active) {
            return;
        }
        render::twin::End_Draw();
        Collect_Unported();
        capture_active = false;
        std::copy(saved_plane.begin(), saved_plane.end(), plane_off.px.begin());
        memcpy(video::g_page_off.pixels, saved_1x.data(), saved_1x.size());
        for (int32_t y = 0; y < window_h; ++y) {
            if (!plane_off.row_dirty.empty()) {
                plane_off.row_dirty[(size_t)y] = 1;
            }
        }
        saved_plane.clear();
        saved_1x.clear();
    }

    const std::vector<std::array<uint8_t, 256>>& Tables() {
        return capture_tables;
    }

    std::vector<uint8_t> Glass_Lut(int32_t table_id) {
        std::vector<uint8_t> lut((size_t)32 * 32 * 32 * 3, 0);
        if (table_id < 0 || (size_t)table_id >= capture_tables.size()) {
            return lut;
        }
        const uint8_t* table = capture_tables[(size_t)table_id].data();
        for (int32_t b = 0; b < 32; ++b) {
            for (int32_t g = 0; g < 32; ++g) {
                for (int32_t r = 0; r < 32; ++r) {
                    const uint32_t rgb = ((uint32_t)(r * 255 / 31) << 16) | ((uint32_t)(g * 255 / 31) << 8) | (uint32_t)(b * 255 / 31);
                    const uint32_t through = Capture_Through_Table(rgb, table);
                    const size_t i = ((size_t)r + (size_t)g * 32 + (size_t)b * 1024) * 3;
                    lut[i] = (uint8_t)(through >> 16);
                    lut[i + 1] = (uint8_t)(through >> 8);
                    lut[i + 2] = (uint8_t)through;
                }
            }
        }
        return lut;
    }

    std::string Ops() {
        return Joined(capture_ops);
    }

    std::string Unported() {
        return Joined(unported);
    }

    void Resolve_Window(std::vector<uint8_t>* rgba, int32_t* width, int32_t* height) {
        render::twin::End_Draw();
        const int32_t w = (capture_active ? window_w : SDL_min(640, plane_off.width)) * S;
        const int32_t h = (capture_active ? window_h : SDL_min(351, plane_off.height)) * S;
        *width = w;
        *height = h;
        rgba->assign((size_t)w * h * 4, 0);
        const size_t row = (size_t)plane_off.width * S;
        uint8_t glass;
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                uint8_t* out = &(*rgba)[((size_t)y * w + x) * 4];
                Pixel_Rgba(plane_off.px[(size_t)y * row + x], out, &glass);
                out[3] = 255;
            }
        }
    }

    void Composite(const s_frame& frame, std::vector<uint8_t>* background, int32_t width) {
        for (int32_t y = 0; y < frame.height; ++y) {
            for (int32_t x = 0; x < frame.width; ++x) {
                const int32_t bx = frame.x * S + x;
                const int32_t by = frame.y * S + y;
                uint8_t* bg = &(*background)[((size_t)by * width + bx) * 4];
                const size_t i = (size_t)y * frame.width + x;
                const uint8_t mark = frame.glass[i];
                if (mark != 0 && (size_t)(mark - 1) < capture_tables.size()) {
                    const uint32_t rgb = ((uint32_t)bg[0] << 16) | ((uint32_t)bg[1] << 8) | bg[2];
                    const uint32_t through = Capture_Through_Table(rgb, capture_tables[(size_t)(mark - 1)].data());
                    bg[0] = (uint8_t)(through >> 16);
                    bg[1] = (uint8_t)(through >> 8);
                    bg[2] = (uint8_t)through;
                }
                const uint8_t* src = &frame.rgba[i * 4];
                const uint32_t a = src[3];
                for (int32_t c = 0; c < 3; ++c) {
                    bg[c] = (uint8_t)((src[c] * a + bg[c] * (255u - a)) / 255u);
                }
            }
        }
    }

} // namespace render::capture
