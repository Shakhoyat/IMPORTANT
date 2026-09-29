#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Cylinder centred at the origin with its axis along local Z (Lab-04 sheet, page 3), z in [-0.5, 0.5].
//   phi_j = 2 * pi * j / slices,  ring point = (r cos(phi_j), r sin(phi_j), z)
// Side: 2 rings of 'slices' vertices, 2 triangles per slice.
// Caps: 1 centre + 'slices' ring vertices each (separate vertices because the cap normal is +-Z).
// With radiusTop != radiusBottom the same routine produces a frustum (used for the light beam).
// With radiusTop = 0 it produces a closed cone (pine trees): one triangle per slice, no top cap.
MeshData createCylinder(int slices = 36, float radiusBottom = 0.5f, float radiusTop = 0.5f, bool capped = true);

}  // namespace ts
