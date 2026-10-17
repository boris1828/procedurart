#pragma once
// Geometry shared by the signrec generator and its spawners.
#include "core/canvas.h"
#include "core/rng.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace signrec {

using core::Vec2;

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float length(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// A rectilinear polyline. Closed shapes have a last edge back to the first vertex;
// open shapes (lines) do not, so their first and last edges are not neighbours.
struct Shape {
    std::vector<Vec2> pts;
    bool closed = true;

    int edge_count() const {
        const int n = static_cast<int>(pts.size());
        return closed ? n : std::max(0, n - 1);
    }
    // Edge i runs from a(i) to b(i).
    Vec2 a(int i) const { return pts[static_cast<size_t>(i)]; }
    Vec2 b(int i) const { return pts[(static_cast<size_t>(i) + 1) % pts.size()]; }
};

// Every edge is axis-aligned and drawn as a rectangle: the segment inflated by
// half the stroke width on all sides (so the ends are square, not round).
struct Rect {
    float x0, y0, x1, y1;
};

inline Rect stroke_rect(Vec2 a, Vec2 b, float hw) {
    return {std::min(a.x, b.x) - hw, std::min(a.y, b.y) - hw,
            std::max(a.x, b.x) + hw, std::max(a.y, b.y) + hw};
}

// Distance between two rectangles (0 if they touch or overlap).
inline float rect_gap(const Rect& r, const Rect& s) {
    float dx = std::max({0.0f, r.x0 - s.x1, s.x0 - r.x1});
    float dy = std::max({0.0f, r.y0 - s.y1, s.y0 - r.y1});
    return std::sqrt(dx * dx + dy * dy);
}

// Fills the interior of a closed axis-aligned polygon with horizontal slabs: between
// two consecutive vertex heights, the vertical edges crossing the slab pair up
// left-to-right and each pair bounds a filled rectangle.
inline void fill_orthogonal(core::Canvas& canvas, const std::vector<Vec2>& p, core::Color col) {
    std::vector<float> ys;
    for (Vec2 v : p) ys.push_back(v.y);
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());

    for (size_t s = 0; s + 1 < ys.size(); ++s) {
        const float y0 = ys[s], y1 = ys[s + 1], mid = 0.5f * (y0 + y1);
        std::vector<float> xs;
        for (size_t i = 0; i < p.size(); ++i) {
            const Vec2 a = p[i], b = p[(i + 1) % p.size()];
            if (a.x != b.x) continue; // only vertical edges
            if (std::min(a.y, b.y) < mid && mid < std::max(a.y, b.y)) xs.push_back(a.x);
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2)
            canvas.rect({xs[k], y0}, {xs[k + 1] - xs[k], y1 - y0}, col);
    }
}

// 50% chance to move `v` by up to +-10% of its value (at least `step`), clamped to
// [lo, hi]. Used by the "small perturbation" of every parameter.
inline void nudge(core::Rng& r, int& v, int lo, int hi, int step) {
    constexpr float kAmount = 0.10f;
    if (!r.chance(0.5f)) return;
    int d = static_cast<int>(std::lround(static_cast<float>(v) * r.range(-kAmount, kAmount)));
    if (std::abs(d) < step) d = r.chance(0.5f) ? step : -step;
    v = std::clamp(v + d, lo, hi);
}

} // namespace signrec
