#include "primitives/Torus.h"

#include <algorithm>
#include <cmath>

namespace ts {

MeshData createTorus(float R, float r, int u, int v) {
    u = std::max(u, 3);
    v = std::max(v, 3);
    r = std::clamp(r, 0.001f, 0.25f);
    R = std::clamp(R, r, 0.5f - r);  // keeps the hole open and the ring inside the unit cube
    MeshData m;
    m.name = "Torus";
    for (int i = 0; i < u; ++i) {
        const float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(u);
        for (int j = 0; j < v; ++j) {
            const float b = 2.0f * PI * static_cast<float>(j) / static_cast<float>(v);
            const Vec3 n{std::cos(b) * std::cos(a), std::cos(b) * std::sin(a), std::sin(b)};
            m.vertices.push_back({{(R + r * std::cos(b)) * std::cos(a), (R + r * std::cos(b)) * std::sin(a), r * std::sin(b)}, n});
        }
    }
    auto at = [u, v](int i, int j) { return static_cast<uint32_t>((i % u) * v + (j % v)); };
    // dp/du x dp/dv points along +n, so (i, j) -> (i+1, j) -> (i+1, j+1) is counter-clockwise from outside.
    for (int i = 0; i < u; ++i)
        for (int j = 0; j < v; ++j)
            m.indices.insert(m.indices.end(), {at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j), at(i + 1, j + 1), at(i, j + 1)});
    return m;
}

}  // namespace ts
