// Browser front-end (Emscripten). Replaces app.cpp in the web build: no ImGui window,
// the page (web/shell.html) owns the UI and calls the web_* functions below. The art is
// rendered offscreen with raylib, composed into one RGBA buffer, and handed to JS.
#include "core/piece.h"

#include <emscripten.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace core {

// Named App so Canvas's friend declaration covers it.
class App {
public:
    static App& get() {
        static App app;
        return app;
    }

    int start(Piece& piece) {
        piece_ = &piece;
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(16, 16, piece.name()); // only needed for the GL context
        resize(360, 640);
        // JS drives everything from here. main()'s locals (the piece) must outlive
        // this call, so leave via an exception that skips destructors instead of returning.
        emscripten_exit_with_live_runtime();
        return 0;
    }

    // Screen size in physical pixels. The piece keeps its desktop width (1080) and gets
    // the screen's aspect ratio, so parameters look the same as on the desktop.
    void resize(int w, int h) {
        screen_w_ = std::max(w, 1);
        screen_h_ = std::max(h, 1);
        const int lw = 1080;
        const int lh = std::clamp(static_cast<int>(std::lround(1080.0 * screen_h_ / screen_w_)), 400, 4000);
        piece_->set_canvas_size(lw, lh);
        canvas_ = std::make_unique<Canvas>(lw, lh);
        compose();
    }

    void set_grid(int n) {
        grid_ = std::clamp(n, 1, 5);
        compose();
    }

    void randomize_all() {
        seed_ = static_cast<uint32_t>(GetRandomValue(0, 0x7fffffff));
        piece_->set_seed(seed_);
        piece_->randomize_all();
        compose();
    }

    void perturb() {
        piece_->set_seed(seed_);
        piece_->perturb();
        compose();
    }

    void randomize_colors() {
        piece_->randomize_colors();
        compose();
    }

    const uint8_t* pixels() const { return pixels_.data(); }
    int out_w() const { return out_w_; }
    int out_h() const { return out_h_; }

    bool save_png(const char* path) {
        Image img{pixels_.data(), out_w_, out_h_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        return ExportImage(img, path);
    }

private:
    // grid x grid cells, seeds seed_, seed_+1, ... row by row; each cell is the screen
    // size divided by grid, so the whole image is the screen resolution.
    void compose() {
        piece_->set_seed(seed_);
        const int n = grid_;
        const int cw = std::max(1, screen_w_ / n), ch = std::max(1, screen_h_ / n);
        out_w_ = cw * n;
        out_h_ = ch * n;
        pixels_.assign(static_cast<size_t>(out_w_) * static_cast<size_t>(out_h_) * 4, 255);

        for (int k = 0; k < n * n; ++k) {
            rng_.reseed(seed_ + static_cast<uint32_t>(k));
            canvas_->begin();
            canvas_->clear({0, 0, 0, 255});
            piece_->render(*canvas_, rng_);
            canvas_->end();

            const std::vector<uint8_t> cell = canvas_->read_rgba(cw, ch);
            const int x0 = (k % n) * cw, y0 = (k / n) * ch;
            for (int y = 0; y < ch; ++y) {
                std::copy_n(cell.begin() + static_cast<size_t>(y) * static_cast<size_t>(cw) * 4,
                            static_cast<size_t>(cw) * 4,
                            pixels_.begin() + (static_cast<size_t>(y0 + y) * static_cast<size_t>(out_w_) +
                                               static_cast<size_t>(x0)) * 4);
            }
        }
    }

    Piece* piece_ = nullptr;
    std::unique_ptr<Canvas> canvas_;
    Rng rng_;
    uint32_t seed_ = 1;
    int grid_ = 1;
    int screen_w_ = 360, screen_h_ = 640;
    int out_w_ = 0, out_h_ = 0;
    std::vector<uint8_t> pixels_;
};

int run(Piece& piece) { return App::get().start(piece); }

} // namespace core

extern "C" {
EMSCRIPTEN_KEEPALIVE void web_resize(int w, int h) { core::App::get().resize(w, h); }
EMSCRIPTEN_KEEPALIVE void web_set_grid(int n) { core::App::get().set_grid(n); }
EMSCRIPTEN_KEEPALIVE void web_randomize_all() { core::App::get().randomize_all(); }
EMSCRIPTEN_KEEPALIVE void web_perturb() { core::App::get().perturb(); }
EMSCRIPTEN_KEEPALIVE void web_randomize_colors() { core::App::get().randomize_colors(); }
EMSCRIPTEN_KEEPALIVE const uint8_t* web_pixels() { return core::App::get().pixels(); }
EMSCRIPTEN_KEEPALIVE int web_out_w() { return core::App::get().out_w(); }
EMSCRIPTEN_KEEPALIVE int web_out_h() { return core::App::get().out_h(); }
EMSCRIPTEN_KEEPALIVE int web_save_png() { return core::App::get().save_png("/art.png") ? 1 : 0; }
}
