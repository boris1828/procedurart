#pragma once
#include "core/canvas.h"
#include "core/rng.h"

namespace core {

// A background plus four foreground colors that work together.
struct Palette {
    Color background;
    Color foreground[4];
    int foreground_count = 4; // only foreground[0 .. foreground_count-1] are meaningful
};

// OKLCH -> sRGB. L in 0..1, C ~0..0.4, h in degrees. Out-of-gamut colors keep
// their lightness and hue and lose chroma until they fit.
Color oklch(float L, float C, float hue_deg);

// Random harmonious palette with a random number (2 to 4) of foregrounds, built in OKLCH from a random base hue and a random
// harmony scheme (monochromatic, analogous, complementary, split-complementary,
// triadic, tetradic). The background is either dark or light and always has a
// clear lightness gap to the foregrounds.
Palette random_palette(Rng& rng);

} // namespace core
