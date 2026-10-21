#include "core/palette.h"
#include "core/piece.h"

#include <delaunator.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gridpath {

namespace {

constexpr int kEmpty = -1;
constexpr float kSqrt3 = 1.73205081f;
constexpr float kPi = 3.14159265f;
enum Shape { kSquare, kHexagon, kTriangle };

uint8_t to_byte(float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); }
core::Color to_color(const float c[3]) { return {to_byte(c[0]), to_byte(c[1]), to_byte(c[2])}; }

// Smallest corner of a triangle, in degrees.
float min_angle(core::Vec2 a, core::Vec2 b, core::Vec2 c) {
    auto ang = [](core::Vec2 p, core::Vec2 q, core::Vec2 r) {
        const float ux = q.x - p.x, uy = q.y - p.y, vx = r.x - p.x, vy = r.y - p.y;
        const float cosv = (ux * vx + uy * vy) / (std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy) + 1e-9f);
        return std::acos(std::clamp(cosv, -1.0f, 1.0f)) * 180.0f / kPi;
    };
    return std::min({ang(a, b, c), ang(b, c, a), ang(c, a, b)});
}

struct Nb {
    int cell;
    bool side; // shares an edge (the only way a path steps); false = corner only
};

// The cells of one grid: each outline (already scaled) and its neighbours.
struct Grid {
    std::vector<std::vector<core::Vec2>> outline;
    std::vector<std::vector<Nb>> nb;
};

} // namespace

// Grid of square, hexagonal or triangular cells. Each path starts on a random free
// cell and grows through neighbouring cells, keeping one empty cell between itself
// and every other path.
class GridPath : public core::Piece {
public:
    GridPath() {
        core::Rng r(7);
        set_palette(core::random_palette(r));
    }

    const char* name() const override { return "gridpath"; }
    int width() const override { return w_; }
    int height() const override { return h_; }
    void set_canvas_size(int w, int h) override {
        w_ = w;
        h_ = h;
    }

    // The web page's spawn-method buttons pick the grid shape. Triangles are left out
    // for now (the mesh is not right yet), so only square and hexagon are offered.
    int spawner_count() const override { return 2; }
    int spawner() const override { return shape_; }
    void set_spawner(int index) override { shape_ = std::clamp(index, 0, spawner_count() - 1); }

    int palette(core::Color* out, int max) const override {
        int n = 0;
        for (int i = 1; i <= n_fg_ && n < max; ++i) out[n++] = to_color(colors_[i]);
        if (n < max) out[n++] = to_color(colors_[0]);
        return n;
    }

    // Each number has a 50% chance to move by up to 10% of its value (at least 1).
    // Colors and the grid shape stay.
    void perturb() override {
        core::Rng r = time_rng();
        auto nudge = [&](int& v, int lo, int hi) {
            const int d = std::max(1, v / 10);
            if (r.chance(0.5f)) v = std::clamp(v + r.range(-d, d), lo, hi);
        };
        nudge(density_, 2, 20);
        nudge(margin_, 0, 100);
        nudge(max_len_, 1, 200);
        nudge(min_len_, 1, max_len_);
        min_len_ = std::min(min_len_, max_len_);
        if (r.chance(0.5f)) scale_ = std::clamp(scale_ + 0.1f * (r.uniform() - 0.5f), 0.5f, 1.0f);
    }

    // The grid shape is picked by hand, like signrec's spawn method.
    void randomize_all() override {
        core::Rng r = time_rng();
        density_ = r.range(4, 20);
        margin_ = r.range(0, 100);
        max_len_ = r.range(2, 40);
        min_len_ = r.range(1, max_len_);
        shrink_ = r.chance(0.5f);
        scale_ = 0.5f + 0.5f * r.uniform();
        set_palette(core::random_palette(r));
    }

