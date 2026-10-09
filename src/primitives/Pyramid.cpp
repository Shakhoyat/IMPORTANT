#include "primitives/Pyramid.h"

#include "primitives/FlatFaces.h"

namespace ts {

MeshData createPyramid() {
    const Vec3 b0{-0.5f, -0.5f, -0.5f}, b1{0.5f, -0.5f, -0.5f}, b2{0.5f, -0.5f, 0.5f}, b3{-0.5f, -0.5f, 0.5f};
    const Vec3 a{0.0f, 0.5f, 0.0f};
    MeshData m;
    m.name = "Pyramid";
    addFlatFace(m, {b0, b1, b2, b3});  // base (-Y)
    addFlatFace(m, {b3, b2, a});       // front (+Z)
    addFlatFace(m, {b2, b1, a});       // right (+X)
    addFlatFace(m, {b1, b0, a});       // back (-Z)
    addFlatFace(m, {b0, b3, a});       // left (-X)
    return m;
}

}  // namespace ts
