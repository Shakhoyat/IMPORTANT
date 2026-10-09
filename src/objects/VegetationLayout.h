// v9 M6: where the asset vegetation (CC0 glTF trees, bushes, rocks, ground cover) stands. Pure CPU.
//
// The scene graph keeps the v8 primitive trees as the gameplay model: their parts stay occluders / obstacles for the
// hide-and-seek AI and the walker, and LOW / HIGH (or a build without assets/) still draw them. At ULTRA / EPIC the
// renderer replaces what it draws: extractVegetation reads every primitive tree, bush and boulder of the scene (group
// position, yaw, size) and returns one asset instance per object plus the list of primitive leaves it replaces.
// Decorative ground cover (ferns, small shrubs) is added around the camp trees with the same rules everywhere.
#pragma once

#include <cstdint>
#include <vector>

#include "scene/SceneNode.h"

namespace ts {

enum class VegKind : uint8_t {
    CampConifer = 0,    // camp pines (3-4.6 m): fir saplings
    Broadleaf = 1,      // round camp trees: tree_small_02
    Bush = 2,           // camp bushes: shrub_01 / shrub_02
    Rock = 3,           // boulders: boulder_01 / rock_moss_set
    TallConifer = 4,    // treeline + hills (6-18 m): fir_tree_01 / pine_tree_01
    YoungConifer = 5,   // forest edges: pine_sapling_small
    GroundCover = 6,    // ferns, low shrubs (decorative, no gameplay)
    Count = 7
};

struct VegInstance {
    VegKind kind = VegKind::CampConifer;
    float x = 0, y = 0, z = 0;
    float yawDeg = 0;
    float height = 1;        // target height of the object (m)
    float width = 0;         // target footprint diameter (m); 0 = keep the asset's proportions
    uint32_t seed = 0;       // variant choice / tint
};

struct VegLayout {
    std::vector<VegInstance> instances;
    std::vector<const SceneNode*> replaced;   // primitive leaves the asset instances stand in for
};

// Trees, bushes, boulders and treeline pines of the scene (any subset of the scene's leaves).
VegLayout extractVegetation(const std::vector<const SceneNode*>& leaves);
// Ferns and low shrubs around the given trees (deterministic); 'groundY(x, z)' gives the terrain height.
template <typename Ground>
void addGroundCover(VegLayout& layout, Ground groundY, uint32_t seed);

uint32_t vegHash(uint32_t x);

}  // namespace ts

// ---- template implementation ----
#include <cmath>

namespace ts {

template <typename Ground>
void addGroundCover(VegLayout& layout, Ground groundY, uint32_t seed) {
    const size_t trees = layout.instances.size();
    for (size_t i = 0; i < trees; ++i) {
        const VegInstance t = layout.instances[i];
        if (t.kind != VegKind::CampConifer && t.kind != VegKind::Broadleaf) continue;
        const int n = 3 + static_cast<int>(vegHash(seed ^ static_cast<uint32_t>(i) * 977u) % 4u);
        for (int k = 0; k < n; ++k) {
            const uint32_t h = vegHash(seed + static_cast<uint32_t>(i) * 131u + static_cast<uint32_t>(k) * 7919u);
            const float a = static_cast<float>(h & 0xFFFF) / 65535.0f * 6.2831853f;
            const float d = 0.9f + 1.6f * static_cast<float>((h >> 16) & 0xFF) / 255.0f;
            VegInstance g;
            g.kind = VegKind::GroundCover;
            g.x = t.x + d * std::cos(a);
            g.z = t.z + d * std::sin(a);
            g.y = groundY(g.x, g.z);
            g.yawDeg = static_cast<float>((h >> 8) % 360u);
            g.height = 0.35f + 0.3f * static_cast<float>((h >> 24) & 0xFF) / 255.0f;
            g.seed = vegHash(h);
            layout.instances.push_back(g);
        }
    }
}

}  // namespace ts
