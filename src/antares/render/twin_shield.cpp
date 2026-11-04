// Combat shields in HD (render::Combat_Shield): the shield glow around a ship, drawn again on
// the HD plane of the image the game built it in (CMBTDRW1, the shielded-ship branch).
//
// The game grows the shield as rings one 1x pixel thick (bitmap::Outline_Bitmap, once per ring
// colour), cuts four arcs apart with a 1x gap mask, drops the outer rings of weakened arcs through
// 1x arc masks and draws the ship on top. Each of those steps has its own twin, so the HD result
// already follows the HD ship, but it is still made of whole 1x rings and 1x masks. This pass
// replaces the shield part of the plane with the same shield measured at sub-pixel scale:
//   - the distance of each sub-pixel to the HD ship (exact Euclidean distance transform);
//   - ring k covers distances (k, k+1] in 1x pixels; the colour blends between neighbouring
//     ring colours (an index pixel's fg/bg/coverage, so palette effects still reach it);
//   - the gap lines and arc sectors come from the game's own masks: the centre and the gap
//     angle are fitted to the gap mask, each arc's sector is the one holding its mask;
//   - edges (the ship side, the outer edge, the gaps) are antialiased over one sub-pixel.
// Rewritten sub-pixels carry MASK_STROKE: they follow the geometry, not the 1x pixels (D21).
// The 1x rings grow along rows and columns only, so they cut the corners the round HD shield
// keeps; the image is marked `geometry` and drawing it also draws those pixels (twin_draw.cpp).

#include "internal.h"
#include "twin_internal.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <unordered_map>
#include <vector>

namespace render::twin {

    namespace {

        constexpr double PI = 3.14159265358979323846;

        // 1D squared distance transform (Felzenszwalb and Huttenlocher) of `f`, in place. Far
        // cells hold a large finite value, so the parabola intersections stay finite.
        void Distance_1D(std::vector<double>& f, int32_t n, std::vector<double>& d, std::vector<int32_t>& v, std::vector<double>& z) {
            auto cross = [&](int32_t q, int32_t p) {
                return ((f[q] + (double)q * q) - (f[p] + (double)p * p)) / (2.0 * q - 2.0 * p);
            };
            int32_t k = 0;
            v[0] = 0;
            z[0] = -1e300;
            z[1] = 1e300;
            for (int32_t q = 1; q < n; ++q) {
                double s = cross(q, v[k]);
                while (s <= z[k]) {
                    --k;
                    s = cross(q, v[k]);
                }
                ++k;
                v[k] = q;
                z[k] = s;
                z[k + 1] = 1e300;
            }
            k = 0;
            for (int32_t q = 0; q < n; ++q) {
                while (z[k + 1] < q) {
                    ++k;
                }
                const double dq = (double)q - v[k];
                d[q] = dq * dq + f[v[k]];
            }
            for (int32_t q = 0; q < n; ++q) {
                f[q] = d[q];
            }
        }

        // Squared distance (in sub-pixels) of each cell to the nearest solid cell.
        std::vector<double> Distance_Field(const std::vector<uint8_t>& solid, int32_t w, int32_t h) {
            constexpr double FAR = 1e20;
            std::vector<double> grid((size_t)w * h);
            for (size_t i = 0; i < grid.size(); ++i) {
                grid[i] = solid[i] == 1 ? 0.0 : FAR;
            }
            const int32_t n = SDL_max(w, h);
            std::vector<double> f(n), d(n), z(n + 1);
            std::vector<int32_t> v(n);
            for (int32_t x = 0; x < w; ++x) {
                for (int32_t y = 0; y < h; ++y) {
                    f[y] = grid[(size_t)y * w + x];
                }
                Distance_1D(f, h, d, v, z);
                for (int32_t y = 0; y < h; ++y) {
                    grid[(size_t)y * w + x] = f[y];
                }
            }
            for (int32_t y = 0; y < h; ++y) {
                for (int32_t x = 0; x < w; ++x) {
                    f[x] = grid[(size_t)y * w + x];
                }
                Distance_1D(f, w, d, v, z);
                for (int32_t x = 0; x < w; ++x) {
                    grid[(size_t)y * w + x] = f[x];
                }
            }
            return grid;
        }

        double Wrap_Degrees(double a) {
            a = std::fmod(a, 360.0);
            return a < 0 ? a + 360.0 : a;
        }

        // The gap mask's geometry: its centre (the centroid of the gap pixels: two lines crossing
        // there, not always at right angles), each line's angle in [0, 180) and the gap's
        // half-width, all in 1x pixels.
        struct s_gaps {
            double cx = 0;
            double cy = 0;
            double angle[2] = {0, 90};
            double half = 0.5;
            double sin_a[2] = {0, 1}; // of each angle
            double cos_a[2] = {1, 0};
        };