    bool ui() override {
        bool c = false;
        c |= ImGui::RadioButton("Square", &shape_, kSquare);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Hexagon", &shape_, kHexagon);
        // Triangles (kTriangle) are switched off until the mesh looks right.
        c |= ImGui::SliderInt("Grid density", &density_, 2, 20);
        c |= ImGui::SliderInt("Margin", &margin_, 0, 100);
        c |= ImGui::SliderInt("Max path length", &max_len_, 1, 200);
        min_len_ = std::min(min_len_, max_len_);
        c |= ImGui::SliderInt("Min path length", &min_len_, 1, max_len_);

        c |= ImGui::Checkbox("Shrink cells", &shrink_);
        ImGui::BeginDisabled(!shrink_);
        c |= ImGui::SliderFloat("Cell scale", &scale_, 0.5f, 1.0f);
        ImGui::EndDisabled();

        c |= palette_row();
        if (ImGui::Button("Randomize colors")) {
            randomize_colors();
            c = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize all")) {
            randomize_all();
            c = true;
        }
        return c;
    }

    void render(core::Canvas& canvas, core::Rng& rng) override {
        const core::Palette pal = current_palette();
        canvas.clear(pal.background);

        // The grid is laid out inside the margin, then moved by it.
        const float m = static_cast<float>(margin_);
        const float W = static_cast<float>(canvas.width()), H = static_cast<float>(canvas.height());
        Grid g = build_grid(rng, W - 2 * m, H - 2 * m, shrink_ ? scale_ : 1.0f);
        for (std::vector<core::Vec2>& poly : g.outline)
            for (core::Vec2& v : poly) {
                v.x += m;
                v.y += m;
            }
        const Paths paths = grow(rng, g, pal.foreground_count);
        for (size_t cell = 0; cell < g.outline.size(); ++cell) {
            if (paths.owner[cell] == kEmpty) continue;
            canvas.polygon(g.outline[cell], pal.foreground[paths.color[paths.owner[cell]]]);
        }
    }

private:
    struct Paths {
        std::vector<int> owner; // path index per cell, or kEmpty
        std::vector<int> color; // foreground index per path
    };

    Grid build_grid(core::Rng& rng, float W, float H, float scale) const {
        return shape_ == kTriangle ? build_triangles(rng, W, H, scale) : build_lattice(W, H, scale);
    }

    // Columns follow the density across the width. Squares (side W / cols) and hexagons
    // then fit as many rows as the height allows, so cells keep their shape on any canvas.
    int cols() const { return density_; }
    int rows(float W, float H) const {
        if (shape_ == kSquare) return std::max(1, static_cast<int>(H / (W / cols())));
        return std::max(1, 1 + static_cast<int>((H / hex_radius(W) - 2) / 1.5f));
    }
    // Circumradius of a pointy-top hexagon, so that cols() of them fit the width W.
    float hex_radius(float W) const { return W / (kSqrt3 * (cols() + 0.5f)); }

