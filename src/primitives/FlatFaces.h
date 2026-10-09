// Flat-shaded polyhedra (cube family: wedge, tapered box, pyramid): every face gets its own vertices with the face
// normal (a corner is repeated once per face that meets there, as in createCube()). A face is a convex polygon listed
// counter-clockwise seen from outside; it is fanned from its first corner: (0, 1, 2), (0, 2, 3), ...
// flatCorners() recovers the unique corner positions (first-appearance order in the vertex buffer) and every face as a
// tuple of corner indices: the data of a corner table / TikZ figure, read back from the generated mesh.
#pragma once

#include <initializer_list>
#include <vector>

#include "primitives/MeshData.h"

namespace ts {

void addFlatFace(MeshData& m, std::initializer_list<Vec3> corners);

struct FlatCorners {
    std::vector<Vec3> corners;            // unique positions, in the order the generator first emits them
    std::vector<std::vector<int>> faces;  // per face: corner indices, counter-clockwise seen from outside
};

// Faces are the runs of consecutive vertices that share one normal (how addFlatFace and createCube lay them out).
FlatCorners flatCorners(const MeshData& m);

}  // namespace ts
