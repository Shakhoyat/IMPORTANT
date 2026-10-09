#include "primitives/Wedge.h"

#include "primitives/FlatFaces.h"

namespace ts {

MeshData createWedge() {
    const Vec3 v0{-0.5f, -0.5f, -0.5f}, v1{0.5f, -0.5f, -0.5f}, v2{0.5f, -0.5f, 0.5f}, v3{-0.5f, -0.5f, 0.5f};
    const Vec3 v4{-0.5f, 0.5f, -0.5f}, v5{0.5f, 0.5f, -0.5f};
    MeshData m;
    m.name = "Wedge";
    addFlatFace(m, {v0, v1, v2, v3});  // bottom (-Y)
    addFlatFace(m, {v0, v4, v5, v1});  // back (-Z)
    addFlatFace(m, {v3, v2, v5, v4});  // slope (+Y +Z)
    addFlatFace(m, {v0, v3, v4});      // left end (-X)
    addFlatFace(m, {v1, v5, v2});      // right end (+X)
    return m;
}

}  // namespace ts
