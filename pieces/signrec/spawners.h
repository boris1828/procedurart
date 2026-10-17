#pragma once
#include "spawner.h"

namespace signrec {

// Concentric closed rectangles, each inset `spacing` more than the previous one.
class RectSpawner : public Spawner {
public:
    const char* name() const override { return "Rectangles"; }
    bool closed_shapes() const override { return true; }

    std::vector<Shape> spawn(const Common& c) const override {
        std::vector<Shape> out;
        const float W = static_cast<float>(c.width), H = static_cast<float>(c.height);
        for (int k = 0; k < rects_; ++k) {
            // Corners are snapped onto the grid, rounding towards the page centre
            // so the margin is never violated.
            const float o = c.inset() + static_cast<float>(k * spacing_);
            const float x0 = c.snap_up(o), y0 = c.snap_up(o);
            const float x1 = c.snap_down(W - o), y1 = c.snap_down(H - o);
            if (x1 <= x0 || y1 <= y0) break; // a coarse grid can collapse the inner rectangles
            out.push_back({{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true});
        }
        return out;
    }

    void sanitize(const Common& c) override {
        rects_ = std::clamp(rects_, 1, 4);
        const int lo = c.min_gap();
        while (rects_ > 1 && (rects_ - 1) * lo > c.room()) --rects_; // doesn't fit: drop rectangles
        auto [sp_lo, sp_hi] = spacing_range(c);
        spacing_ = std::clamp(c.snap_round(spacing_), sp_lo, sp_hi);
    }

    int max_margin(const Common& c) const override {
        return std::max(0, c.room() - (rects_ - 1) * c.min_gap());
    }

    bool ui(const Common& c) override {
        bool changed = sel_slider(&sel_[0], "Rectangles", &rects_, 1, 4);
        sanitize(c); // so the spacing range below is valid
        auto [sp_lo, sp_hi] = spacing_range(c);
        ImGui::BeginDisabled(rects_ < 2);
        changed |= sel_slider(&sel_[1], "Rect spacing", &spacing_, sp_lo, sp_hi);
        ImGui::EndDisabled();
        return changed;
    }

    void randomize(core::Rng& r, const Common& c, bool only_selected) override {
        auto on = [&](int i) { return !only_selected || sel_[i]; };
        if (on(0)) rects_ = r.range(1, 4);
        sanitize(c);
        if (on(1)) {
            auto [sp_lo, sp_hi] = spacing_range(c);
            spacing_ = r.range(sp_lo, sp_hi);
        }
    }

    void perturb(core::Rng& r, const Common& c) override {
        if (r.chance(0.5f)) rects_ = std::clamp(rects_ + (r.chance(0.5f) ? 1 : -1), 1, 4);
        sanitize(c);
        if (rects_ > 1) {
            auto [sp_lo, sp_hi] = spacing_range(c);
            nudge(r, spacing_, sp_lo, sp_hi, c.grid);
        }
    }

private:
    // Centerline distance between neighbouring rectangles: at least stroke width +
    // epsilon (so the start state is valid), at most what fits in the canvas.
    std::pair<int, int> spacing_range(const Common& c) const {
        const int lo = c.min_gap();
        const int hi = rects_ > 1 ? (c.room() - c.margin) / (rects_ - 1) : lo;
        return {lo, std::max(lo, hi)};
    }

    bool sel_[2] = {true, true};
    int rects_ = 1;
    int spacing_ = 120;
};

// n parallel straight lines, evenly spread between the margins. The margins are in
// screen axes: h_margin is the distance from the left/right sides, v_margin from the
// top/bottom, whichever way the lines run. Across the lines it is a start position, never
// closer to the border than the common margin allows; along them it can be 0, so the
// lines start exactly on the border. Lines are open, so there is nothing to fill.
class LineSpawner : public Spawner {
public:
    const char* name() const override { return "Lines"; }
    bool closed_shapes() const override { return false; }

    std::vector<Shape> spawn(const Common& c) const override {
        std::vector<Shape> out;
        const bool vert = orientation_ == 1;
        // Lines run "along" one axis and are spread over the other ("across").
        const float along_len = static_cast<float>(vert ? c.height : c.width);
        const float across_len = static_cast<float>(vert ? c.width : c.height);
        const float along_m = static_cast<float>(vert ? v_margin_ : h_margin_);
        const float across_m = static_cast<float>(vert ? h_margin_ : v_margin_);

        // Along the lines the margin is the gap between the border and the stroke's outer
        // end, so 0 makes the lines start exactly on the border (the common margin does
        // not apply there). The end caps stick out half a stroke past the endpoints.
        const float hw = static_cast<float>(c.edge_width) * 0.5f;
        const float a0 = along_m == 0.0f ? hw : c.snap_up(along_m + hw);
        const float a1 = along_m == 0.0f ? along_len - hw : c.snap_down(along_len - along_m - hw);
        if (a1 <= a0) return out;

        std::vector<float> pos;
        if (count_ == 1) {
            pos.push_back(c.snap_nearest(across_len * 0.5f));
        } else {
            const float step = line_step(c);
            const float total = step * static_cast<float>(count_ - 1);
            const float slack = across_len - 2.0f * across_m - static_cast<float>(c.grid) - total;
            const float start = c.snap_up(across_m + slack * 0.5f);
            for (int i = 0; i < count_; ++i) pos.push_back(start + step * static_cast<float>(i));
        }
        for (float p : pos)
            out.push_back({vert ? std::vector<Vec2>{{p, a0}, {p, a1}} : std::vector<Vec2>{{a0, p}, {a1, p}}, false});
        return out;
    }

