#include "core/palette.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace core {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Rgb {
    float r, g, b;
};

// Bjorn Ottosson's OKLab -> linear sRGB.
Rgb oklab_to_linear(float L, float a, float b) {
    const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return {+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

bool in_gamut(Rgb c) {
    constexpr float lo = -0.0005f, hi = 1.0005f;
    return c.r >= lo && c.r <= hi && c.g >= lo && c.g <= hi && c.b >= lo && c.b <= hi;
}

uint8_t encode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    const float s = linear <= 0.0031308f ? 12.92f * linear
                                         : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s, 0.0f, 1.0f) * 255.0f + 0.5f);
}

float wrap_hue(float h) {
    h = std::fmod(h, 360.0f);
    return h < 0 ? h + 360.0f : h;
}

} // namespace

Color oklch(float L, float C, float hue_deg) {
    const float h = wrap_hue(hue_deg) * kPi / 180.0f;
    auto at = [&](float chroma) { return oklab_to_linear(L, chroma * std::cos(h), chroma * std::sin(h)); };

    float chroma = C;
    if (!in_gamut(at(chroma))) {
        float lo = 0.0f, hi = C; // bisect for the largest chroma that fits
        for (int i = 0; i < 14; ++i) {
            const float mid = 0.5f * (lo + hi);
            (in_gamut(at(mid)) ? lo : hi) = mid;
        }
        chroma = lo;
    }
    const Rgb c = at(chroma);
    return {encode(c.r), encode(c.g), encode(c.b), 255};
}

Palette random_palette(Rng& rng) {
    // Hue offsets (degrees) of the four foregrounds relative to the base hue.
    static constexpr std::array<std::array<float, 4>, 6> kSchemes = {{
        {0, 0, 0, 0},         // monochromatic: lightness/chroma do the work
        {0, 25, -25, 50},     // analogous
        {0, 180, 15, 195},    // complementary
        {0, 150, 210, 30},    // split-complementary
        {0, 120, 240, 60},    // triadic
        {0, 90, 180, 270},    // tetradic
    }};

    const float base = rng.range(0.0f, 360.0f);
    const auto& scheme = kSchemes[static_cast<size_t>(rng.range(0, static_cast<int>(kSchemes.size()) - 1))];
    const bool dark = rng.chance(0.5f);

    Palette p;
    const int n = rng.range(2, 4); // number of foreground colors
    p.foreground_count = n;

    // Background: quiet, low chroma, tinted by one of the scheme hues.
    const float bg_hue = base + scheme[static_cast<size_t>(rng.range(0, 3))];
    p.background = oklch(dark ? rng.range(0.12f, 0.28f) : rng.range(0.93f, 0.98f),
                         rng.range(0.01f, 0.05f), bg_hue);

    // Foregrounds: lightness stratified over a band that is far from the background,
    // so they are distinguishable and always contrast with it. The first n hue
    // offsets of the scheme are used.
    const float band_lo = dark ? 0.62f : 0.25f;
    const float band_hi = dark ? 0.92f : 0.55f;
    for (int i = 0; i < n; ++i) {
        const float t = (static_cast<float>(i) + rng.uniform()) / static_cast<float>(n);
        const float L = band_lo + (band_hi - band_lo) * t;
        const float hue = base + scheme[static_cast<size_t>(i)] + rng.range(-8.0f, 8.0f);
        p.foreground[i] = oklch(L, rng.range(0.08f, 0.20f), hue);
    }
    return p;
}

} // namespace core
