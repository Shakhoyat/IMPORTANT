#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Wedge / triangular prism filling the unit cube: the right triangle (y, z) = (-0.5, -0.5), (-0.5, +0.5), (+0.5, -0.5)
// extruded along X. Full bottom face (y = -0.5), full back face (z = -0.5), and the slope from the top back edge
// (y = +0.5, z = -0.5) down to the bottom front edge (y = -0.5, z = +0.5); two triangular ends (x = +-0.5).
// Corners: v0 (-,-,-) v1 (+,-,-) v2 (+,-,+) v3 (-,-,+) v4 (-,+,-) v5 (+,+,-).
// Flat-shaded (each face has its own vertices): 3 quads x 4 + 2 triangles x 3 = 18 vertices, 3 x 2 + 2 x 1 = 8 triangles.
MeshData createWedge();

}  // namespace ts