        double Line_Distance(double dx, double dy, double angle_degrees) {
            const double a = angle_degrees * PI / 180.0;
            return std::fabs(-dx * std::sin(a) + dy * std::cos(a));
        }

        double Gap_Distance(const s_gaps& gaps, double dx, double dy) {
            return SDL_min(std::fabs(-dx * gaps.sin_a[0] + dy * gaps.cos_a[0]), std::fabs(-dx * gaps.sin_a[1] + dy * gaps.cos_a[1]));
        }

        s_gaps Fit_Gaps(const uint8_t* mask, int32_t width, int32_t height) {
            s_gaps gaps;
            std::vector<std::pair<double, double>> points;
            double sx = 0;
            double sy = 0;
            for (int32_t y = 0; y < height; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    if (mask[(size_t)y * width + x] != 0) {
                        points.emplace_back(x + 0.5, y + 0.5);
                        sx += x + 0.5;
                        sy += y + 0.5;
                    }
                }
            }
            if (points.empty()) {
                gaps.cx = width / 2.0;
                gaps.cy = height / 2.0;
                return gaps;
            }
            gaps.cx = sx / points.size();
            gaps.cy = sy / points.size();
            // Each line: the angle that puts the most gap pixels within one pixel of it; the second
            // is looked for at least 30 degrees from the first.
            auto hits = [&](double angle) {
                int32_t n = 0;
                for (const auto& p : points) {
                    n += Line_Distance(p.first - gaps.cx, p.second - gaps.cy, angle) <= 1.0 ? 1 : 0;
                }
                return n;
            };
            for (int32_t line = 0; line < 2; ++line) {
                int32_t best = -1;
                for (int32_t step = 0; step < 360; ++step) {
                    const double angle = step * 0.5;
                    if (line == 1) {
                        const double apart = std::fabs(std::remainder(angle - gaps.angle[0], 180.0));
                        if (apart < 30.0) {
                            continue;
                        }
                    }
                    const int32_t n = hits(angle);
                    if (n > best) {
                        best = n;
                        gaps.angle[line] = angle;
                    }
                }
            }
            // Half-width: the gap pixels' area over the lines' length inside the mask.
            double length = 0;
            for (int32_t line = 0; line < 2; ++line) {
                const double a = gaps.angle[line] * PI / 180.0;
                const double ca = std::fabs(std::cos(a));
                const double sa = std::fabs(std::sin(a));
                length += SDL_min(ca > 1e-6 ? width / ca : 1e30, sa > 1e-6 ? height / sa : 1e30);
            }
            gaps.half = SDL_max(0.5, points.size() / length / 2.0);
            for (int32_t line = 0; line < 2; ++line) {
                gaps.sin_a[line] = std::sin(gaps.angle[line] * PI / 180.0);
                gaps.cos_a[line] = std::cos(gaps.angle[line] * PI / 180.0);
            }
            return gaps;
        }

        // Which sector between the gap lines (0..3, counted from the first line's direction)
        // the direction (dx, dy) falls in.
        int32_t Quadrant(const s_gaps& gaps, double dx, double dy) {
            const double theta = Wrap_Degrees(std::atan2(dy, dx) * 180.0 / PI - gaps.angle[0]);
            const double second = SDL_fmod(Wrap_Degrees(gaps.angle[1] - gaps.angle[0]), 180.0); // in (0, 180)
            const double half = SDL_fmod(theta, 180.0);
            return (theta >= 180.0 ? 2 : 0) + (half >= second ? 1 : 0);
        }

        int32_t Mask_Quadrant(const s_gaps& gaps, const uint8_t* mask, int32_t width, int32_t height) {
            double sx = 0;
            double sy = 0;
            int32_t count = 0;
            for (int32_t y = 0; y < height; ++y) {
                for (int32_t x = 0; x < width; ++x) {
                    if (mask[(size_t)y * width + x] != 0) {
                        sx += x + 0.5;
                        sy += y + 0.5;
                        ++count;
                    }
                }
            }
            if (count == 0) {
                return -1;
            }
            return Quadrant(gaps, sx / count - gaps.cx, sy / count - gaps.cy);
        }

        // The ship as Shield_Ship saw it, before the shield was grown around it.
        struct s_outline {
            std::vector<uint8_t> cells; // sub-pixels: 0 empty, 1 solid, 2 edge
            int32_t width = 0;          // 1x
            int32_t height = 0;
            int32_t x = 0;
            int32_t y = 0;
        } ship_outline;

