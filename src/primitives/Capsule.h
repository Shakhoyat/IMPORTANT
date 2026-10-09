#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Capsule along local Z filling the unit cube: a cylinder of radius 0.5 between z = -(0.5 - h) and +(0.5 - h) closed
// by two half-ellipsoid caps of semi-axes (0.5, 0.5, h). In a part scaled by S the caps are true hemispheres when
// S.z h = S.x / 2 (the sandbags: S about 0.27 x 0.17 x 0.5 with h = 0.25).
//   cap ring k (k = 1..capStacks from the pole): theta_k = (pi / 2) k / capStacks,
//   p = (0.5 sin(theta) cos(phi), 0.5 sin(theta) sin(phi), +-(0.5 - h + h cos(theta))),
//   n = normalize(sin(theta) cos(phi) / 0.5, sin(theta) sin(phi) / 0.5, +-cos(theta) / h)
// The two equator rings are shared by the cap and the cylinder side (the normal is radial there for both).
// Vertices V = 2 capStacks slices + 2, triangles T = 4 capStacks slices.
MeshData createCapsule(int slices = 16, int capStacks = 3, float capHeight = 0.25f);

}  // namespace ts
