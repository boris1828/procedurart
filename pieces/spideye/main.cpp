#include "core/palette.h"
#include "core/piece.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace spideye {

namespace {

constexpr int kTries = 50;    // failed placements before a set starts again from zero
constexpr int kRestarts = 20; // after this many restarts the last, partial set is kept
constexpr int kSegments = 64; // outline points per eye, for drawing and for overlap tests
constexpr float kPi = 3.14159265f;

// An eye in the left half of its cell: centre (x, y) relative to the cell centre, semi-axes
// a (long) and b (short), rotated by t. A circle has a == b.
struct Eye {
    float x, y, a, b, t;
};

uint8_t to_byte(float v) { return static_cast<uint8_t>(v * 255.0f + 0.5f); }
core::Color to_color(const float c[3]) { return {to_byte(c[0]), to_byte(c[1]), to_byte(c[2])}; }
void set_color(float dst[3], core::Color c) {
    dst[0] = c.r / 255.0f;
    dst[1] = c.g / 255.0f;
    dst[2] = c.b / 255.0f;
}

// Outline point at parameter k of the eye, relative to the cell centre.
core::Vec2 point_on(const Eye& e, float k) {
    const float u = e.a * std::cos(k), v = e.b * std::sin(k);
    const float c = std::cos(e.t), s = std::sin(e.t);
    return {e.x + u * c - v * s, e.y + u * s + v * c};
}

// Is p inside the eye, grown by `grow` on every side?
bool inside(const Eye& e, core::Vec2 p, float grow) {
    const float dx = p.x - e.x, dy = p.y - e.y;
    const float c = std::cos(e.t), s = std::sin(e.t);
    const float u = (dx * c + dy * s) / (e.a + grow), v = (-dx * s + dy * c) / (e.b + grow);
    return u * u + v * v <= 1;
}

// Do two eyes overlap, or come within `gap` of each other? Works for any mix of circles and
// rotated ellipses: each outline is sampled and the samples are tested against the other.
// shortcut: sampled outlines can miss a very thin overlap, upgrade to exact conic tests if
// eyes ever visibly touch.
bool touches(const Eye& p, const Eye& q, float gap) {
    for (int k = 0; k < kSegments; ++k) {
        const float ang = 2 * kPi * k / kSegments;
        if (inside(q, point_on(p, ang), gap) || inside(p, point_on(q, ang), gap)) return true;
    }
    return false;
}

// Drawn outline, mirrored across the vertical centre line when side is -1.
std::vector<core::Vec2> outline(const Eye& e, float cx, float cy, float side) {
    std::vector<core::Vec2> pts;
    for (int k = 0; k < kSegments; ++k) {
        const core::Vec2 p = point_on(e, 2 * kPi * k / kSegments);
        pts.push_back({cx + side * p.x, cy + p.y});
    }
    return pts;
}

} // namespace

class SpiderEye : public core::Piece {
public:
    SpiderEye() {
        core::Rng r(7);
        new_colors(r);
    }

    const char* name() const override { return "spideye"; }

    bool ui() override {
        bool c = ImGui::SliderInt("Grid size", &grid_, 1, 6);
        c |= ImGui::SliderInt("Circles (%)", &circle_pct_, 50, 100);
        c |= ImGui::SliderInt("Set size (%)", &size_pct_, 50, 90);
        c |= palette_row();
        if (ImGui::Button("Randomize colors")) {
            randomize_colors();
            c = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize all")) {
            randomize_all();
            request_seed(); // a new spawn too, not only new parameters
            c = true;
        }
        return c;
    }

    // Nudges the numbers a little; colours and grid size stay.
    void perturb() override {
        core::Rng r = time_rng();
        circle_pct_ = std::clamp(circle_pct_ + r.range(-5, 5), 50, 100);
        size_pct_ = std::clamp(size_pct_ + r.range(-5, 5), 50, 90);
    }

    // Foregrounds first, then the background, as the web page's swatches expect.
    int palette(core::Color* out, int max) const override {
        int n = 0;
        for (int i = 1; i <= 1 + n_var_ && n < max; ++i) out[n++] = to_color(colors_[i]);
        if (n < max) out[n++] = to_color(colors_[0]);
        return n;
    }

