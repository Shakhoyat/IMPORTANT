#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Torus about local Z (the cylinder's axis, so a torus can replace a ring-shaped cylinder part) inside the unit cube:
// ring radius R, tube radius r with R + r <= 0.5; it spans x, y in [-(R + r), R + r] and z in [-r, r] (a part's S.z
// is therefore its thickness / (2 r)).
//   u_i = 2 pi i / U (around the axis), v_j = 2 pi j / V (around the tube)
//   p = ((R + r cos v) cos u, (R + r cos v) sin u, r sin v),   n = (cos v cos u, cos v sin u, sin v)
// The seams are not duplicated (no texture coordinates): vertices V = U V, triangles T = 2 U V; closed surface.
MeshData createTorus(float R = 0.35f, float r = 0.15f, int u = 36, int v = 16);

}  // namespace ts