        uint8_t Clamp_Coverage(double a) {
            return (uint8_t)SDL_clamp((int32_t)std::lround(a * 255.0), 0, 255);
        }

    } // namespace

    void Shield_Ship(const void* ship, int32_t x, int32_t y) {
        ship_outline = {};
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        const s_plane* plane = Resolve_Target(ship, &pixels, &width, &height);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const bool hd = plane != nullptr && !plane->px.empty();
        const int32_t sw = width * S;
        const int32_t sh = height * S;
        ship_outline.cells.assign((size_t)sw * sh, 0);
        for (int32_t sy = 0; sy < sh; ++sy) {
            for (int32_t sx = 0; sx < sw; ++sx) {
                // 1: the ship (distances start here); 2: its antialiased edge, kept as drawn.
                uint32_t coverage = 0;
                if (hd) {
                    const s_px& px = plane->px[(size_t)sy * sw + sx];
                    if (Bound(px)) {
                        // Index 0 is transparent inside images.
                        coverage = (px.mask & 0xFF) == 0 ? 0 : (px.mask & MASK_CLEAR) == 0 ? 255 : (px.mask >> 16) & 0xFF;
                    } else {
                        coverage = (px.base >> 24) & 0xFF;
                    }
                } else {
                    coverage = pixels[(size_t)(sy / S) * width + sx / S] != 0 ? 255 : 0;
                }
                ship_outline.cells[(size_t)sy * sw + sx] = coverage >= 128 ? 1 : coverage > 0 ? 2 : 0;
            }
        }
        ship_outline.width = width;
        ship_outline.height = height;
        ship_outline.x = x;
        ship_outline.y = y;
    }

    void Shield_Image(const s_shield& shield) {
        const uint8_t* pixels = nullptr;
        int32_t width = 0;
        int32_t height = 0;
        s_plane* plane = Resolve_Target(shield.image, &pixels, &width, &height);
        if (plane == nullptr || width <= 0 || height <= 0 || shield.ring_count <= 0 || shield.gap_mask == nullptr) {
            return;
        }
        if (plane->px.empty()) {
            Fill_From_1x(*plane, pixels);
        }
        if (ship_outline.width <= 0) {
            return;
        }

        // The ship's outline (Shield_Ship) on the image's sub-pixel grid.
        const int32_t hw = width * S;
        const int32_t hh = height * S;
        std::vector<uint8_t> solid((size_t)hw * hh, 0);
        const int32_t sw = ship_outline.width * S;
        const int32_t sh = ship_outline.height * S;
        for (int32_t y = 0; y < sh; ++y) {
            const int32_t iy = ship_outline.y * S + y;
            if (iy < 0 || iy >= hh) {
                continue;
            }
            for (int32_t x = 0; x < sw; ++x) {
                const int32_t ix = ship_outline.x * S + x;
                if (ix >= 0 && ix < hw) {
                    solid[(size_t)iy * hw + ix] = ship_outline.cells[(size_t)y * sw + x];
                }
            }
        }
        // The same ship in the same pose is shielded many frames running (a hit animation): the
        // distance field is cached by the outline's hash.
        static std::unordered_map<uint64_t, std::vector<double>> fields;
        uint64_t field_key = 1469598103934665603ull ^ ((uint64_t)hw << 32) ^ (uint64_t)hh;
        for (const uint8_t cell : solid) {
            field_key = (field_key ^ (cell == 1 ? 1u : 0u)) * 1099511628211ull;
        }
        auto field = fields.find(field_key);
        if (field == fields.end()) {
            if (fields.size() >= 32) {
                fields.clear();
            }
            field = fields.emplace(field_key, Distance_Field(solid, hw, hh)).first;
        }
        const std::vector<double>& dist2 = field->second;

        // Arc geometry from the game's masks.
        // The fit only depends on the mask's pixels: cached by their hash.
        static std::unordered_map<uint64_t, s_gaps> fitted;
        uint64_t key = 1469598103934665603ull ^ ((uint64_t)shield.mask_width << 32) ^ (uint64_t)shield.mask_height;
        for (int32_t i = 0; i < shield.mask_width * shield.mask_height; ++i) {
            key = (key ^ shield.gap_mask[i]) * 1099511628211ull;
        }
        auto found = fitted.find(key);
        if (found == fitted.end()) {
            found = fitted.emplace(key, Fit_Gaps(shield.gap_mask, shield.mask_width, shield.mask_height)).first;
        }
        const s_gaps gaps = found->second;
        plane->geometry = true;
        int32_t arc_of_quadrant[4] = {-1, -1, -1, -1};
        for (int32_t arc = 0; arc < 4; ++arc) {
            if (shield.arc_masks[arc] != nullptr) {
                const int32_t q = Mask_Quadrant(gaps, shield.arc_masks[arc], shield.arc_width[arc], shield.arc_height[arc]);
                if (q >= 0) {
                    arc_of_quadrant[q] = arc;
                }
            }
        }

        // The rings as grown: Outline_Bitmap takes pixels already in its colour as empty, so the
        // same colour twice in a row grows no new ring.
        uint8_t rings[sizeof(shield.rings)];
        int32_t ring_count = 0;
        for (int32_t k = 0; k < shield.ring_count; ++k) {
            if (ring_count == 0 || rings[ring_count - 1] != shield.rings[k]) {
                rings[ring_count++] = shield.rings[k];
            }
        }
        // Rings: spacers (colours 1 and 2, cleared at the end) leave the inner band empty.
        int32_t first = 0;
        while (first < ring_count && (rings[first] == 1 || rings[first] == 2)) {
            ++first;
        }
        if (first >= ring_count) {
            return;
        }
        // Each arc's outer edge: one past its last ring that kept its colour.
        int32_t outer[4];
        for (int32_t arc = 0; arc < 4; ++arc) {
            outer[arc] = first;
            for (int32_t k = first; k < ring_count; ++k) {
                const uint8_t c = rings[k];
                const bool dropped = c >= 0x50 && c <= 0x57 && (shield.dropped[arc] & (1u << (c - 0x50))) != 0;
                if (!dropped) {
                    outer[arc] = k + 1;
                }
            }
        }

        int32_t widest = first;
        for (int32_t arc = 0; arc < 4; ++arc) {
            widest = SDL_max(widest, outer[arc]);
        }
        const double widest_sub2 = ((double)widest * S + 0.5) * ((double)widest * S + 0.5);
        const double inv_s = 1.0 / S;
        for (int32_t y = 0; y < hh; ++y) {
            for (int32_t x = 0; x < hw; ++x) {
                const size_t i = (size_t)y * hw + x;
                if (solid[i] != 0) {
                    continue; // the ship and its edge
                }
                s_px& px = plane->px[i];
                // Distance to the ship in 1x pixels, from this sub-pixel's centre to the edge of
                // the nearest solid sub-pixel.
                if (dist2[i] >= widest_sub2) {
                    if (!Bound(px) || (px.mask & MASK_CLEAR) == 0 || ((px.mask >> 16) & 0xFF) != 0) {
                        px = Clear_Px();
                        px.mask |= MASK_STROKE;
                    }
                    continue; // past every arc: nothing there
                }
                const double d = SDL_max(0.0, (std::sqrt(dist2[i]) - 0.5) * inv_s);
                const double gx = (x + 0.5) * inv_s - gaps.cx;
                const double gy = (y + 0.5) * inv_s - gaps.cy;
                int32_t arc = arc_of_quadrant[Quadrant(gaps, gx, gy)];
                const int32_t edge = arc >= 0 ? outer[arc] : ring_count;
                if (d >= edge || d < first) {
                    px = Clear_Px();
                    px.mask |= MASK_STROKE;
                    continue;
                }
                double alpha = SDL_min(1.0, (edge - d) * S); // outer edge
                if (first > 0) {
                    alpha = SDL_min(alpha, (d - first) * S); // the spacer band beside the ship
                }
                alpha = SDL_min(alpha, (Gap_Distance(gaps, gx, gy) - gaps.half) * S + 0.5);
                if (alpha <= 0) {
                    px = Clear_Px();
                    px.mask |= MASK_STROKE;
                    continue;
                }
                // Ring centres sit at k + 0.5: blend the two around d.
                const double t = SDL_clamp(d - 0.5, (double)first, (double)(edge - 1));
                const int32_t k = SDL_min((int32_t)t, edge - 1);
                const int32_t k2 = SDL_min(k + 1, edge - 1);
                const double f = t - k;
                if (alpha < 1.0) {
                    // An edge: the nearer ring colour over transparency.
                    const uint8_t c = rings[f < 0.5 ? k : k2];
                    px = {0u, c | (KIND_INDEX << KIND_SHIFT) | MASK_CLEAR | ((uint32_t)Clamp_Coverage(alpha) << 16) | MASK_STROKE};
                } else {
                    const uint8_t fg = rings[k];
                    const uint8_t bg = rings[k2];
                    px = {0u, fg | ((uint32_t)bg << 8) | ((uint32_t)Clamp_Coverage(1.0 - f) << 16) | (KIND_INDEX << KIND_SHIFT) | MASK_STROKE};
                }
            }
        }
    }

} // namespace render::twin
