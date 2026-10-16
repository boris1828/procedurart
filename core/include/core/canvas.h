#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace core {

struct Vec2 {
    float x = 0, y = 0;
};

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

// Float HSV (h in degrees 0..360, s/v/a in 0..1) -> Color.
Color hsv(float h, float s, float v, float a = 1.0f);

// Backend-agnostic drawing surface with a fixed pixel size. Origin is top-left,
// +y down. This is the only header pieces need for drawing; raylib stays hidden
// behind it so the backend can be swapped without touching any piece.
class Canvas {
public:
    Canvas(int width, int height);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    int width() const { return w_; }
    int height() const { return h_; }

    void clear(Color c);

    void line(Vec2 a, Vec2 b, Color c, float thickness = 1.0f);
    void polyline(std::span<const Vec2> pts, Color c, float thickness = 1.0f);
    void bezier(Vec2 a, Vec2 b, Vec2 control1, Vec2 control2, Color c, float thickness = 1.0f);

    void circle(Vec2 center, float radius, Color c);
    void circle_outline(Vec2 center, float radius, Color c, float thickness = 1.0f);
    void rect(Vec2 pos, Vec2 size, Color c);
    void rect_outline(Vec2 pos, Vec2 size, Color c, float thickness = 1.0f);
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color col);
    // Convex polygon, vertices in any order (drawn as a fan around the centroid).
    void polygon(std::span<const Vec2> pts, Color c);

    bool save_png(const std::string& path) const;

    // The canvas content resized to out_w x out_h, as tightly packed RGBA8 rows
    // (top row first).
    std::vector<uint8_t> read_rgba(int out_w, int out_h) const;

    // Draw the canvas scaled to fit the current ImGui window (centered).
    void show_in_imgui() const;

private:
    friend class App; // app.cpp / web_app.cpp begin/end drawing (and show the texture)
    void begin();
    void end();

    int w_, h_;
    struct Impl;
    Impl* impl_;
};

} // namespace core
