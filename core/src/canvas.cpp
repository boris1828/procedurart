#include "core/canvas.h"

#include <raylib.h>
#include <rlImGui.h>

#include <cmath>
#include <vector>

namespace core {

namespace {
::Color rl(Color c) { return {c.r, c.g, c.b, c.a}; }
::Vector2 rl(Vec2 v) { return {v.x, v.y}; }
} // namespace

Color hsv(float h, float s, float v, float a) {
    ::Color c = ColorFromHSV(std::fmod(h, 360.0f), s, v);
    c.a = static_cast<uint8_t>(a * 255.0f + 0.5f);
    return {c.r, c.g, c.b, c.a};
}

struct Canvas::Impl {
    RenderTexture2D tex;
};

Canvas::Canvas(int width, int height) : w_(width), h_(height), impl_(new Impl) {
    impl_->tex = LoadRenderTexture(w_, h_);
    SetTextureFilter(impl_->tex.texture, TEXTURE_FILTER_BILINEAR);
}

Canvas::~Canvas() {
    UnloadRenderTexture(impl_->tex);
    delete impl_;
}

void Canvas::begin() { BeginTextureMode(impl_->tex); }
void Canvas::end() { EndTextureMode(); }

void Canvas::clear(Color c) { ClearBackground(rl(c)); }

void Canvas::line(Vec2 a, Vec2 b, Color c, float t) { DrawLineEx(rl(a), rl(b), t, rl(c)); }

void Canvas::polyline(std::span<const Vec2> pts, Color c, float t) {
    for (size_t i = 1; i < pts.size(); ++i) line(pts[i - 1], pts[i], c, t);
}

void Canvas::bezier(Vec2 a, Vec2 b, Vec2 c1, Vec2 c2, Color c, float t) {
    DrawSplineSegmentBezierCubic(rl(a), rl(c1), rl(c2), rl(b), t, rl(c));
}

void Canvas::circle(Vec2 p, float r, Color c) { DrawCircleV(rl(p), r, rl(c)); }

void Canvas::circle_outline(Vec2 p, float r, Color c, float t) {
    DrawRing(rl(p), r - t * 0.5f, r + t * 0.5f, 0.0f, 360.0f, 64, rl(c));
}

void Canvas::rect(Vec2 p, Vec2 s, Color c) { DrawRectangleV(rl(p), rl(s), rl(c)); }

void Canvas::rect_outline(Vec2 p, Vec2 s, Color c, float t) {
    DrawRectangleLinesEx({p.x, p.y, s.x, s.y}, t, rl(c));
}

void Canvas::triangle(Vec2 a, Vec2 b, Vec2 c, Color col) {
    // raylib expects counter-clockwise winding; order by signed area.
    float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area > 0) DrawTriangle(rl(a), rl(c), rl(b), rl(col));
    else DrawTriangle(rl(a), rl(b), rl(c), rl(col));
}

void Canvas::polygon(std::span<const Vec2> pts, Color c) {
    if (pts.size() < 3) return;
    Vec2 mid{};
    for (Vec2 p : pts) { mid.x += p.x; mid.y += p.y; }
    mid.x /= static_cast<float>(pts.size());
    mid.y /= static_cast<float>(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) triangle(mid, pts[i], pts[(i + 1) % pts.size()], c);
}

bool Canvas::save_png(const std::string& path) const {
    Image img = LoadImageFromTexture(impl_->tex.texture);
    ImageFlipVertical(&img); // render textures are stored upside down
    bool ok = ExportImage(img, path.c_str());
    UnloadImage(img);
    return ok;
}

std::vector<uint8_t> Canvas::read_rgba(int out_w, int out_h) const {
    Image img = LoadImageFromTexture(impl_->tex.texture);
    ImageFlipVertical(&img); // render textures are stored upside down
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (img.width != out_w || img.height != out_h) ImageResize(&img, out_w, out_h);
    const auto* px = static_cast<const uint8_t*>(img.data);
    std::vector<uint8_t> out(px, px + static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);
    UnloadImage(img);
    return out;
}

void Canvas::show_in_imgui() const { rlImGuiImageRenderTextureFit(&impl_->tex, true); }

} // namespace core
