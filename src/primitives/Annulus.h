#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Annulus (flat ring) in the local XY plane (z = 0, axis Z like the cylinder), outer radius 0.5, inner radius
// 'inner' (0 < inner < 0.5). Both faces are built (front normal +Z, back normal -Z, separate vertices, opposite
// winding) so the ring is visible from either side with back-face culling on.
//   phi_j = 2 pi j / N,  outer point (0.5 cos phi, 0.5 sin phi, 0), inner point (inner cos phi, inner sin phi, 0)
// Vertices V = 4 N (2 rings x 2 faces), triangles T = 4 N (2 per segment per face).
MeshData createAnnulus(float inner = 0.43f, int segments = 48);

}  // namespace ts
