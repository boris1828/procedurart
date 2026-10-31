#include "core/canvas.h"

#include <raylib.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace core {

namespace {
::Color rl(Color c) { return {c.r, c.g, c.b, c.a}; }
::Vector2 rl(Vec2 v) { return {v.x, v.y}; }

// Area average: each output pixel is the mean of the source pixels it covers. Unlike
// bicubic resizing it does not ring next to sharp edges, so supersampled edges come out clean.
std::vector<uint8_t> area_downscale(const uint8_t* src, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4);
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw);
            uint64_t sum[4] = {};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = src + (static_cast<size_t>(sy) * static_cast<size_t>(sw) + static_cast<size_t>(sx)) * 4;
                    for (int k = 0; k < 4; ++k) sum[k] += p[k];
                }
            const uint64_t n = static_cast<uint64_t>(y1 - y0) * static_cast<uint64_t>(x1 - x0);
            uint8_t* q = out.data() + (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4;
            for (int k = 0; k < 4; ++k) q[k] = static_cast<uint8_t>((sum[k] + n / 2) / n);
        }
    }
    return out;
}
} // namespace

Color hsv(float h, float s, float v, float a) {
    ::Color c = ColorFromHSV(std::fmod(h, 360.0f), s, v);
    c.a = static_cast<uint8_t>(a * 255.0f + 0.5f);
    return {c.r, c.g, c.b, c.a};
}

struct Canvas::Impl {
    RenderTexture2D tex;
    int ss = 1; // supersampling factor: drawn at ss times the size, averaged down on output
};

// Drawing at up to 4x and averaging down on output smooths every edge (raylib's render
// textures have no MSAA here). shortcut: the long side times ss stays within 4096 px, the
// safe texture size on phones, so 1080 gets 3x and canvases above 4096 px get none.
Canvas::Canvas(int width, int height) : w_(width), h_(height), impl_(new Impl) {
    impl_->ss = std::clamp(4096 / std::max(w_, h_), 1, 4);
    impl_->tex = LoadRenderTexture(w_ * impl_->ss, h_ * impl_->ss);
    SetTextureFilter(impl_->tex.texture, TEXTURE_FILTER_BILINEAR);
}

Canvas::~Canvas() {
    UnloadRenderTexture(impl_->tex);
    delete impl_;
}

// The camera scales every draw call, so pieces keep working in canvas pixels.
void Canvas::begin() {
    BeginTextureMode(impl_->tex);
    BeginMode2D(Camera2D{{0, 0}, {0, 0}, 0.0f, static_cast<float>(impl_->ss)});
}

void Canvas::end() {
    EndMode2D();
    EndTextureMode();
    // The preview shows the texture smaller than it is; mipmaps keep it from shimmering.
    GenTextureMipmaps(&impl_->tex.texture);
    SetTextureFilter(impl_->tex.texture, TEXTURE_FILTER_TRILINEAR);
}

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
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    std::vector<uint8_t> px = area_downscale(static_cast<const uint8_t*>(img.data), img.width, img.height, w_, h_);
    UnloadImage(img);
    Image out{px.data(), w_, h_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    return ExportImage(out, path.c_str());
}

std::vector<uint8_t> Canvas::read_rgba(int out_w, int out_h) const {
    Image img = LoadImageFromTexture(impl_->tex.texture);
    ImageFlipVertical(&img); // render textures are stored upside down
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    std::vector<uint8_t> out = area_downscale(static_cast<const uint8_t*>(img.data), img.width, img.height, out_w, out_h);
    UnloadImage(img);
    return out;
}

void Canvas::show_in_imgui() const { rlImGuiImageRenderTextureFit(&impl_->tex, true); }

} // namespace core