    void sanitize(const Common& c) override {
        orientation_ = std::clamp(orientation_, 0, 1);
        h_margin_ = std::clamp(c.snap_round(h_margin_), h_lo(c), margin_hi(c.width, c));
        v_margin_ = std::clamp(c.snap_round(v_margin_), v_lo(c), margin_hi(c.height, c));
        count_ = std::clamp(count_, 1, max_lines(c));
    }

    bool ui(const Common& c) override {
        bool changed = false;
        sanitize(c);
        changed |= sel_slider(&sel_[0], "Lines", &count_, 1, max_lines(c));

        ImGui::PushID("orientation");
        ImGui::Checkbox("##sel", &sel_[1]);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Horizontal", &orientation_, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Vertical", &orientation_, 1);
        ImGui::PopID();
        sanitize(c); // the count limit depends on the orientation

        changed |= sel_slider(&sel_[2], "H margin", &h_margin_, h_lo(c), margin_hi(c.width, c));
        changed |= sel_slider(&sel_[3], "V margin", &v_margin_, v_lo(c), margin_hi(c.height, c));
        sanitize(c);
        return changed;
    }

    void randomize(core::Rng& r, const Common& c, bool only_selected) override {
        auto on = [&](int i) { return !only_selected || sel_[i]; };
        if (on(1)) orientation_ = r.range(0, 1);
        const int hlo = h_lo(c), vlo = v_lo(c);
        if (on(2)) h_margin_ = r.range(hlo, std::max(hlo, std::min(margin_hi(c.width, c), hlo + 300)));
        if (on(3)) v_margin_ = r.range(vlo, std::max(vlo, std::min(margin_hi(c.height, c), vlo + 300)));
        sanitize(c);
        if (on(0)) count_ = r.range(1, std::min(10, max_lines(c)));
        sanitize(c);
    }

    void perturb(core::Rng& r, const Common& c) override {
        sanitize(c);
        nudge(r, h_margin_, h_lo(c), margin_hi(c.width, c), c.grid);
        nudge(r, v_margin_, v_lo(c), margin_hi(c.height, c), c.grid);
        sanitize(c);
        if (r.chance(0.5f)) count_ += r.chance(0.5f) ? 1 : -1;
        sanitize(c);
    }

private:
    static constexpr int kMaxLines = 20;

    // Across the lines the margin is a centerline distance, at least the common inset.
    // Along them it is the gap to the stroke's end and may be 0 (flush with the border).
    // At most leaving 100px (half-length 50) of line or of spread.
    static int margin_lo(const Common& c) { return static_cast<int>(std::ceil(c.inset())); }
    static int margin_hi(int side, const Common& c) { return std::max(margin_lo(c), side / 2 - 50); }
    int h_lo(const Common& c) const { return orientation_ == 0 ? 0 : margin_lo(c); } // horizontal lines run along x
    int v_lo(const Common& c) const { return orientation_ == 1 ? 0 : margin_lo(c); } // vertical lines run along y

    // Distance between neighbouring lines for the current count (grid multiples). One grid
    // step is reserved so snapping the first line can never push the last one out.
    float line_step(const Common& c) const { return step_for(count_, c); }
    float step_for(int n, const Common& c) const {
        const float across_len = static_cast<float>(orientation_ == 1 ? c.width : c.height);
        const float across_m = static_cast<float>(orientation_ == 1 ? h_margin_ : v_margin_);
        const float usable = across_len - 2.0f * across_m - static_cast<float>(c.grid);
        return std::floor(usable / static_cast<float>(n - 1) / static_cast<float>(c.grid)) *
               static_cast<float>(c.grid);
    }

    // Most lines that still keep width + epsilon between neighbours.
    int max_lines(const Common& c) const {
        int n = kMaxLines;
        while (n > 1 && step_for(n, c) < static_cast<float>(c.min_gap())) --n;
        return n;
    }

    bool sel_[4] = {true, true, true, true};
    int count_ = 4;
    int orientation_ = 0; // 0 = horizontal lines, 1 = vertical lines
    int h_margin_ = 120;
    int v_margin_ = 300;
};

} // namespace signrec
