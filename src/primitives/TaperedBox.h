#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Tapered box (frustum of a square pyramid) in the unit cube: bottom face 1 x 1 at y = -0.5, top face topX x topZ
// (0 < top <= 1) at y = +0.5, both centred on the Y axis. Corners: b0..b3 = bottom (-,-) (+,-) (+,+) (-,+) in (x, z),
// t0..t3 = top in the same order. Flat-shaded: 6 faces x 4 = 24 vertices, 6 x 2 = 12 triangles (for any top size).
MeshData createTaperedBox(float topX = 0.8f, float topZ = 0.8f);

}  // namespace ts
