#include "core/palette.h"
#include "core/piece.h"
#include "spawners.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

namespace signrec {

namespace {

uint64_t clock_bits() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

uint8_t to_byte(float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); }
core::Color to_color(const float c[3]) { return {to_byte(c[0]), to_byte(c[1]), to_byte(c[2]), 255}; }

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

    int palette(core::Color* out, int max) const override {
        int n = 0;
        for (int i = 1; i <= n_fg_ && n < max; ++i) out[n++] = to_color(colors_[i]);
        if (n < max) out[n++] = to_color(colors_[0]);
        return n;
    }
    int spawner_count() const override { return static_cast<int>(spawners().size()); }
    int spawner() const override { return spawner_idx_; }
    void set_spawner(int index) override {
        spawner_idx_ = index;
        sanitize();
    }

    // Each numeric parameter has a 50% chance to move by up to +-10% of its value (at
    // least one grid step); the switches flip rarely. Colors and the spawn method are
    // untouched. Retries until the result still has bumps, otherwise leaves the
    // parameters as they were.
    void perturb() override {
        core::Rng r = time_rng();
        const Signrec before = *this;
        for (int attempt = 0; attempt < 20; ++attempt) {
            *this = before;
            perturb_once(r);
            sanitize();
            core::Rng preview(seed());
            if (generate(preview).successes > 0) return;
        }
        *this = before;
    }

    bool ui() override {
        bool c = false;

        // The spawn method is picked by hand: Randomize never switches it.
        {
            std::string items;
            for (const Spawner* s : spawners()) {
                items += s->name();
                items += '\0';
            }
            ImGui::SetNextItemWidth(190);
            if (ImGui::Combo("Spawn method", &spawner_idx_, items.c_str())) {
                sanitize();
                c = true;
            }
        }

        c |= slider(kEdgeWidth, "Edge width", &edge_width_, 3, 100);
        c |= slider(kMargin, "Margin", &margin_, 0, 300);
        sanitize(); // so the spawner's ranges are valid for the current values
        c |= active().ui(common());
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

        // Open shapes (lines) have no interior to fill.
        ImGui::BeginDisabled(!active().closed_shapes());
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
        ImGui::EndDisabled();

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
        if (ImGui::Button("Small perturbation")) {
            perturb();
            c = true;
        }
        sanitize();
        return c;
    }

    void render(core::Canvas& canvas, core::Rng& rng) override {
        canvas.clear(to_color(colors_[0]));
        sanitize();

        const std::vector<Shape> shapes = generate(rng).shapes;

        // Color assignment has its own seeds, so it never disturbs the geometry and
        // strokes/fills can be reshuffled independently. Strokes use the four
        // foregrounds; fills use all five colors (background included) but never
        // the same color as their own stroke.
        core::Rng stroke_rng(stroke_seed_);
        const int fixed_fg = 1 + core::Rng(fill_seed_).range(0, n_fg_ - 1); // for kFillOneColor
        const float hw = static_cast<float>(edge_width_) * 0.5f;
        for (const Shape& sh : shapes) {
            const int stroke_idx = 1 + stroke_rng.range(0, n_fg_ - 1);
            const int fill_idx = fill_mode_ == kFillBackground ? 0
                               : fill_mode_ == kFillEdge       ? stroke_idx
                                                               : fixed_fg;
            const core::Color col = to_color(colors_[stroke_idx]);
            if (fill_ && sh.closed) fill_orthogonal(canvas, sh.pts, to_color(colors_[fill_idx]));
            for (int i = 0; i < sh.edge_count(); ++i) {
                Rect r = stroke_rect(sh.a(i), sh.b(i), hw);
                canvas.rect({r.x0, r.y0}, {r.x1 - r.x0, r.y1 - r.y0}, col);
            }
        }
    }

private:
    struct Layout {
        std::vector<Shape> shapes;
        int successes = 0; // number of bumps actually added
    };

