// Surface description shared by the visible-light and the thermal renderings.
#pragma once

#include "math/Math3D.h"

namespace ts {

enum class ShadingModel { Flat, Gouraud, Phong, Beam };

inline const char* shadingName(ShadingModel s) {
    switch (s) {
        case ShadingModel::Flat: return "Flat";
        case ShadingModel::Gouraud: return "Gouraud";
        case ShadingModel::Phong: return "Phong";
        case ShadingModel::Beam: return "Additive beam";
    }
    return "?";
}

// Procedural surface pattern evaluated in the fragment shader (no image textures are used).
enum class Surface { Plain = 0, Concrete, Ground, Metal, Paint, Foliage, Bark, Rock, Wood, Sand, Rubber, Glass };

struct Material {
    Vec3 baseColor{0.5f, 0.5f, 0.5f};  // albedo under visible light
    float heat = 0.2f;                 // intrinsic temperature, 0 = ambient night ground, 1 = hottest
    float emissive = 0.0f;             // self-illumination (beacon bulb, LEDs, moon)
    float specular = 0.1f;
    float shininess = 16.0f;
    bool fogged = true;                // false only for the far-away moon
    // Physically based parameters (GGX / Cook-Torrance). roughness < 0 means "derive from shininess".
    float roughness = -1.0f;
    float metalness = 0.0f;
    Surface surface = Surface::Plain;
    // Practical light: an emissive part that also lights its surroundings (lamp, fire, floodlight).
    // Colour = baseColor * lightIntensity * emissive (so blinking/flickering emission dims the light too).
    float lightIntensity = 0.0f;  // 0 = not a light source
    float lightRange = 0.0f;      // metres; smooth cut-off
    float lightConeDeg = 0.0f;    // 0 = omni-directional, otherwise spot half-angle around the part's local +Z
    float opacity = 1.0f;         // < 1: drawn in the translucent pass (rotor blur discs), no shadows
};

inline Material asLight(Material m, float intensity, float range, float coneDeg = 0.0f) {
    m.lightIntensity = intensity;
    m.lightRange = range;
    m.lightConeDeg = coneDeg;
    return m;
}

// Perceptual roughness used by the PBR shaders.
inline float pbrRoughness(const Material& m) {
    if (m.roughness >= 0.0f) return m.roughness;
    const float fromShininess = 1.0f - std::log2(m.shininess > 1.0f ? m.shininess : 1.0f) / 8.0f;
    return clampf(fromShininess, 0.08f, 0.95f);
}

}  // namespace ts
