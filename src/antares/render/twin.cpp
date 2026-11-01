#include "pch.h"

#include "antares/render/internal.h"
#include "antares/render/twin_internal.h"
#include "antares/identity.h"

#include <algorithm>
#include <string>

// Twin planes, WP1.3a: plane storage and lookup, scopes, and the Unported fallback.
namespace render {

    bool twins_on;

    namespace twin {

        s_plane plane_off;
        s_plane plane_back;
        s_plane plane_published;
        s_plane plane_present;
        std::unordered_map<const void*, s_plane> images;

        namespace {

            // 1x pixels of an image start after its header and frame offset (bitmap.cpp).
            constexpr size_t IMAGE_PIXELS_OFFSET = 0x10;

            // Open scopes, innermost last. Entries are reused, so their snapshot buffers keep
            // their capacity.
            struct s_open {
                const char* op;
                const void* target;
                int32_t width; // the target's size when opened
                int32_t height;
                int32_t x0;
                int32_t y0;
                int32_t x1;
                int32_t y1;
                std::vector<uint8_t> before; // the rectangle's 1x pixels when opened
                uint64_t opened_ns;          // ANTARES_HD_STATS: when it was opened
            };
            std::vector<s_open> open_scopes;
            int32_t depth;

            // Unported calls since the last publish, by operation (outermost scopes only).
            std::unordered_map<std::string, uint32_t> unported_calls;
            uint32_t unported_pixels;
            // The same over the current stats window (ANTARES_HD_STATS).
            std::unordered_map<std::string, uint32_t> window_calls;
            uint32_t window_pixels;
            uint64_t window_scope_ns; // time inside outermost scopes (snapshot, operation, fallback)

            std::string Joined(const std::unordered_map<std::string, uint32_t>& calls) {
                std::vector<std::pair<uint32_t, std::string>> sorted;
                for (const auto& item : calls) {
                    sorted.emplace_back(item.second, item.first);
                }
                std::sort(sorted.rbegin(), sorted.rend());
                std::string names;
                for (const auto& item : sorted) {
                    names += (names.empty() ? "" : ",") + item.second + "=" + std::to_string(item.first);
                }
                return names;
            }

            void Allocate_Page(s_plane& plane, int32_t width, int32_t height) {
                plane.width = width;
                plane.height = height;
                plane.px.assign((size_t)width * height * S * S, Index_Px(0));
                plane.row_dirty.assign((size_t)height, 1);
            }

        } // namespace

        // The last operations run on each image (debug: ANTARES_TWIN_CHECK).
        std::unordered_map<const void*, std::string> recent_ops;

        std::string Recent_Ops(const void* target) {
            const auto found = recent_ops.find(target);
            return found == recent_ops.end() ? std::string("(none)") : found->second;
        }

        void Note_Op(const void* target, const char* op) {
            if (images.count(target) == 0) {
                return;
            }
            std::string& ops = recent_ops[target];
            ops = std::string(op) + (ops.empty() ? "" : " < ") + ops;
            if (ops.size() > 200) {
                ops.resize(200);
            }
        }

        s_plane* Resolve_Target(const void* target, const uint8_t** pixels, int32_t* width, int32_t* height) {
            if (target == nullptr) {
                return nullptr;
            }
            if (!plane_off.px.empty()) {
                if (target == video::g_page_off.pixels) {
                    *pixels = video::g_page_off.pixels;
                    *width = plane_off.width;
                    *height = plane_off.height;
                    return &plane_off;
                }
                if (target == video::g_page_back.pixels) {
                    *pixels = video::g_page_back.pixels;
                    *width = plane_back.width;
                    *height = plane_back.height;
                    return &plane_back;
                }
            }
            const auto found = images.find(target);
            if (found == images.end()) {
                return nullptr;
            }
            *pixels = (const uint8_t*)target + IMAGE_PIXELS_OFFSET;
            *width = found->second.width;
            *height = found->second.height;
            return &found->second;
        }

        void Probe(const s_plane& plane, int32_t x, int32_t y, const char* what, int32_t value) {
            if (x == probe_x && y == probe_y && (&plane == &plane_off || &plane == &plane_back)) {
                const s_px px = plane.px.empty() ? s_px{0u, 0u} : plane.px[(size_t)y * S * plane.width * S + (size_t)x * S];
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin probe (%d,%d) %s %s value=%d depth=%d plane before=%08x", x, y, &plane == &plane_off ? "off" : "back", what, value, depth, px.mask);
            }
        }

