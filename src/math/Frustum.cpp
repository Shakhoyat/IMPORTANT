#include "math/Frustum.h"

#include <algorithm>

namespace ts {

Frustum::Frustum(const Mat4& m) {
    // Row r of the matrix is (m.at(r,0), m.at(r,1), m.at(r,2), m.at(r,3)).
    // Planes: row3 + row0 (left), row3 - row0 (right), row3 + row1, row3 - row1, row3 + row2, row3 - row2.
    for (int i = 0; i < 6; ++i) {
        const int row = i / 2;
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        Vec4 p{m.at(3, 0) + sign * m.at(row, 0), m.at(3, 1) + sign * m.at(row, 1), m.at(3, 2) + sign * m.at(row, 2),
               m.at(3, 3) + sign * m.at(row, 3)};
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > 1e-8f) p = {p.x / len, p.y / len, p.z / len, p.w / len};
        planes_[i] = p;
    }
}

bool Frustum::intersects(const Vec3& c, float r) const {
    for (const Vec4& p : planes_)
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    return true;
}

float unitPrimitiveRadius(const Mat4& w) {
    const float sx = length(Vec3{w.at(0, 0), w.at(1, 0), w.at(2, 0)});
    const float sy = length(Vec3{w.at(0, 1), w.at(1, 1), w.at(2, 1)});
    const float sz = length(Vec3{w.at(0, 2), w.at(1, 2), w.at(2, 2)});
    return 0.5f * std::sqrt(sx * sx + sy * sy + sz * sz);  // half the diagonal of the scaled unit box
}

}  // namespace ts
