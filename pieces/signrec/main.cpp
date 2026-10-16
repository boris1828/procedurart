#include "core/palette.h"
#include "core/piece.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

using core::Vec2;

namespace {

uint64_t clock_bits() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

uint8_t to_byte(float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); }
core::Color to_color(const float c[3]) { return {to_byte(c[0]), to_byte(c[1]), to_byte(c[2]), 255}; }

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
float length(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

// Every edge is axis-aligned and drawn as a rectangle: the segment inflated by
// half the stroke width on all sides (so the ends are square, not round).
struct Rect {
    float x0, y0, x1, y1;
};

Rect stroke_rect(Vec2 a, Vec2 b, float hw) {
    return {std::min(a.x, b.x) - hw, std::min(a.y, b.y) - hw,
            std::max(a.x, b.x) + hw, std::max(a.y, b.y) + hw};
}

// Distance between two rectangles (0 if they touch or overlap).
float rect_gap(const Rect& r, const Rect& s) {
    float dx = std::max({0.0f, r.x0 - s.x1, s.x0 - r.x1});
    float dy = std::max({0.0f, r.y0 - s.y1, s.y0 - r.y1});
    return std::sqrt(dx * dx + dy * dy);
}

// Fills the interior of a closed axis-aligned polygon with horizontal slabs: between
// two consecutive vertex heights, the vertical edges crossing the slab pair up
// left-to-right and each pair bounds a filled rectangle.
void fill_orthogonal(core::Canvas& canvas, const std::vector<Vec2>& p, core::Color col) {
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

} // namespace

class Signrec : public core::Piece {
public:
    Signrec() {
        std::fill(std::begin(sel_), std::end(sel_), true);
        core::Rng r(7);
        set_palette(core::random_palette(r));
    }

    const char* name() const override { return "signrec"; }
    int width() const override { return w_; }
    int height() const override { return h_; }

    void set_canvas_size(int w, int h) override {
        w_ = w;
        h_ = h;
        sanitize();
    }
    void randomize_all() override { randomize(false); }
    void randomize_colors() override {
        core::Rng r = time_rng();
        new_colors(r);
    }

    bool ui() override {
        bool c = false;
        c |= slider(kEdgeWidth, "Edge width", &edge_width_, 3, 100);
        c |= slider(kMargin, "Margin", &margin_, 0, 300);
        c |= slider(kRects, "Rectangles", &rects_, 1, 4);
        sanitize(); // so the spacing range below is valid for the current values
        auto [sp_lo, sp_hi] = spacing_range();
        ImGui::BeginDisabled(rects_ < 2);
        c |= slider(kSpacing, "Rect spacing", &spacing_, sp_lo, sp_hi);
        ImGui::EndDisabled();
        c |= slider(kLMin, "l min", &l_min_, 10, 300);
        c |= slider(kDMin, "d min", &d_min_, 10, 300);
        c |= slider(kDMax, "d max", &d_max_, 10, 600);
        c |= slider(kEps, "epsilon min", &eps_min_, 3, 200);
        c |= slider(kGrid, "Grid step", &grid_, 1, 100);
        c |= slider(kSuccesses, "Successful iters", &target_successes_, 1, 500);
        c |= slider(kMaxIters, "Max iters", &max_iters_, 1, 20000);

        ImGui::PushID(kCollide);
        ImGui::Checkbox("##sel", &sel_[kCollide]);
        ImGui::SameLine();
        c |= ImGui::Checkbox("No layer collisions", &no_collide_);
        ImGui::PopID();

        ImGui::PushID(kFill);
        ImGui::Checkbox("##sel", &sel_[kFill]);
        ImGui::SameLine();
        c |= ImGui::Checkbox("Fill layers", &fill_);
        ImGui::PopID();

        ImGui::PushID(kFillMode);
        ImGui::Checkbox("##sel", &sel_[kFillMode]);
        ImGui::SameLine();
        ImGui::BeginDisabled(!fill_);
        c |= ImGui::RadioButton("Background", &fill_mode_, kFillBackground);
        ImGui::SameLine();
        c |= ImGui::RadioButton("Edge", &fill_mode_, kFillEdge);
        ImGui::SameLine();
        c |= ImGui::RadioButton("One color", &fill_mode_, kFillOneColor);
        ImGui::EndDisabled();
        ImGui::PopID();

        c |= palette_row();

        if (ImGui::Button("Randomize colors")) {
            randomize_colors();
            c = true;
        }
        if (ImGui::Button("Swap colors")) {
            core::Rng r = time_rng();
            if (swap_strokes_) stroke_seed_ = r.next_u32();
            if (swap_fills_) fill_seed_ = r.next_u32();
            c |= swap_strokes_ || swap_fills_;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Strokes", &swap_strokes_);
        ImGui::SameLine();
        ImGui::Checkbox("Fills", &swap_fills_);

        ImGui::Separator();
        if (ImGui::Button("Randomize all")) {
            randomize(false);
            c = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize selected")) {
            randomize(true);
            c = true;
        }
        sanitize();
        return c;
    }

    void render(core::Canvas& canvas, core::Rng& rng) override {
        canvas.clear(to_color(colors_[0]));
        sanitize();

        const std::vector<Polygon> polys = generate(rng).polys;

        // Color assignment has its own seeds, so it never disturbs the geometry and
        // strokes/fills can be reshuffled independently. Strokes use the four
        // foregrounds; fills use all five colors (background included) but never
        // the same color as their own stroke.
        core::Rng stroke_rng(stroke_seed_);
        const int fixed_fg = 1 + core::Rng(fill_seed_).range(0, n_fg_ - 1); // for kFillOneColor
        const float hw = static_cast<float>(edge_width_) * 0.5f;
        for (const Polygon& pts : polys) {
            const int stroke_idx = 1 + stroke_rng.range(0, n_fg_ - 1);
            const int fill_idx = fill_mode_ == kFillBackground ? 0
                               : fill_mode_ == kFillEdge       ? stroke_idx
                                                               : fixed_fg;
            const core::Color col = to_color(colors_[stroke_idx]);
            if (fill_) fill_orthogonal(canvas, pts, to_color(colors_[fill_idx]));
            for (size_t i = 0; i < pts.size(); ++i) {
                Rect r = stroke_rect(pts[i], pts[(i + 1) % pts.size()], hw);
                canvas.rect({r.x0, r.y0}, {r.x1 - r.x0, r.y1 - r.y0}, col);
            }
        }
    }

private:
    using Polygon = std::vector<Vec2>;

    struct Layout {
        std::vector<Polygon> polys;
        int successes = 0; // number of bumps actually added
    };

    // Builds the starting rectangles and runs the extrusion loop. Pure function of
    // (parameters, rng); render() and randomize() both use it.
    Layout generate(core::Rng& rng) const {
        Layout out;
        const float W = static_cast<float>(width()), H = static_cast<float>(height());
        // Centerlines never leave the box inset_ from the border, so the stroke's
        // outer side stays at least `margin_` from the page edge.
        const float inset = inset_();
        // Each polygon is closed: edge i goes from p[i] to p[(i+1) % n].
        // Rectangle k is inset by k * spacing_ more than the outermost one.
        for (int k = 0; k < rects_; ++k) {
            // Corners are snapped onto the grid, rounding towards the page centre
            // so the margin is never violated.
            const float o = inset + static_cast<float>(k * spacing_);
            const float x0 = snap_up(o), y0 = snap_up(o);
            const float x1 = snap_down(W - o), y1 = snap_down(H - o);
            if (x1 <= x0 || y1 <= y0) break; // a coarse grid can collapse the inner rectangles
            out.polys.push_back({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
        }
        if (out.polys.empty()) return out; // grid too coarse for this margin: nothing fits

        for (int iter = 0; iter < max_iters_ && out.successes < target_successes_; ++iter) {
            if (try_extrude(out.polys, rng, inset, W, H)) ++out.successes;
        }
        return out;
    }

    // One attempt: pick an edge (of any polygon), split it in three, replace the
    // middle by a bump. Returns false (polys untouched) if any constraint fails.
    bool try_extrude(std::vector<Polygon>& polys, core::Rng& rng, float inset, float W, float H) const {
        int total = 0;
        for (const Polygon& p : polys) total += static_cast<int>(p.size());
        int i = rng.range(0, total - 1);
        size_t pi = 0;
        while (i >= static_cast<int>(polys[pi].size())) i -= static_cast<int>(polys[pi++].size());

        const Polygon& pts = polys[pi];
        const int n = static_cast<int>(pts.size());
        const Vec2 A = pts[i], B = pts[(i + 1) % n];
        const float L = length(B - A);

        // Edges are axis-aligned: exact unit direction, no float drift.
        const Vec2 t = {static_cast<float>((B.x > A.x) - (B.x < A.x)),
                        static_cast<float>((B.y > A.y) - (B.y < A.y))};
        const Vec2 normal = {-t.y, t.x};

        // Work in grid units so every new vertex lands on the grid.
        const int g = grid_;
        const int Lg = static_cast<int>(std::lround(L / static_cast<float>(g)));
        const int mg = (l_min_ + g - 1) / g; // l min, rounded up to the grid
        if (Lg < 3 * mg) return false;
        const int lk = rng.range(mg, Lg - 2 * mg);
        const int r1k = rng.range(mg, Lg - lk - mg);
        const int dlo = (d_min_ + g - 1) / g;
        const int dk = rng.range(dlo, std::max(dlo, d_max_ / g));

        const float l = static_cast<float>(lk * g), r1 = static_cast<float>(r1k * g);
        float d = static_cast<float>(dk * g);
        if (rng.chance(0.5f)) d = -d;

        const Vec2 p1 = A + t * r1, p2 = A + t * (r1 + l);
        const Vec2 q1 = p1 + normal * d, q2 = p2 + normal * d;

        // New vertices must stay inside the forbidden-margin box.
        for (Vec2 q : {q1, q2})
            if (q.x < inset || q.x > W - inset || q.y < inset || q.y > H - inset) return false;

        // Candidate polygon, then check the 3 new edges against everything else.
        Polygon next(pts.begin(), pts.begin() + i + 1);
        next.insert(next.end(), {p1, q1, q2, p2});
        next.insert(next.end(), pts.begin() + i + 1, pts.end());

        const int m = static_cast<int>(next.size());
        const float hw = static_cast<float>(edge_width_) * 0.5f;
        const float eps = static_cast<float>(eps_min_);
        for (int e = i + 1; e <= i + 3; ++e) {          // new edges: p1q1, q1q2, q2p2
            const Rect re = stroke_rect(next[e], next[(e + 1) % m], hw);
            for (int o = 0; o < m; ++o) {               // same layer, non-neighbours
                if (o == e || (o + 1) % m == e || (e + 1) % m == o) continue;
                if (rect_gap(re, stroke_rect(next[o], next[(o + 1) % m], hw)) < eps) return false;
            }
            if (!no_collide_) continue;                 // layers may overlap: skip other layers
            for (size_t q = 0; q < polys.size(); ++q) { // every edge of the other layers
                if (q == pi) continue;
                const Polygon& other = polys[q];
                for (size_t o = 0; o < other.size(); ++o)
                    if (rect_gap(re, stroke_rect(other[o], other[(o + 1) % other.size()], hw)) < eps)
                        return false;
            }
        }

        polys[pi] = std::move(next);
        return true;
    }

    float inset_() const { return static_cast<float>(margin_) + static_cast<float>(edge_width_) * 0.5f; }

    int snap_round(int v) const { return ((v + grid_ / 2) / grid_) * grid_; }
    float snap_up(float v) const { return std::ceil(v / static_cast<float>(grid_)) * static_cast<float>(grid_); }
    float snap_down(float v) const { return std::floor(v / static_cast<float>(grid_)) * static_cast<float>(grid_); }

    // Centerline distance between neighbouring rectangles: at least stroke width +
    // epsilon (so the start state is valid), at most what fits in the canvas.
    std::pair<int, int> spacing_range() const {
        const int lo = edge_width_ + eps_min_;
        const int hi = rects_ > 1 ? (room_() - margin_) / (rects_ - 1) : lo;
        return {lo, std::max(lo, hi)};
    }

    // Half the short side, minus a minimum 50px for the innermost rectangle.
    int room_() const { return std::min(width(), height()) / 2 - 50 - (edge_width_ + 1) / 2; }

    // Keep parameters coherent: min <= max, and every rectangle has room.
    void sanitize() {
        grid_ = std::clamp(grid_, 1, 100);
        // Distances and sizes live on the grid.
        margin_ = snap_round(margin_);
        spacing_ = snap_round(spacing_);
        l_min_ = std::clamp(snap_round(l_min_), 10, 300);
        d_min_ = std::clamp(snap_round(d_min_), 10, 300);
        d_max_ = std::clamp(snap_round(d_max_), 10, 600);
        d_max_ = std::max(d_max_, d_min_);
        rects_ = std::clamp(rects_, 1, 4);
        const int lo = edge_width_ + eps_min_;
        while (rects_ > 1 && (rects_ - 1) * lo > room_()) --rects_; // doesn't fit: drop rectangles
        margin_ = std::clamp(margin_, 0, std::max(0, room_() - (rects_ - 1) * lo));
        auto [sp_lo, sp_hi] = spacing_range();
        spacing_ = std::clamp(spacing_, sp_lo, sp_hi);
    }

    // Randomizes every parameter, or only the ticked ones, and keeps trying until
    // the result actually contains bumps (not just the bare starting rectangles).
    void randomize(bool only_selected) {
        core::Rng r(clock_bits() ^ 0x9e3779b97f4a7c15ULL);
        for (int attempt = 0; attempt < 50; ++attempt) {
            randomize_once(r, only_selected);
            sanitize();
            core::Rng preview(seed()); // same seed the app will render with
            if (generate(preview).successes > 0) break;
        }
    }

    // Ranges stay coherent (d min <= d max, spacing within what fits) whichever
    // subset is chosen.
    void randomize_once(core::Rng& r, bool only_selected) {
        auto on = [&](Param p) { return !only_selected || sel_[p]; };

        if (on(kGrid)) grid_ = r.range(1, 100);
        if (on(kEdgeWidth)) edge_width_ = r.range(3, 100);
        if (on(kMargin)) margin_ = r.range(0, 150);
        if (on(kRects)) rects_ = r.range(1, 4);
        if (on(kLMin)) l_min_ = r.range(10, 300);
        if (on(kEps)) eps_min_ = r.range(3, 100);
        if (on(kDMin)) d_min_ = r.range(10, on(kDMax) ? 200 : std::min(200, d_max_));
        if (on(kDMax)) d_max_ = r.range(d_min_, std::min(600, d_min_ + 400));
        if (on(kSuccesses)) target_successes_ = r.range(10, 200);
        if (on(kMaxIters)) max_iters_ = r.range(2000, 20000);
        if (on(kCollide)) no_collide_ = r.chance(0.5f);
        sanitize();
        if (on(kSpacing)) {
            auto [sp_lo, sp_hi] = spacing_range();
            spacing_ = r.range(sp_lo, sp_hi);
        }
        if (on(kFill)) fill_ = r.chance(0.5f);
        if (on(kFillMode)) fill_mode_ = r.range(0, 2);
        if (on(kPalette)) new_colors(r);
    }

    static core::Rng time_rng() {
        return core::Rng(clock_bits() ^ 0x2545f4914f6cdd1dULL);
    }

    // New palette plus new stroke/fill assignments; geometry is untouched.
    void new_colors(core::Rng& r) {
        set_palette(core::random_palette(r));
        stroke_seed_ = r.next_u32();
        fill_seed_ = r.next_u32();
    }

    enum Param {
        kEdgeWidth, kMargin, kRects, kSpacing, kLMin, kDMin, kDMax, kEps,
        kGrid, kSuccesses, kMaxIters, kCollide, kFill, kFillMode, kPalette, kParamCount
    };

    // How layer interiors are colored (when fill is on).
    enum FillMode {
        kFillBackground, // always the background color
        kFillEdge,       // the same color as the layer's own outline
        kFillOneColor    // one fixed foreground (chosen by fill_seed_) for every layer
    };

    // A checkbox ("include in Randomize selected") to the left of each control.
    bool slider(Param p, const char* label, int* v, int lo, int hi) {
        ImGui::PushID(p);
        ImGui::Checkbox("##sel", &sel_[p]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(190);
        bool changed = ImGui::SliderInt(label, v, lo, hi);
        ImGui::PopID();
        return changed;
    }

    // Background swatch followed by the four foreground swatches; each one is
    // editable, and the checkbox includes the whole palette in randomization.
    bool palette_row() {
        bool changed = false;
        ImGui::PushID(kPalette);
        ImGui::Checkbox("##sel", &sel_[kPalette]);
        for (int i = 0; i <= n_fg_; ++i) {
            ImGui::SameLine();
            ImGui::PushID(i);
            changed |= ImGui::ColorEdit3("##c", colors_[i], ImGuiColorEditFlags_NoInputs);
            ImGui::PopID();
            if (i == 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Palette");
        ImGui::PopID();
        return changed;
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

    bool sel_[kParamCount];
    int w_ = 1080;
    int h_ = 1920;
    int edge_width_ = 6;
    int margin_ = 60;
    int rects_ = 1;
    int spacing_ = 120;
    int l_min_ = 60;
    int d_min_ = 40;
    int d_max_ = 200;
    int eps_min_ = 20;
    int grid_ = 1;
    int target_successes_ = 40;
    int max_iters_ = 5000;
    bool no_collide_ = true;
    bool fill_ = false;
    int fill_mode_ = kFillBackground;
    uint32_t stroke_seed_ = 1; // which foreground each layer's outline uses
    uint32_t fill_seed_ = 2;   // which foreground "One color" fill uses
    bool swap_strokes_ = true;
    bool swap_fills_ = true;
    int n_fg_ = 4;            // number of foregrounds in use (2..4), set by the palette
    float colors_[5][3] = {}; // [0] background, [1..n_fg_] foregrounds
};

int main() {
    Signrec piece;
    return core::run(piece);
}
