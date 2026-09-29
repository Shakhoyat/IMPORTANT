// Thermal colour maps shared by the HUD (CPU) -- the GLSL thermalPalette() in ShaderSources.cpp uses the
// same stops. 0 = ironbow (cold indigo -> hot white), 1 = white-hot (grey ramp), 2 = rainbow.
#pragma once

#include <algorithm>

#include "math/Math3D.h"

namespace ts {

constexpr int kPaletteCount = 3;

inline const char* paletteName(int p) {
    switch (p) {
        case 1: return "WHITE-HOT";
        case 2: return "RAINBOW";
        default: return "IRONBOW";
    }
}

inline Vec3 thermalPaletteCpu(int palette, float t) {
    static const float iron[7][3] = {{0.00f, 0.00f, 0.03f}, {0.12f, 0.02f, 0.33f}, {0.46f, 0.03f, 0.55f},
                                     {0.80f, 0.10f, 0.34f}, {0.98f, 0.42f, 0.06f}, {1.00f, 0.78f, 0.16f},
                                     {1.00f, 1.00f, 0.88f}};
    static const float rainbow[7][3] = {{0.00f, 0.00f, 0.05f}, {0.05f, 0.10f, 0.60f}, {0.00f, 0.70f, 0.90f},
                                        {0.10f, 0.85f, 0.20f}, {1.00f, 0.95f, 0.10f}, {1.00f, 0.20f, 0.05f},
                                        {1.00f, 1.00f, 1.00f}};
    t = clampf(t, 0.0f, 1.0f);
    if (palette == 1) {
        const float g = std::pow(t, 0.9f);
        return {g, g, g};
    }
    const float(*stops)[3] = palette == 2 ? rainbow : iron;
    const float x = t * 6.0f;
    const int i = std::min(static_cast<int>(x), 5);
    const float f = x - static_cast<float>(i);
    return {lerpf(stops[i][0], stops[i + 1][0], f), lerpf(stops[i][1], stops[i + 1][1], f),
            lerpf(stops[i][2], stops[i + 1][2], f)};
}

}  // namespace ts
