#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Grass blade (v9 M1, rendering/Grass.cpp): a folded, tapered strip of 3 segments, x in [-0.625, 0.625] (1.25 wide to
// make up for the unit cube's thickness it replaced), y in [-0.5, 0.5], z = 0. Rows at t = 0, 0.38, 0.72 (two vertices
// each, edge normals tilted outwards by the midrib fold) and the tip: vertices V = 7, triangles T = 5 (2 per segment
// + the tip). One-sided: the grass shader turns the normal to the viewer and draws it with culling off. It is not a
// scene-graph leaf: the GPU instances it per blade (budget per quality preset below).
MeshData createGrassBlade();

// Blade budget per quality preset (QualityProfile::grassBlades; GrassField places at most this many).
constexpr int kGrassBladesLow = 14000;
constexpr int kGrassBladesHigh = 35000;
constexpr int kGrassBladesUltra = 140000;
constexpr int kGrassBladesEpic = 450000;

}  // namespace ts