        void Fill_Block_Index(s_plane& plane, int32_t x, int32_t y, uint8_t index) {
            if (probe_x >= 0) {
                Probe(plane, x, y, "index", index);
            }
            s_px* block = Block(plane, x, y);
            const size_t stride = (size_t)plane.width * S;
            const s_px px = Index_Px(index);
            for (int32_t sy = 0; sy < S; ++sy) {
                std::fill(block + sy * stride, block + sy * stride + S, px);
            }
        }

        void Fill_From_1x(s_plane& plane, const uint8_t* pixels) {
            plane.px.resize((size_t)plane.width * plane.height * S * S);
            for (int32_t y = 0; y < plane.height; ++y) {
                for (int32_t x = 0; x < plane.width; ++x) {
                    Fill_Block_Index(plane, x, y, pixels[(size_t)y * plane.width + x]);
                }
            }
            std::fill(plane.row_dirty.begin(), plane.row_dirty.end(), (uint8_t)1);
        }

        void Attach_Image(const void* header, int32_t width, int32_t height) {
            if (header == nullptr || width <= 0 || height <= 0) {
                return;
            }
            Note_Op(header, "(created)");
            static const bool check_attach = antares::Get_Env("TWIN_CHECK") != nullptr;
            static uint32_t logged_attach;
            if (check_attach && logged_attach++ < 12) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin check: attach %p %dx%d at depth %d", header, width, height, depth);
            }
            // Headers are created again in place for every flip, copy and scale: keep the buffer.
            s_plane& plane = images[header];
            plane.width = width;
            plane.height = height;
            plane.px.clear();
            // Created again inside an operation on it (Draw_Picture_To_Bitmap, Screen_Bitmap_Capture,
            // Scale_Bitmap): the open scope covers the new image, starting from a blank one.
            for (int32_t i = 0; i < depth; ++i) {
                s_open& entry = open_scopes[(size_t)i];
                if (entry.target == header) {
                    entry.width = width;
                    entry.height = height;
                    entry.x0 = 0;
                    entry.y0 = 0;
                    entry.x1 = width;
                    entry.y1 = height;
                    entry.before.assign((size_t)width * height, 0);
                }
            }
        }

        void Drop_Images(const void* begin, const void* end) {
            for (auto it = images.begin(); it != images.end();) {
                it = it->first >= begin && it->first < end ? images.erase(it) : std::next(it);
            }
        }

        void Allocate_Pages(int32_t width, int32_t height) {
            std::lock_guard<std::mutex> lock(planes_mutex);
            Allocate_Page(plane_off, width, height);
            Allocate_Page(plane_back, width, height);
            Allocate_Page(plane_published, width, height);
            Allocate_Page(plane_present, width, height);
        }

        void Free_Pages() {
            std::lock_guard<std::mutex> lock(planes_mutex);
            plane_off = s_plane();
            plane_back = s_plane();
            plane_published = s_plane();
            plane_present = s_plane();
        }

        void End_Frame_Unported(std::string* names, uint32_t* pixels) {
            *names = Joined(unported_calls);
            *pixels = unported_pixels;
            unported_calls.clear();
            unported_pixels = 0;
        }

        void End_Window_Unported(std::string* names, uint32_t* pixels, uint64_t* scope_ns) {
            *names = Joined(window_calls);
            *pixels = window_pixels;
            *scope_ns = window_scope_ns;
            window_calls.clear();
            window_pixels = 0;
            window_scope_ns = 0;
        }

        void Accept_Rect(const void* target, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            const uint8_t* pixels = nullptr;
            int32_t width = 0;
            for (int32_t i = 0; i < depth; ++i) {
                s_open& entry = open_scopes[(size_t)i];
                if (entry.target != target) {
                    continue;
                }
                if (pixels == nullptr) {
                    int32_t height = 0;
                    if (Resolve_Target(target, &pixels, &width, &height) == nullptr || width != entry.width) {
                        return;
                    }
                }
                const int32_t ax0 = SDL_max(x0, entry.x0);
                const int32_t ay0 = SDL_max(y0, entry.y0);
                const int32_t ax1 = SDL_min(x1, entry.x1);
                const int32_t ay1 = SDL_min(y1, entry.y1);
                for (int32_t y = ay0; y < ay1; ++y) {
                    if (ax0 < ax1) {
                        memcpy(&entry.before[(size_t)(y - entry.y0) * (entry.x1 - entry.x0) + (ax0 - entry.x0)],
                            pixels + (size_t)y * width + ax0,
                            (size_t)(ax1 - ax0));
                    }
                }
            }
        }

        void Accept(const void* target, int32_t x, int32_t y) {
            if (depth > 0) {
                Accept_Rect(target, x, y, x + 1, y + 1);
            }
        }

