#include "core/palette.h"
#include "core/piece.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gridpath {

namespace {

constexpr int kEmpty = -1;
constexpr float kSqrt3 = 1.73205081f;
constexpr float kPi = 3.14159265f;
enum Shape { kSquare, kHexagon };
// How a filled cell is drawn: its shape, the circle inside it, the shape with a
// background-colored hole that leaves a margin to the edge, or the circle with that hole.
enum Fill { kShape, kCircle, kHole, kCircleHole };
constexpr float kHoleRatio = 0.7f; // hole radius as a share of the inscribed circle

uint8_t to_byte(float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); }
core::Color to_color(const float c[3]) { return {to_byte(c[0]), to_byte(c[1]), to_byte(c[2])}; }

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

// Grid of square or hexagonal cells. Each path starts on a random free cell and grows
// through neighbouring cells, keeping one empty cell between itself and every other
// path. A filled cell is drawn in one of four ways (Fill); the piece picks one at random.
class GridPath : public core::Piece {
public:
    GridPath() {
        core::Rng r(7);
        set_palette(core::random_palette(r));
        fill_ = static_cast<Fill>(r.range(kShape, kCircleHole));
    }

    const char* name() const override { return "gridpath"; }
    int width() const override { return w_; }
    int height() const override { return h_; }
    void set_canvas_size(int w, int h) override {
        w_ = w;
        h_ = h;
    }

    // The web page's spawn-method buttons pick the grid shape.
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
        nudge(inset_, 50, 95);
    }

    // The grid shape is picked by hand, like signrec's spawn method.
    void randomize_all() override {
        core::Rng r = time_rng();
        density_ = r.range(4, 20);
        margin_ = r.range(0, 100);
        max_len_ = r.range(2, 40);
        min_len_ = r.range(1, max_len_);
        inset_ = r.range(50, 95);
        fill_ = static_cast<Fill>(r.range(kShape, kCircleHole));
        set_palette(core::random_palette(r));
    }

    bool ui() override {
        bool c = false;
        c |= ImGui::RadioButton("Square", &shape_, kSquare);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Hexagon", &shape_, kHexagon);
        c |= ImGui::SliderInt("Grid density", &density_, 2, 20);
        c |= ImGui::SliderInt("Margin", &margin_, 0, 100);
        c |= ImGui::SliderInt("Max path length", &max_len_, 1, 200);
        min_len_ = std::min(min_len_, max_len_);
        c |= ImGui::SliderInt("Min path length", &min_len_, 1, max_len_);
        c |= ImGui::SliderInt("Inset", &inset_, 50, 95, "%d%%");

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
        Grid g = build_lattice(W - 2 * m, H - 2 * m, inset_ / 100.0f);
        for (std::vector<core::Vec2>& poly : g.outline)
            for (core::Vec2& v : poly) {
                v.x += m;
                v.y += m;
            }
        const Paths paths = grow(rng, g, pal.foreground_count);
        for (size_t cell = 0; cell < g.outline.size(); ++cell) {
            if (paths.owner[cell] == kEmpty) continue;
            const std::vector<core::Vec2>& poly = g.outline[cell];
            const core::Color fill = pal.foreground[paths.color[paths.owner[cell]]];
            if (fill_ == kShape) {
                canvas.polygon(poly, fill);
                continue;
            }

            // Centre: the midpoint of two opposite corners. Radius: centre to the middle of a
            // side, which is the inscribed circle of a square or hexagon.
            const core::Vec2 a = poly[0], b = poly[1], opposite = poly[poly.size() / 2];
            const core::Vec2 c{(a.x + opposite.x) / 2, (a.y + opposite.y) / 2};
            const core::Vec2 side{(a.x + b.x) / 2, (a.y + b.y) / 2};
            const float r = std::hypot(side.x - c.x, side.y - c.y);
            const bool round = fill_ == kCircle || fill_ == kCircleHole;
            if (round) canvas.circle(c, r, fill);
            else canvas.polygon(poly, fill);
            if (fill_ == kHole || fill_ == kCircleHole) canvas.circle(c, kHoleRatio * r, pal.background);
        }
    }

private:
    struct Paths {
        std::vector<int> owner; // path index per cell, or kEmpty
        std::vector<int> color; // foreground index per path
    };

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
    int inset_ = 80;          // percent of each cell kept, 50..95
    Fill fill_ = kShape;
    int n_fg_ = 4;            // foregrounds in use (2..4)
    float colors_[5][3] = {}; // [0] background, [1..n_fg_] foregrounds
};

} // namespace gridpath

int main() {
    gridpath::GridPath piece;
    return core::run(piece);
}
