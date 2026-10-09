// Shared builder of the round library primitives (surfaces of revolution about local Z): a list of rings from the top
// to the bottom, each ring 'slices' vertices with its own normal, closed by a pole vertex or a flat cap at either end.
// Bands between neighbouring rings are two triangles per slice, CCW seen from outside (ring a above ring b:
// a0 b0 b1 / a0 b1 a1); a pole fan is one triangle per slice; a flat cap is a centre vertex + its own ring (the cap
// normal differs from the side normal, so the cap has separate vertices). Used by the hemisphere and the capsule.
#pragma once

#include <vector>

#include "primitives/MeshData.h"

namespace ts {

struct RevolveRing {
    float radius;  // distance from the Z axis
    float z;       // height of the ring
    float nr, nz;  // normal = normalize(nr cos(phi), nr sin(phi), nz)
};

enum class RevolveEnd { Pole, FlatCap };

// rings: top to bottom. topEnd / bottomEnd say how the first / last ring is closed: Pole adds one vertex on the
// axis at the given z (normal +-Z) fanned to the ring, FlatCap adds a disc (centre + ring copy) with normal +-Z.
MeshData revolve(const std::vector<RevolveRing>& rings, int slices, RevolveEnd topEnd, float topPoleZ, RevolveEnd bottomEnd,
                 float bottomPoleZ);

}  // namespace ts
