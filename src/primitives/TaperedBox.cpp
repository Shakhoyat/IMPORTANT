#include "primitives/TaperedBox.h"

#include <algorithm>

#include "primitives/FlatFaces.h"

namespace ts {

MeshData createTaperedBox(float topX, float topZ) {
    const float tx = 0.5f * std::clamp(topX, 0.01f, 1.0f), tz = 0.5f * std::clamp(topZ, 0.01f, 1.0f);
    const Vec3 b0{-0.5f, -0.5f, -0.5f}, b1{0.5f, -0.5f, -0.5f}, b2{0.5f, -0.5f, 0.5f}, b3{-0.5f, -0.5f, 0.5f};
    const Vec3 t0{-tx, 0.5f, -tz}, t1{tx, 0.5f, -tz}, t2{tx, 0.5f, tz}, t3{-tx, 0.5f, tz};
    MeshData m;
    m.name = "TaperedBox";
    addFlatFace(m, {b0, b1, b2, b3});  // bottom (-Y)
    addFlatFace(m, {t0, t3, t2, t1});  // top (+Y)
    addFlatFace(m, {b3, b2, t2, t3});  // front (+Z, leaning in)
    addFlatFace(m, {b1, b0, t0, t1});  // back (-Z)
    addFlatFace(m, {b2, b1, t1, t2});  // right (+X)
    addFlatFace(m, {b0, b3, t3, t0});  // left (-X)
    return m;
}

}  // namespace ts
