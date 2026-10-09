#include "primitives/Hemisphere.h"

#include <cmath>

#include "primitives/Revolve.h"

namespace ts {

MeshData createHemisphere(int slices, int stacks) {
    if (slices < 3) slices = 3;
    if (stacks < 1) stacks = 1;
    // Built about +Z (Revolve), then turned so that +Z becomes +Y: (x, y, z) -> (x, z, -y), a proper rotation (the
    // triangle winding stays counter-clockwise from outside).
    const float a = 0.5f, b = 1.0f, base = -0.5f;
    std::vector<RevolveRing> rings;
    for (int k = 1; k <= stacks; ++k) {
        const float theta = 0.5f * PI * static_cast<float>(k) / static_cast<float>(stacks);
        const float s = std::sin(theta), c = std::cos(theta);
        rings.push_back({a * s, base + b * c, s / a, c / b});
    }
    MeshData m = revolve(rings, slices, RevolveEnd::Pole, base + b, RevolveEnd::FlatCap, base);
    for (Vertex& v : m.vertices) {
        v.position = {v.position.x, v.position.z, -v.position.y};
        v.normal = {v.normal.x, v.normal.z, -v.normal.y};
    }
    m.name = "Hemisphere";
    return m;
}

}  // namespace ts
