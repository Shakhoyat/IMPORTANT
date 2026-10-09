#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Hemisphere (dome + base disc) filling the unit cube: the upper half of the ellipsoid centred at (0, -0.5, 0) with
// semi-axes (0.5, 1, 0.5), pole at y = +0.5, flat base disc at y = -0.5 (so a part's S is its width x height x depth,
// like the other primitives). Axis = local +Y (the sphere generator's polar axis).
//   theta_k = (pi / 2) k / stacks (k = 1..stacks, from the pole to the base ring), phi_j = 2 pi j / slices
//   p = (0.5 sin(theta) cos(phi), -0.5 + cos(theta), -0.5 sin(theta) sin(phi)),
//   n = normalize(sin(theta) cos(phi) / 0.5, cos(theta) / 1, -sin(theta) sin(phi) / 0.5)
// Dome: pole + 'stacks' rings; base: centre + its own ring (normal -Y).
// Vertices V = slices (stacks + 1) + 2, triangles T = 2 slices stacks (dome slices (2 stacks - 1) + base slices).
MeshData createHemisphere(int slices = 36, int stacks = 9);

}  // namespace ts
