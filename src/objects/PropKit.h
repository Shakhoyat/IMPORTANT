// Shared helpers for the environment props: ground placement, PBR material presets, segment-aligned parts
// and a tiny deterministic random generator (the scene is identical on every run, so the Excel tables and
// the tests stay reproducible).
#pragma once

#include <cstdint>
#include <string>

#include "objects/Builder.h"
#include "rendering/Material.h"

namespace ts {

// Point on the terrain surface (plus a vertical offset).
Vec3 onGround(float x, float yOffset, float z);

// Complete material: visible colour, heat (thermal camera), procedural surface, GGX roughness/metalness and
// emission. The classic Blinn-Phong terms (specular, shininess) are derived from roughness/metalness so the
// 'B' fallback shading stays consistent with the PBR look.
Material makeMaterial(const Vec3& color, float heat, Surface surface, float roughness, float metalness,
                      float emissive = 0.0f);

// Adds the PBR parameters to an existing (Blinn-Phong) material, keeping its specular/shininess.
Material withPbr(Material m, Surface surface, float roughness, float metalness);

// Euler angles (pitch about X, yaw about Y, 0) in degrees that turn a primitive's local +Z axis onto 'dir'.
Vec3 alignZ(const Vec3& dir);

// Leaf whose local Z axis spans the segment a -> b (in the parent's space), e.g. a wire, brace or rail.
// Cylinder/Cone: 'thickness' is the diameter. Cube: square cross-section 'thickness'.
SceneNode* strut(Builder& b, SceneNode* parent, const std::string& name, PrimitiveType prim, const Vec3& a,
                 const Vec3& c, float thickness, const Material& mat, const std::string& purpose);

// ---- v7 detail kit (WP-M): small parts that add silhouette / material detail to the hero objects ----------------
// Same material with the albedo scaled by k (darker grime k < 1, worn bright edge k > 1); heat is unchanged, so a
// detail part never changes the thermal signature of the surface it sits on.
Material shade(Material m, float k);
// Same material with another roughness / metalness (polished wear, oily grime), Blinn-Phong terms re-derived.
Material reworked(Material m, float roughness, float metalness);
// Indicator LED / small display: emissive glass, cool in the thermal image, NOT a practical light.
Material indicator(const Vec3& color, float emissive, float heat = 0.30f);
// Painted marking (decal on a thin cube): matte paint, heat of the surface it is painted on.
Material decal(const Vec3& color, float heat);

// Leaf that is pure surface detail: no UAV-avoidance obstacle, not an occluder (it lies on a larger part).
SceneNode* detail(Builder& b, SceneNode* parent, const std::string& name, PrimitiveType prim, const Vec3& s,
                  const Vec3& rDeg, const Vec3& t, const Material& mat, const std::string& purpose);
// Rounded edge (bevel) between two points: a thin cylinder that catches a specular highlight along a box edge.
SceneNode* bevel(Builder& b, SceneNode* parent, const std::string& name, const Vec3& a, const Vec3& c, float diameter,
                 const Material& mat, const std::string& purpose);

// Deterministic linear congruential generator (Numerical Recipes constants).
struct PropRng {
    uint32_t state;
    explicit PropRng(uint32_t seed) : state(seed) {}
    float next() {  // [0, 1)
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 16777216.0f;
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }
};

// Material presets shared by several props (heat values stay well below the tracker's 0.80 blob threshold;
// only the rover's motor box (0.97) and the UAV motors are meant to look like hot targets).
namespace mats {
Material pineNeedles();
Material leaves();
Material bark();
Material stone();
Material crateWood();
Material oliveDrum();
Material rustDrum();
Material fenceWood();
Material wire();
Material galvanized();
Material sandbag();
Material canvas();
Material camoNet();
Material towerSteel();
Material warningSign();
Material aviationLight();
Material floodLens();
Material concrete();
}  // namespace mats

}  // namespace ts