        void Scope::Open(const char* op, const void* target, int32_t x, int32_t y, int32_t width, int32_t height) {
            Note_Op(target, op);
            const uint8_t* pixels = nullptr;
            int32_t target_width = 0;
            int32_t target_height = 0;
            if (Resolve_Target(target, &pixels, &target_width, &target_height) == nullptr) {
                return; // no plane: nothing to keep in step
            }
            if (width <= 0 || height <= 0) {
                x = 0;
                y = 0;
                width = target_width;
                height = target_height;
            }
            const int32_t x0 = SDL_max(x, 0);
            const int32_t y0 = SDL_max(y, 0);
            const int32_t x1 = SDL_min((int64_t)x + width, (int64_t)target_width);
            const int32_t y1 = SDL_min((int64_t)y + height, (int64_t)target_height);
            if (x0 >= x1 || y0 >= y1) {
                return;
            }
            if ((int32_t)open_scopes.size() <= depth) {
                open_scopes.emplace_back();
            }
            s_open& entry = open_scopes[(size_t)depth];
            entry.op = op;
            entry.target = target;
            entry.width = target_width;
            entry.height = target_height;
            entry.x0 = x0;
            entry.y0 = y0;
            entry.x1 = x1;
            entry.y1 = y1;
            entry.opened_ns = stats_enabled && depth == 0 ? SDL_GetTicksNS() : 0;
            entry.before.resize((size_t)(x1 - x0) * (y1 - y0));
            for (int32_t row = y0; row < y1; ++row) {
                memcpy(&entry.before[(size_t)(row - y0) * (x1 - x0)], pixels + (size_t)row * target_width + x0, (size_t)(x1 - x0));
            }
            slot_ = depth++;
        }

        void Scope::Open_Sprite(const char* op, const void* target, int32_t x, int32_t y, const s_animation_header* anim) {
            if (anim == nullptr) {
                return;
            }
            if ((anim->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_SCREEN_BACKGROUND) {
                Open(op, target, 0, 0, 0, 0);
                return;
            }
            Open(op, target, x, y, anim->width, anim->height);
        }

        void Scope::Close() {
            End_Draw(); // a draw still collecting pixels inside this operation renders first
            depth = slot_;
            const s_open& entry = open_scopes[(size_t)slot_];
            slot_ = -1;
            const uint8_t* pixels = nullptr;
            int32_t width = 0;
            int32_t height = 0;
            s_plane* plane = Resolve_Target(entry.target, &pixels, &width, &height);
            if (plane == nullptr || plane->px.empty()) {
                return; // an image with no plane pixels yet stands for its 1x pixels: nothing to do
            }
            uint32_t changed = 0;
            if (plane->px.empty() || width != entry.width || height != entry.height) {
                static const bool check_full = antares::Get_Env("TWIN_CHECK") != nullptr;
                static uint32_t logged_full;
                if (check_full && logged_full++ < 10) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin check: %s on %p refilled whole: empty=%d size %dx%d (opened %dx%d) depth=%d",
                        entry.op, entry.target, plane->px.empty() ? 1 : 0, width, height, entry.width, entry.height, depth);
                }
                // An image created or resized inside the operation: all of it from 1x.
                Fill_From_1x(*plane, pixels);
                changed = (uint32_t)width * height;
            } else {
                const int32_t span = entry.x1 - entry.x0;
                for (int32_t y = entry.y0; y < entry.y1; ++y) {
                    const uint8_t* now = pixels + (size_t)y * width;
                    const uint8_t* was = &entry.before[(size_t)(y - entry.y0) * span];
                    bool row_changed = false;
                    for (int32_t x = entry.x0; x < entry.x1; ++x) {
                        if (now[x] != was[x - entry.x0]) {
                            static const bool check = antares::Get_Env("TWIN_CHECK") != nullptr;
                            static uint32_t logged;
                            if (check && changed == 0 && logged++ < 40) {
                                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "twin check: Unported %s on %p (%dx%d) first change at %d,%d: %u -> %u",
                                    entry.op, entry.target, width, height, x, y, was[x - entry.x0], now[x]);
                            }
                            Fill_Block_Index(*plane, x, y, now[x]);
                            row_changed = true;
                            changed++;
                        }
                    }
                    if (row_changed && !plane->row_dirty.empty()) {
                        plane->row_dirty[(size_t)y] = 1;
                    }
                }
            }
            if (depth == 0 && changed != 0) {
                unported_calls[entry.op]++;
                window_calls[entry.op]++;
            }
            unported_pixels += changed;
            window_pixels += changed;
            if (entry.opened_ns != 0) {
                window_scope_ns += SDL_GetTicksNS() - entry.opened_ns;
            }
        }

    } // namespace twin

} // namespace render