    // Registered spawners, indexed by spawner_idx_. Adding a method = one more member
    // and one more entry here.
    std::array<Spawner*, 2> spawners() { return {&rect_spawner_, &line_spawner_}; }
    std::array<const Spawner*, 2> spawners() const { return {&rect_spawner_, &line_spawner_}; }
    Spawner& active() { return *spawners()[static_cast<size_t>(spawner_idx_)]; }
    const Spawner& active() const { return *spawners()[static_cast<size_t>(spawner_idx_)]; }

    Common common() const { return {width(), height(), edge_width_, margin_, eps_min_, grid_}; }

    // Builds the starting shapes and runs the extrusion loop. Pure function of
    // (parameters, rng); render() and randomize() both use it.
    Layout generate(core::Rng& rng) const {
        Layout out;
        const float W = static_cast<float>(width()), H = static_cast<float>(height());
        const Common c = common();
        out.shapes = active().spawn(c);
        if (out.shapes.empty()) return out; // grid too coarse for this margin: nothing fits

        for (int iter = 0; iter < max_iters_ && out.successes < target_successes_; ++iter) {
            if (try_extrude(out.shapes, rng, c.inset(), W, H)) ++out.successes;
        }
        return out;
    }

    // One attempt: pick an edge (of any shape), split it in three, replace the
    // middle by a bump. Returns false (shapes untouched) if any constraint fails.
    bool try_extrude(std::vector<Shape>& shapes, core::Rng& rng, float inset, float W, float H) const {
        int total = 0;
        for (const Shape& s : shapes) total += s.edge_count();
        if (total == 0) return false;
        int i = rng.range(0, total - 1);
        size_t si = 0;
        while (i >= shapes[si].edge_count()) i -= shapes[si++].edge_count();

        const Shape& sh = shapes[si];
        const Vec2 A = sh.a(i), B = sh.b(i);
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

        // Candidate shape, then check the 3 new edges against everything else.
        Shape next;
        next.closed = sh.closed;
        next.pts.assign(sh.pts.begin(), sh.pts.begin() + i + 1);
        next.pts.insert(next.pts.end(), {p1, q1, q2, p2});
        next.pts.insert(next.pts.end(), sh.pts.begin() + i + 1, sh.pts.end());

        const int m = static_cast<int>(next.pts.size());
        const int ec = next.edge_count();
        const float hw = static_cast<float>(edge_width_) * 0.5f;
        const float eps = static_cast<float>(eps_min_);
        for (int e = i + 1; e <= i + 3; ++e) {          // new edges: p1q1, q1q2, q2p2
            const Rect re = stroke_rect(next.a(e), next.b(e), hw);
            for (int o = 0; o < ec; ++o) {              // same shape, non-neighbours
                if (o == e || (o + 1) % m == e || (e + 1) % m == o) continue;
                if (rect_gap(re, stroke_rect(next.a(o), next.b(o), hw)) < eps) return false;
            }
            if (!no_collide_) continue;                 // layers may overlap: skip other shapes
            for (size_t q = 0; q < shapes.size(); ++q) {
                if (q == si) continue;
                const Shape& other = shapes[q];
                for (int o = 0; o < other.edge_count(); ++o)
                    if (rect_gap(re, stroke_rect(other.a(o), other.b(o), hw)) < eps) return false;
            }
        }

        shapes[si] = std::move(next);
        return true;
    }

    int snap_round(int v) const { return common().snap_round(v); }

    // Keep parameters coherent: min <= max, everything on the grid, and the active
    // spawner's parameters valid for the shared ones.
    void sanitize() {
        grid_ = std::clamp(grid_, 1, 100);
        spawner_idx_ = std::clamp(spawner_idx_, 0, static_cast<int>(spawners().size()) - 1);
        // Distances and sizes live on the grid.
        margin_ = snap_round(margin_);
        l_min_ = std::clamp(snap_round(l_min_), 10, 300);
        d_min_ = std::clamp(snap_round(d_min_), 10, 300);
        d_max_ = std::clamp(snap_round(d_max_), 10, 600);
        d_max_ = std::max(d_max_, d_min_);
        active().sanitize(common()); // e.g. drops rectangles that don't fit
        margin_ = std::clamp(margin_, 0, active().max_margin(common()));
        active().sanitize(common()); // the margin may have changed
    }

