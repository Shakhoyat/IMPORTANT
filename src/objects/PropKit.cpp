#include "objects/PropKit.h"

#include <algorithm>

#include "objects/Terrain.h"

namespace ts {

Vec3 onGround(float x, float yOffset, float z) { return {x, terrainHeight(x, z) + yOffset, z}; }

Material makeMaterial(const Vec3& color, float heat, Surface surface, float roughness, float metalness,
                      float emissive) {
    Material m;
    m.baseColor = color;
    m.heat = heat;
    m.emissive = emissive;
    m.roughness = roughness;
    m.metalness = metalness;
    m.surface = surface;
    // Inverse of pbrRoughness(): roughness 1 -> shininess 2, roughness 0 -> 256.
    m.shininess = std::max(2.0f, std::exp2(8.0f * (1.0f - roughness)));
    m.specular = 0.04f + 0.5f * metalness + 0.3f * (1.0f - roughness) * (1.0f - metalness);
    return m;
}

Material withPbr(Material m, Surface surface, float roughness, float metalness) {
    m.surface = surface;
    m.roughness = roughness;
    m.metalness = metalness;
    return m;
}

Vec3 alignZ(const Vec3& dir) {
    const float horizontal = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    return {degrees(std::atan2(-dir.y, horizontal)), degrees(std::atan2(dir.x, dir.z)), 0.0f};
}

SceneNode* strut(Builder& b, SceneNode* parent, const std::string& name, PrimitiveType prim, const Vec3& a,
                 const Vec3& c, float thickness, const Material& mat, const std::string& purpose) {
    const Vec3 d = c - a;
    return b.part(parent, name, prim, {thickness, thickness, length(d)}, alignZ(d), (a + c) * 0.5f, mat, purpose);
}

namespace mats {
//                                   colour                 heat   surface            rough  metal
Material pineNeedles() { return makeMaterial({0.10f, 0.19f, 0.11f}, 0.11f, Surface::Foliage, 0.75f, 0.0f); }
Material leaves() { return makeMaterial({0.16f, 0.24f, 0.13f}, 0.12f, Surface::Foliage, 0.70f, 0.0f); }
Material bark() { return makeMaterial({0.25f, 0.19f, 0.14f}, 0.18f, Surface::Bark, 0.90f, 0.0f); }
Material stone() { return makeMaterial({0.30f, 0.29f, 0.27f}, 0.30f, Surface::Rock, 0.88f, 0.0f); }
Material crateWood() { return makeMaterial({0.42f, 0.33f, 0.20f}, 0.28f, Surface::Wood, 0.75f, 0.0f); }
Material oliveDrum() { return makeMaterial({0.22f, 0.30f, 0.22f}, 0.42f, Surface::Paint, 0.45f, 0.6f); }
Material rustDrum() { return makeMaterial({0.36f, 0.19f, 0.10f}, 0.38f, Surface::Metal, 0.70f, 0.5f); }
Material fenceWood() { return makeMaterial({0.30f, 0.26f, 0.20f}, 0.20f, Surface::Wood, 0.85f, 0.0f); }
Material wire() { return makeMaterial({0.30f, 0.30f, 0.31f}, 0.12f, Surface::Metal, 0.75f, 0.6f); }  // rough: thin wires alias less
Material galvanized() { return makeMaterial({0.55f, 0.56f, 0.58f}, 0.22f, Surface::Metal, 0.40f, 0.9f); }
Material sandbag() { return makeMaterial({0.52f, 0.47f, 0.36f}, 0.25f, Surface::Sand, 0.95f, 0.0f); }
Material canvas() { return makeMaterial({0.30f, 0.31f, 0.22f}, 0.35f, Surface::Plain, 0.85f, 0.0f); }
Material camoNet() { return makeMaterial({0.20f, 0.26f, 0.16f}, 0.20f, Surface::Foliage, 0.80f, 0.0f); }
Material towerSteel() { return makeMaterial({0.32f, 0.34f, 0.30f}, 0.24f, Surface::Paint, 0.45f, 0.6f); }
Material warningSign() { return makeMaterial({0.80f, 0.65f, 0.10f}, 0.20f, Surface::Paint, 0.50f, 0.3f); }
Material aviationLight() {
    return asLight(makeMaterial({1.00f, 0.08f, 0.05f}, 0.50f, Surface::Glass, 0.10f, 0.0f, 1.0f), 1.6f, 4.0f);
}
Material floodLens() {  // watchtower floodlight: a real spot light aimed along the lens axis
    return asLight(makeMaterial({1.00f, 0.90f, 0.72f}, 0.68f, Surface::Glass, 0.05f, 0.0f, 1.0f), 9.0f, 24.0f, 26.0f);
}
Material concrete() { return makeMaterial({0.44f, 0.43f, 0.40f}, 0.30f, Surface::Concrete, 0.85f, 0.0f); }
}  // namespace mats

}  // namespace ts
