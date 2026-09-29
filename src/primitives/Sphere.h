#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Unit-diameter sphere (r = 0.5) centred at the origin.
//   theta_k = k * pi / stacks   (polar angle from +Y)
//   phi_j   = 2 * pi * j / slices (azimuth, 10 degrees for slices = 36)
//   x = r sin(theta) cos(phi),  y = r cos(theta),  z = r sin(theta) sin(phi)
// Only the northern hemisphere is generated; the southern one is obtained by reflecting
// y -> -y (Lab-04 sheet, page 3) and reversing the triangle winding.
// Vertices = 2 + (stacks - 1) * slices, triangles = 2 * slices * (stacks - 1).
MeshData createSphere(int slices = 36, int stacks = 18);

}  // namespace ts
