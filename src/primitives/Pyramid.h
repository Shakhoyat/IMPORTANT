#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Square pyramid in the unit cube: base 1 x 1 at y = -0.5 (corners b0..b3 = (-,-) (+,-) (+,+) (-,+) in (x, z)),
// apex a = (0, +0.5, 0). Flat-shaded: base 4 + 4 sides x 3 = 16 vertices, 2 + 4 = 6 triangles.
MeshData createPyramid();

}  // namespace ts
