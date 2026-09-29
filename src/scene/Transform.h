#pragma once

#include "math/Math3D.h"

namespace ts {

// Local transform of one scene node. Model matrix M = T * R * S with R = Ry * Rx * Rz,
// i.e. the primitive is scaled first, then rotated (roll, pitch, yaw), then translated.
struct Transform {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec3 rotationDeg{0.0f, 0.0f, 0.0f};  // (pitch about X, yaw about Y, roll about Z)
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Mat4 matrix() const { return translate(translation) * rotateEulerYXZ(rotationDeg) * ts::scale(scale); }
};

}  // namespace ts
