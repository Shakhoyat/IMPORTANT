#include "objects/Terrain.h"

#include "objects/Builder.h"

namespace ts {

namespace {

constexpr float kRimLevel = -0.30f;  // terrain height at r = 20 m (the backdrop's far ground sits just below)
constexpr float kPadInner = 3.8f, kPadOuter = 6.5f;  // levelled pad under the bunker

float smoothstep(float e0, float e1, float x) {
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Soft floor: C1-continuous, >= floor, and exactly h once h >= floor + k (the bunker pad stays at h = 0).
float softFloor(float h, float floor, float k) {
    if (h >= floor + k) return h;
    if (h <= floor - k) return floor;
    const float t = h - floor + k;
    return floor + t * t / (4.0f * k);
}

// 1 on the track centre line, 0 beyond 'outer' metres from it; the track starts in front of the bunker door.
float trackMask(float x, float z, float inner, float outer) {
    const float d = std::fabs(x - trackCentreX(z));
    return (1.0f - smoothstep(inner, outer, d)) * smoothstep(1.8f, 3.6f, z);
}

float rollingHills(float x, float z) {
    return 0.22f * std::sin(0.23f * x + 0.4f) * std::cos(0.19f * z - 0.3f) +
           0.13f * std::sin(0.31f * x - 0.27f * z + 1.1f) + 0.06f * std::sin(0.90f * x + 0.70f * z) +
           0.025f * std::sin(2.1f * x - 1.7f * z);
}

}  // namespace

float trackCentreX(float z) { return 1.4f * std::sin(0.2f * (z - 3.0f)); }

float creekCentreZ(float x) { return -9.5f + 2.2f * std::sin(0.17f * x + 0.6f) + 0.7f * std::sin(0.43f * x - 0.4f); }

float terrainRimHeight() { return kRimLevel; }

float terrainHeight(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    const float pad = smoothstep(kPadInner, kPadOuter, r);  // 0 on the bunker pad, 1 in the open
    const float track = trackMask(x, z, 0.5f * kTrackHalfWidth, 1.4f * kTrackHalfWidth);

    // Rolling hills, flattened on the pad and along the graded track, calmer towards the rim.
    float h = rollingHills(x, z) * pad * (1.0f - 0.7f * track) * (1.0f - 0.8f * smoothstep(16.5f, 19.5f, r));
    // Vehicle track: a shallow worn depression leading from the gate to the bunker door.
    h -= 0.10f * track * smoothstep(kPadInner, kPadOuter, r);
    // Dry creek bed / drainage ditch crossing the south half.
    const float dc = std::fabs(z - creekCentreZ(x));
    h -= 0.40f * (1.0f - smoothstep(0.45f, kCreekHalfWidth + 0.9f, dc)) * pad;
    // Raised earth berm just inside the fence, cut open where the track passes the gate and where the
    // creek drains out of the compound.
    const float db = (r - kBermRadius) / 1.0f;
    const float creekGap = 1.0f - smoothstep(1.0f, 3.5f, dc);
    h += 0.45f * std::exp(-db * db) * (1.0f - trackMask(x, z, 1.5f, 3.2f)) * (1.0f - creekGap);
    // Flat creek bottom: never below the rim level (the backdrop's far ground must stay hidden under the disc).
    h = softFloor(h, kRimLevel + 0.03f, 0.08f);
    // Blend into the constant rim height over the last 1.3 m (the fence stands on this gentle slope).
    return lerpf(h, kRimLevel, smoothstep(18.7f, 20.0f, r));
}

Vec3 terrainNormal(float x, float z) {
    // Central differences: n = normalize(-dh/dx, 1, -dh/dz).
    const float e = 0.05f;
    const float dx = (terrainHeight(x + e, z) - terrainHeight(x - e, z)) / (2.0f * e);
    const float dz = (terrainHeight(x, z + e) - terrainHeight(x, z - e)) / (2.0f * e);
    return normalize(Vec3{-dx, 1.0f, -dz});
}

MeshData createTerrainDisc(int rings, int slices, float radius) {
    MeshData mesh;
    mesh.name = "Terrain disc";
    mesh.vertices.push_back({{0.0f, terrainHeight(0, 0), 0.0f}, terrainNormal(0, 0)});
    for (int k = 1; k <= rings; ++k) {
        const float r = radius * static_cast<float>(k) / static_cast<float>(rings);
        for (int j = 0; j < slices; ++j) {
            const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
            const float x = r * std::cos(phi), z = r * std::sin(phi);
            mesh.vertices.push_back({{x, terrainHeight(x, z), z}, terrainNormal(x, z)});
        }
    }
    auto ring = [slices](int k, int j) { return static_cast<uint32_t>(1 + (k - 1) * slices + (j % slices)); };

    // Same index pattern as the sphere's polar cap and bands (CCW seen from +Y).
    for (int j = 0; j < slices; ++j) mesh.indices.insert(mesh.indices.end(), {0u, ring(1, j + 1), ring(1, j)});
    for (int k = 1; k < rings; ++k) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a0 = ring(k, j), a1 = ring(k, j + 1), b0 = ring(k + 1, j), b1 = ring(k + 1, j + 1);
            mesh.indices.insert(mesh.indices.end(), {a0, a1, b0, a1, b1, b0});
        }
    }
    return mesh;
}

std::unique_ptr<SceneNode> buildTerrain() {
    Builder b("Terrain", "TER", ShadingModel::Gouraud);
    auto root = b.root("Terrain", {0, 0, 0}, "Ground of the patrol area");
    Material ground{{0.36f, 0.34f, 0.28f}, 0.15f, 0.0f, 0.03f, 8.0f};
    ground.roughness = 0.92f;
    ground.metalness = 0.0f;
    ground.surface = Surface::Ground;
    SceneNode* disc = b.part(root.get(), "Ground disc", PrimitiveType::TerrainDisc, {1, 1, 1}, {0, 0, 0},
                             {0, 0, 0}, ground, "20 m radius height-field disc");
    disc->explodable = false;
    return root;
}

}  // namespace ts