    void render(core::Canvas& canvas, core::Rng& rng) override {
        canvas.clear(to_color(colors_[0]));

        // A square area in the middle, so the piece stays square on any canvas.
        const float W = static_cast<float>(canvas.width()), H = static_cast<float>(canvas.height());
        const float S = std::min(W, H) / grid_;
        const float ox = (W - grid_ * S) / 2, oy = (H - grid_ * S) / 2;
        // Each set is laid out in a cell size_pct_ percent of the full one, so neighbouring
        // sets keep a margin.
        const float set = S * size_pct_ / 100.0f;
        for (int row = 0; row < grid_; ++row)
            for (int col = 0; col < grid_; ++col) {
                const float cx = ox + (col + 0.5f) * S, cy = oy + (row + 0.5f) * S;
                const std::size_t half = rng.chance(0.9f) ? 4 : 3; // 8 eyes, or 6 in 10% of the sets
                for (const Eye& e : make_half(rng, set, half, 1 - circle_pct_ / 100.0f)) {
                    // Each mirrored pair gets one foreground colour, or one of its variations.
                    const core::Color col_eye = to_color(colors_[rng.range(1, 1 + n_var_)]);
                    canvas.polygon(outline(e, cx, cy, 1), col_eye);
                    canvas.polygon(outline(e, cx, cy, -1), col_eye); // mirror
                }
            }
    }

private:
    // Light background, dark foreground, and 0-2 variations of the foreground close to it
    // (a bit lighter or darker, and a bit different in hue).
    void new_colors(core::Rng& r) {
        const float hue = r.range(0.0f, 360.0f);
        set_color(colors_[0], core::oklch(r.range(0.92f, 0.97f), r.range(0.0f, 0.02f), hue));
        const float L = r.range(0.2f, 0.32f), C = r.range(0.03f, 0.09f);
        set_color(colors_[1], core::oklch(L, C, hue));
        n_var_ = r.range(0, 2);
        for (int i = 0; i < n_var_; ++i)
            set_color(colors_[2 + i], core::oklch(L + r.range(-0.06f, 0.06f), C, hue + r.range(-15.0f, 15.0f)));
    }

    static core::Rng time_rng() {
        return core::Rng(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    void randomize_colors() {
        core::Rng r = time_rng();
        new_colors(r);
    }

    void randomize_all() {
        core::Rng r = time_rng();
        circle_pct_ = r.range(50, 100);
        size_pct_ = r.range(50, 90);
        grid_ = r.range(1, 6);
        new_colors(r);
    }

    // Background, foreground, then the variations; each one is editable.
    bool palette_row() {
        bool changed = false;
        for (int i = 0; i <= 1 + n_var_; ++i) {
            ImGui::PushID(i);
            changed |= ImGui::ColorEdit3("##c", colors_[i], ImGuiColorEditFlags_NoInputs);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::TextUnformatted("Palette");
        return changed;
    }

    // Half a set: `count` eyes that do not overlap, all in the left half of a cell of side S.
    // Each eye is an ellipse with probability ellipse_p, but at most two per half. Sizes and
    // distances from the centre line are biased to small, so the eyes gather near that line.
    std::vector<Eye> make_half(core::Rng& rng, float S, std::size_t count, float ellipse_p) const {
        std::vector<Eye> eyes;
        for (int restart = 0; restart < kRestarts; ++restart) {
            eyes.clear();
            int fails = 0;
            while (eyes.size() < count && fails < kTries) {
                const bool ellipse = ellipses(eyes) < 2 && rng.chance(ellipse_p);
                const Eye e = sample(rng, S, ellipse);
                if (fits(e, eyes, S)) eyes.push_back(e);
                else ++fails;
            }
            if (eyes.size() == count) break;
        }
        return eyes;
    }

    static int ellipses(const std::vector<Eye>& eyes) {
        int n = 0;
        for (const Eye& e : eyes) n += e.a > e.b;
        return n;
    }

    static Eye sample(core::Rng& rng, float S, bool ellipse) {
        const float m = 0.04f * S; // clear of the cell edge
        const float g = 0.02f * S; // clear of the centre line
        const float a = S * (0.03f + 0.11f * rng.uniform() * rng.uniform()); // small is common
        const float b = ellipse ? a * (0.45f + 0.3f * rng.uniform()) : a;
        const float t = ellipse ? rng.range(0.0f, kPi) : 0.0f;
        const float c = std::cos(t), s = std::sin(t);
        const float ex = std::sqrt(a * a * c * c + b * b * s * s); // half-width of the eye
        const float ey = std::sqrt(a * a * s * s + b * b * c * c); // half-height of the eye
        const float far = S / 2 - ey - m;
        float y = far * rng.uniform() * rng.uniform(); // near the centre line is common
        if (rng.chance(0.5f)) y = -y;
        const float x = rng.range(-S / 2 + m + ex, -ex - g);
        return {x, y, a, b, t};
    }

    static bool fits(const Eye& e, const std::vector<Eye>& eyes, float S) {
        const float gap = 0.015f * S;
        return std::none_of(eyes.begin(), eyes.end(), [&](const Eye& o) { return touches(e, o, gap); });
    }

    int grid_ = 4;            // sets per side
    int circle_pct_ = 80;     // share of eyes that are circles (the rest are ellipses)
    int size_pct_ = 90;
    int n_var_ = 0;           // foreground variations in use (0..2)
    float colors_[4][3] = {}; // [0] background, [1] foreground, [2..] variations
};

} // namespace spideye

int main() {
    spideye::SpiderEye piece;
    return core::run(piece);
}