    // Square or hexagonal cells on a lattice. Odd hexagon rows are shifted right by
    // half a cell.
    Grid build_lattice(float W, float H, float scale) const {
        const int nc = cols(), nr = rows(W, H);
        const bool hex = shape_ == kHexagon;
        const float hr = hex_radius(W);
        const float cw = W / nc, sy = (H - cw * nr) / 2; // square side, and the gap that centres the rows
        const float ox = (W - kSqrt3 * hr * (nc + 0.5f)) / 2;
        const float oy = (H - hr * (2 + 1.5f * (nr - 1))) / 2;
        Grid g;
        g.outline.resize(nc * nr);
        g.nb.resize(nc * nr);
        for (int y = 0; y < nr; ++y) {
            for (int x = 0; x < nc; ++x) {
                const int cell = y * nc + x;
                std::vector<core::Vec2>& out = g.outline[cell];
                if (hex) {
                    const float cx = ox + kSqrt3 * hr * (x + 0.5f * (y & 1) + 0.5f);
                    const float cy = oy + hr + 1.5f * hr * y;
                    for (int k = 0; k < 6; ++k) {
                        const float a = (30 + 60 * k) * kPi / 180;
                        out.push_back({cx + hr * scale * std::cos(a), cy + hr * scale * std::sin(a)});
                    }
                } else {
                    const float cx = (x + 0.5f) * cw, cy = sy + (y + 0.5f) * cw;
                    const float hw = cw * scale / 2;
                    out = {{cx - hw, cy - hw}, {cx + hw, cy - hw}, {cx + hw, cy + hw}, {cx - hw, cy + hw}};
                }

                std::vector<Nb>& nb = g.nb[cell];
                auto add = [&](int dx, int dy, bool side) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < nc && ny < nr) nb.push_back({ny * nc + nx, side});
                };
                if (!hex) {
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (dx || dy) add(dx, dy, dx == 0 || dy == 0);
                } else {
                    const int lo = (y & 1) ? 0 : -1; // the rows above and below are offset by parity
                    add(1, 0, true);
                    add(-1, 0, true);
                    for (int dy : {-1, 1}) {
                        add(lo, dy, true);
                        add(lo + 1, dy, true);
                    }
                }
            }
        }
        return g;
    }

    // Each Delaunay triangle is a cell. Neighbours are triangles sharing an edge (side)
    // or only a corner.
    static std::vector<double> flatten(const std::vector<core::Vec2>& pts) {
        std::vector<double> coords;
        for (const core::Vec2& p : pts) {
            coords.push_back(p.x);
            coords.push_back(p.y);
        }
        return coords;
    }

    // Flat triangles (a corner under 15 degrees) come from near-collinear points, mostly
    // along the edge. Dropping one point of each and re-triangulating removes them. Hull
    // points go too when a sliver has no interior corner, so the hull can shrink a little.
    // shortcut: 8 passes leave about 1% slivers (22 of 2072 in the test), more passes if they show.
    void drop_slivers(std::vector<core::Vec2>& pts) const {
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<double> coords = flatten(pts);
            const delaunator::Delaunator d(coords);
            std::vector<bool> hull(pts.size(), false), drop(pts.size(), false);
            for (size_t e = 0; e < d.halfedges.size(); ++e)
                if (d.halfedges[e] == delaunator::INVALID_INDEX) {
                    hull[d.triangles[e]] = true;
                    hull[d.triangles[e % 3 == 2 ? e - 2 : e + 1]] = true;
                }
            bool any = false;
            for (size_t t = 0; t + 2 < d.triangles.size(); t += 3) {
                const std::size_t v[3] = {d.triangles[t], d.triangles[t + 1], d.triangles[t + 2]};
                if (min_angle(pts[v[0]], pts[v[1]], pts[v[2]]) >= 15.0f) continue;
                std::size_t pick = v[0];
                for (std::size_t x : v)
                    if (!hull[x]) {
                        pick = x;
                        break;
                    }
                drop[pick] = any = true;
            }
            if (!any) return;

            std::vector<core::Vec2> kept;
            for (size_t i = 0; i < pts.size(); ++i)
                if (!drop[i]) kept.push_back(pts[i]);
            std::vector<double> kept_coords = flatten(kept);
            try {
                const delaunator::Delaunator check(kept_coords);
            } catch (const std::runtime_error&) {
                return; // the removal left degenerate points: keep the last good set
            }
            pts = kept;
        }
    }

    Grid build_triangles(core::Rng& rng, float W, float H, float scale) const {
        std::vector<core::Vec2> pts = blue_noise(rng, W, H);
        Grid g;
        if (pts.size() < 3) return g; // shortcut: no triangles from fewer than 3 points, only tiny canvases
        drop_slivers(pts);

        std::vector<double> coords;
        for (const core::Vec2& p : pts) {
            coords.push_back(p.x);
            coords.push_back(p.y);
        }
        const delaunator::Delaunator d(coords);
        const int T = static_cast<int>(d.triangles.size() / 3);
        std::vector<std::vector<int>> around(pts.size()); // triangles touching each point
        for (int t = 0; t < T; ++t)
            for (int k = 0; k < 3; ++k) around[d.triangles[3 * t + k]].push_back(t);

        g.outline.resize(T);
        g.nb.resize(T);
        std::vector<int> seen(T, -1);
        for (int t = 0; t < T; ++t) {
            std::array<core::Vec2, 3> v;
            for (int k = 0; k < 3; ++k) v[k] = pts[d.triangles[3 * t + k]];
            const core::Vec2 c{(v[0].x + v[1].x + v[2].x) / 3, (v[0].y + v[1].y + v[2].y) / 3};
            for (const core::Vec2& p : v) g.outline[t].push_back({c.x + scale * (p.x - c.x), c.y + scale * (p.y - c.y)});

            std::vector<Nb>& nb = g.nb[t];
            for (int k = 0; k < 3; ++k)
                for (int u : around[d.triangles[3 * t + k]])
                    if (u != t && seen[u] != t) {
                        seen[u] = t;
                        nb.push_back({u, false});
                    }
            for (int k = 0; k < 3; ++k) {
                const std::size_t h = d.halfedges[3 * t + k];
                if (h == delaunator::INVALID_INDEX) continue; // hull edge, no triangle beyond it
                for (Nb& n : nb)
                    if (n.cell == static_cast<int>(h / 3)) n.side = true;
            }
        }
        return g;
    }

    // Bridson's blue noise over the square. Random samples on the boundary seed the
    // growth; no two points are closer than the spacing (width / density).
    std::vector<core::Vec2> blue_noise(core::Rng& rng, float W, float H) const {
        const float r = W / density_;
        const float cs = r / std::sqrt(2.0f); // one point per grid cell at most
        const int gw = static_cast<int>(W / cs) + 1, gh = static_cast<int>(H / cs) + 1;
        std::vector<int> at(gw * gh, -1);
        std::vector<core::Vec2> pts;
        std::vector<int> active;
        auto fits = [&](core::Vec2 p) {
            const int gx = static_cast<int>(p.x / cs), gy = static_cast<int>(p.y / cs);
            for (int y = std::max(0, gy - 2); y <= std::min(gh - 1, gy + 2); ++y)
                for (int x = std::max(0, gx - 2); x <= std::min(gw - 1, gx + 2); ++x) {
                    const int i = at[y * gw + x];
                    if (i < 0) continue;
                    const float dx = pts[i].x - p.x, dy = pts[i].y - p.y;
                    if (dx * dx + dy * dy < r * r) return false;
                }
            return true;
        };
        auto add = [&](core::Vec2 p) {
            at[static_cast<int>(p.y / cs) * gw + static_cast<int>(p.x / cs)] = static_cast<int>(pts.size());
            active.push_back(static_cast<int>(pts.size()));
            pts.push_back(p);
        };

        for (int i = 0; i < 8 * density_; ++i) {
            const int side = rng.range(0, 3);
            const float u = rng.uniform();
            // Pushed a little toward the centre, so the seeds don't lie exactly on the edge.
            const float in = r * 0.25f * rng.uniform();
            const core::Vec2 p = side == 0   ? core::Vec2{u * W, in}
                               : side == 1 ? core::Vec2{W - in, u * H}
                               : side == 2 ? core::Vec2{u * W, H - in}
                                           : core::Vec2{in, u * H};
            if (fits(p)) add(p);
        }
        while (!active.empty()) {
            const int k = rng.range(0, static_cast<int>(active.size()) - 1);
            const core::Vec2 o = pts[active[k]];
            bool placed = false;
            for (int attempt = 0; attempt < 30 && !placed; ++attempt) {
                const float a = rng.uniform() * 2 * kPi;
                const float d = r * (1 + rng.uniform());
                const core::Vec2 p{o.x + d * std::cos(a), o.y + d * std::sin(a)};
                if (p.x >= 0 && p.x <= W && p.y >= 0 && p.y <= H && fits(p)) {
                    add(p);
                    placed = true;
                }
            }
            if (!placed) {
                active[k] = active.back();
                active.pop_back();
            }
        }
        return pts;
    }

    // One start per cell on average is enough to fill the grid; a start on a
    // crowded cell is simply skipped.
    Paths grow(core::Rng& rng, const Grid& g, int fg_count) const {
        const int n = static_cast<int>(g.nb.size());
        Paths p{std::vector<int>(n, kEmpty), {}};
        std::vector<int> options;
        for (int a = 0; a < 4 * n; ++a) {
            const int start = rng.range(0, n - 1);
            const int id = static_cast<int>(p.color.size());
            if (!free_for(p.owner, g, start, kEmpty, id)) continue;

            p.owner[start] = id;
            std::vector<int> cells{start};
            int head = start;
            const int target = rng.range(1, max_len_);
            for (int len = 1; len < target; ++len) {
                options.clear();
                for (const Nb& nb : g.nb[head])
                    if (nb.side && free_for(p.owner, g, nb.cell, head, id)) options.push_back(nb.cell);
                if (options.empty()) break; // stuck: the path ends here
                head = options[rng.range(0, static_cast<int>(options.size()) - 1)];
                p.owner[head] = id;
                cells.push_back(head);
            }
            // Too short paths are erased, so they leave no invisible gap behind.
            if (static_cast<int>(cells.size()) < min_len_) {
                for (int c : cells) p.owner[c] = kEmpty;
                continue;
            }
            p.color.push_back(rng.range(0, fg_count - 1));
        }
        return p;
    }

    // Cell `cell` can join path `id` coming from `head` (kEmpty for a new start).
    // Other paths must stay one empty cell away, corners included. The path's own
    // cells may not share an edge (only a corner), so it cannot fold back on itself.
    bool free_for(const std::vector<int>& owner, const Grid& g, int cell, int head, int id) const {
        if (owner[cell] != kEmpty) return false;
        for (const Nb& nb : g.nb[cell]) {
            if (nb.cell == head || owner[nb.cell] == kEmpty) continue;
            if (owner[nb.cell] != id || nb.side) return false;
        }
        return true;
    }

    static core::Rng time_rng() {
        return core::Rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    void randomize_colors() override {
        core::Rng r = time_rng();
        set_palette(core::random_palette(r));
    }

    // Background swatch, then the foregrounds; each one is editable.
    bool palette_row() {
        bool changed = false;
        for (int i = 0; i <= n_fg_; ++i) {
            ImGui::PushID(i);
            changed |= ImGui::ColorEdit3("##c", colors_[i], ImGuiColorEditFlags_NoInputs);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::TextUnformatted("Palette");
        return changed;
    }

    core::Palette current_palette() const {
        core::Palette p;
        p.background = to_color(colors_[0]);
        p.foreground_count = n_fg_;
        for (int i = 0; i < n_fg_; ++i) p.foreground[i] = to_color(colors_[1 + i]);
        return p;
    }

    void set_palette(const core::Palette& p) {
        set_color(colors_[0], p.background);
        n_fg_ = p.foreground_count;
        for (int i = 0; i < n_fg_; ++i) set_color(colors_[1 + i], p.foreground[i]);
    }

    static void set_color(float dst[3], core::Color c) {
        dst[0] = c.r / 255.0f;
        dst[1] = c.g / 255.0f;
        dst[2] = c.b / 255.0f;
    }

    int w_ = 1080;
    int h_ = 1080;
    int shape_ = kSquare;
    int density_ = 20;
    int margin_ = 0;
    int max_len_ = 12;
    int min_len_ = 3;
    bool shrink_ = false;
    float scale_ = 0.8f;      // 0.5..1.0, only used when shrink_ is on
    int n_fg_ = 4;            // foregrounds in use (2..4)
    float colors_[5][3] = {}; // [0] background, [1..n_fg_] foregrounds
};

} // namespace gridpath

int main() {
    gridpath::GridPath piece;
    return core::run(piece);
}
