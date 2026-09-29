// View-frustum culling: the 6 clip planes of a view-projection matrix (Gribb & Hartmann extraction)
// tested against bounding spheres. Used by every render pass (main view, UAV camera, shadow maps).
#pragma once

#include "math/Math3D.h"

namespace ts {

class Frustum {
public:
    explicit Frustum(const Mat4& viewProj);
    // True if the sphere is at least partly inside all six planes.
    bool intersects(const Vec3& centre, float radius) const;

private:
    Vec4 planes_[6];  // (a, b, c, d) with a*x + b*y + c*z + d >= 0 inside, (a, b, c) normalised
};

// Bounding sphere of a unit primitive (extent +-0.5 on each axis) after the model matrix 'world'.
float unitPrimitiveRadius(const Mat4& world);

}  // namespace ts
