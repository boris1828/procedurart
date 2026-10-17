#pragma once
// A spawner builds the initial shapes that the extrusion loop then grows. Each one has
// its own parameters (UI, randomization, perturbation) and reads the shared ones
// through Common. To add a method: write a Spawner subclass in spawners.h and register
// it in Signrec (one member + one line in Signrec::spawners()).
#include "geometry.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace signrec {

// Read-only view of the parameters every spawner shares.
struct Common {
    int width, height;
    int edge_width;
    int margin; // forbidden zone: nothing, bumps included, gets closer than this to the border
    int eps_min;
    int grid;

    // Centerlines stay in the box inset by this much, so the stroke's outer side
    // is at least `margin` from the border.
    float inset() const { return static_cast<float>(margin) + static_cast<float>(edge_width) * 0.5f; }
    int min_gap() const { return edge_width + eps_min; } // closest valid centerline distance
    // Half the short side, minus a minimum 50px for the innermost shape.
    int room() const { return std::min(width, height) / 2 - 50 - (edge_width + 1) / 2; }

    int snap_round(int v) const { return ((v + grid / 2) / grid) * grid; }
    float snap_up(float v) const { return std::ceil(v / static_cast<float>(grid)) * static_cast<float>(grid); }
    float snap_down(float v) const { return std::floor(v / static_cast<float>(grid)) * static_cast<float>(grid); }
    float snap_nearest(float v) const { return std::round(v / static_cast<float>(grid)) * static_cast<float>(grid); }
};

// A slider with the "include in Randomize selected" checkbox to its left.
inline bool sel_slider(bool* sel, const char* label, int* v, int lo, int hi) {
    ImGui::PushID(label);
    ImGui::Checkbox("##sel", sel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(190);
    const bool changed = ImGui::SliderInt(label, v, lo, std::max(lo, hi));
    ImGui::PopID();
    return changed;
}

class Spawner {
public:
    virtual ~Spawner() = default;

    virtual const char* name() const = 0;
    // Whether the shapes are closed, i.e. have an interior that "Fill layers" can color.
    virtual bool closed_shapes() const = 0;

    virtual std::vector<Shape> spawn(const Common& c) const = 0;

    // Make the spawner's own parameters valid for the shared ones.
    virtual void sanitize(const Common& c) = 0;
    // Largest common margin this spawner can live with.
    virtual int max_margin(const Common& c) const { return std::max(0, c.room()); }

    // Draws the spawner's widgets (inside the Parameters section); true if anything changed.
    virtual bool ui(const Common& c) = 0;
    virtual void randomize(core::Rng& r, const Common& c, bool only_selected) = 0;
    virtual void perturb(core::Rng& r, const Common& c) = 0;
};

} // namespace signrec
