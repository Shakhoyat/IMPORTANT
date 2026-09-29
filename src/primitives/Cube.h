#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Unit cube centred at the origin, x,y,z in [-0.5, 0.5].
// 6 faces x 4 unique vertices = 24 vertices (a corner is repeated per face because its normal differs),
// 6 faces x 2 triangles = 12 triangles = 36 indices.
MeshData createCube();

}  // namespace ts
