// The report's primitive colour key: one fixed colour per library primitive, used by every figure that colours parts by
// primitive (the shape views, the primitive cards and gallery, the F diagnostic view, legends and table chips).
// tools/report/gen_prim_colors.py reads this table and writes report/prim_colors.tex (\definecolor + \primchip), so
// this file is the only place the colours are defined.
//
// Hues are grouped by family and each family has its own CIE L* band, so the families stay apart in greyscale:
//   box      (cube, cuboid, wedge, tapered box, pyramid)  blues / cyan    L* 30-45
//   ring     (torus, annulus, helix)                       pinks / purple  L* 50-57
//   special  (grass blade, terrain disc)                   grey / brown    L* 60-61
//   cylinder (cylinder, frustum, cone)                     greens          L* 64-69
//   round    (sphere, hemisphere, capsule)                 oranges/yellow  L* 74-88
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "math/Math3D.h"
#include "scene/SceneNode.h"

namespace ts::primkey {

struct PrimColor {
    const char* key;      // export name of the primitive ("Cube", "Cuboid", "Cylinder (frustum)", ...)
    const char* texKey;   // LaTeX colour / chip key (prim-<texKey>)
    const char* label;    // name printed next to a chip
    const char* family;   // box, round, cylinder, ring, special
    uint8_t r, g, b;      // sRGB
    float lstar;          // CIE L* of the colour (the greyscale value)
};

// Legend order: family by family.
inline constexpr PrimColor kColors[16] = {
    {"Cube", "cube", "Cube", "box", 0, 70, 144, 30},
    {"Cuboid", "cuboid", "Cuboid", "box", 0, 109, 178, 41},
    {"Wedge", "wedge", "Wedge", "box", 0, 118, 133, 44},
    {"Tapered box", "taperedbox", "Tapered box", "box", 80, 101, 147, 43},
    {"Pyramid", "pyramid", "Pyramid", "box", 0, 88, 80, 33},
    {"Sphere", "sphere", "Sphere", "round", 255, 160, 50, 74},
    {"Hemisphere", "hemisphere", "Hemisphere", "round", 255, 220, 69, 88},
    {"Capsule", "capsule", "Capsule", "round", 255, 200, 120, 84},
    {"Cylinder", "cylinder", "Cylinder", "cylinder", 87, 180, 93, 66},
    {"Cylinder (frustum)", "frustum", "Frustum", "cylinder", 148, 180, 70, 69},
    {"Cone", "cone", "Cone", "cylinder", 51, 174, 141, 64},
    {"Torus", "torus", "Torus", "ring", 210, 71, 144, 52},
    {"Annulus", "annulus", "Annulus", "ring", 130, 104, 192, 50},
    {"Helix", "helix", "Helix", "ring", 189, 107, 186, 57},
    {"Grass blade", "grassblade", "Grass blade", "special", 149, 147, 142, 61},
    {"Terrain disc", "terraindisc", "Terrain disc", "special", 175, 137, 103, 60},
};

// Colour of designs drawn for context only (a nested design shown inside its parent's shape view).
inline constexpr uint8_t kContextGrey[3] = {200, 200, 196};

inline const PrimColor& colorOf(PrimitiveType t, bool cuboid) {
    const std::string key = (t == PrimitiveType::Cube && cuboid) ? std::string("Cuboid") : std::string(primitiveName(t));
    for (const PrimColor& c : kColors)
        if (key == c.key) return c;
    return kColors[0];
}

inline const PrimColor* colorByKey(const std::string& key) {
    for (const PrimColor& c : kColors)
        if (key == c.key) return &c;
    return nullptr;
}

// sRGB in 0..1.
inline Vec3 srgb(const PrimColor& c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }

// Linear-light colour for the renderer's base colour (the diagnostic view lights and tone-maps it).
inline Vec3 linear(const PrimColor& c) {
    auto lin = [](uint8_t v) {
        const float s = v / 255.0f;
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    return {lin(c.r), lin(c.g), lin(c.b)};
}

}  // namespace ts::primkey
