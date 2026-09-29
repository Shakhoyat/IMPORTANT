// Trees, bushes and rocks. Pines are a trunk cylinder plus stacked cones (PrimitiveType::Cone = the lab
// cylinder generator with top radius 0); round trees are a trunk plus two spheres; bushes are flattened spheres.
#pragma once

#include <cstdint>
#include <vector>

#include "objects/Builder.h"
#include "objects/Structures.h"

namespace ts {

// Pine of total height 'height' (m). The lowest cone starts 0.6 m above the ground, so the small rover can
// park under its branches. 2 cones below 3.6 m, 3 cones above.
void addPine(Builder& b, SceneNode* parent, float x, float z, float height, float yawDeg);
void addRoundTree(Builder& b, SceneNode* parent, float x, float z, float size);
void addBush(Builder& b, SceneNode* parent, float x, float z, float size, float yawDeg);
void addRock(Builder& b, SceneNode* parent, float x, float z, const Vec3& s, float yaw);

// True where a tree may grow: inside the berm, off the track, out of the creek bed, away from the bunker
// and from every footprint in 'keepClear' (plus 'margin').
bool vegetationAllowed(float x, float z, const std::vector<Footprint>& keepClear, float margin);

// Deterministic cluster of 'count' trees around (cx, cz) within 'radius' m; 'roundTrees' of them are round
// deciduous trees, the rest pines. Accepted trees are appended to 'keepClear' so patches never overlap.
int addForestPatch(Builder& b, SceneNode* parent, float cx, float cz, float radius, int count, int roundTrees,
                   uint32_t seed, std::vector<Footprint>& keepClear);

}  // namespace ts