    // Randomizes every parameter, or only the ticked ones, and keeps trying until
    // the result actually contains bumps (not just the starting shapes).
    void randomize(bool only_selected) {
        core::Rng r(clock_bits() ^ 0x9e3779b97f4a7c15ULL);
        for (int attempt = 0; attempt < 50; ++attempt) {
            randomize_once(r, only_selected);
            sanitize();
            core::Rng preview(seed()); // same seed the app will render with
            if (generate(preview).successes > 0) break;
        }
    }

    // Ranges stay coherent (d min <= d max, spawner ranges within what fits) whichever
    // subset is chosen.
    void randomize_once(core::Rng& r, bool only_selected) {
        auto on = [&](Param p) { return !only_selected || sel_[p]; };

        if (on(kGrid)) grid_ = r.range(1, 100);
        if (on(kEdgeWidth)) edge_width_ = r.range(3, 100);
        if (on(kMargin)) margin_ = r.range(0, 150);
        if (on(kLMin)) l_min_ = r.range(10, 300);
        if (on(kEps)) eps_min_ = r.range(3, 100);
        if (on(kDMin)) d_min_ = r.range(10, on(kDMax) ? 200 : std::min(200, d_max_));
        if (on(kDMax)) d_max_ = r.range(d_min_, std::min(600, d_min_ + 400));
        if (on(kSuccesses)) target_successes_ = r.range(10, 200);
        if (on(kMaxIters)) max_iters_ = r.range(2000, 20000);
        if (on(kCollide)) no_collide_ = r.chance(0.5f);
        sanitize();
        active().randomize(r, common(), only_selected);
        if (on(kFill)) fill_ = r.chance(0.5f);
        if (on(kFillMode)) fill_mode_ = r.range(0, 2);
        if (on(kPalette)) new_colors(r);
    }

    void perturb_once(core::Rng& r) {
        nudge(r, grid_, 1, 100, 1);
        nudge(r, edge_width_, 3, 100, 1);
        sanitize();
        nudge(r, margin_, 0, 300, grid_);
        nudge(r, l_min_, 10, 300, grid_);
        nudge(r, d_min_, 10, 300, grid_);
        nudge(r, d_max_, 10, 600, grid_);
        nudge(r, eps_min_, 3, 200, 1);
        nudge(r, target_successes_, 1, 500, 1);
        nudge(r, max_iters_, 1, 20000, 1);
        sanitize();
        active().perturb(r, common());
        if (r.chance(0.1f)) no_collide_ = !no_collide_;
        if (r.chance(0.1f)) fill_ = !fill_;
        if (r.chance(0.1f)) fill_mode_ = (fill_mode_ + r.range(1, 2)) % 3;
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

    // Parameters shared by every spawner (each has an "include in Randomize selected"
    // checkbox; the spawner's own parameters keep theirs inside the spawner).
    enum Param {
        kEdgeWidth, kMargin, kLMin, kDMin, kDMax, kEps,
        kGrid, kSuccesses, kMaxIters, kCollide, kFill, kFillMode, kPalette, kParamCount
    };

    // How layer interiors are colored (when fill is on).
    enum FillMode {
        kFillBackground, // always the background color
        kFillEdge,       // the same color as the layer's own outline
        kFillOneColor    // one fixed foreground (chosen by fill_seed_) for every layer
    };

    bool slider(Param p, const char* label, int* v, int lo, int hi) {
        return sel_slider(&sel_[p], label, v, lo, hi);
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

    RectSpawner rect_spawner_;
    LineSpawner line_spawner_;
    int spawner_idx_ = 0;

    int edge_width_ = 6;
    int margin_ = 60;
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

} // namespace signrec

int main() {
    signrec::Signrec piece;
    return core::run(piece);
}
