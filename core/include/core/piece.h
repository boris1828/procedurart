#pragma once
#include "core/canvas.h"
#include "core/rng.h"

namespace core {

// One generative artwork. Implement this, then call core::run() from main().
//
// Contract: render() must be a pure function of (the piece's parameters, rng).
// The app reseeds `rng` before every render, so the same params + seed always
// give the same image.
class Piece {
public:
    virtual ~Piece() = default;

    virtual const char* name() const = 0;

    // Fixed canvas size in pixels for this piece.
    virtual int width() const { return 1080; }
    virtual int height() const { return 1080; }

    // Draw ImGui widgets for the parameters (called inside the Controls window).
    // Return true if any parameter changed, which triggers a re-render.
    virtual bool ui() = 0;

    // Draw the artwork. The canvas is already cleared to black.
    virtual void render(Canvas& canvas, Rng& rng) = 0;

    // Hooks for the web front-end (no ImGui there). Pieces that support it override them.
    virtual void set_canvas_size(int /*w*/, int /*h*/) {}
    virtual void randomize_all() {}
    virtual void randomize_colors() {}
    // Nudge the parameters slightly (colors untouched).
    virtual void perturb() {}
    // Current colors, foregrounds first then the background; returns how many were written.
    virtual int palette(Color* /*out*/, int /*max*/) const { return 0; }
    // Switchable initial-state methods (how the starting shapes are spawned).
    virtual int spawner_count() const { return 0; }
    virtual int spawner() const { return 0; }
    virtual void set_spawner(int /*index*/) {}

    // The seed the app will pass to render() (the rng is reseeded with it).
    // Set by the app before ui(), so a piece can preview what a render would give.
    void set_seed(uint32_t seed) { seed_ = seed; }
    uint32_t seed() const { return seed_; }

private:
    uint32_t seed_ = 1;
};

// Opens the window and runs the main loop until it is closed.
int run(Piece& piece);

} // namespace core
