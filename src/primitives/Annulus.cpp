#include "primitives/Annulus.h"

#include <algorithm>
#include <cmath>

namespace ts {

MeshData createAnnulus(float inner, int segments) {
    const int n = std::max(segments, 3);
    inner = std::clamp(inner, 0.01f, 0.49f);
    MeshData m;
    m.name = "Annulus";
    for (int face = 0; face < 2; ++face) {
        const float nz = face == 0 ? 1.0f : -1.0f;
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        for (int j = 0; j < n; ++j) {
            const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(n);
            const float c = std::cos(phi), s = std::sin(phi);
            m.vertices.push_back({{0.5f * c, 0.5f * s, 0.0f}, {0.0f, 0.0f, nz}});      // outer: base + 2j
            m.vertices.push_back({{inner * c, inner * s, 0.0f}, {0.0f, 0.0f, nz}});    // inner: base + 2j + 1
        }
        for (int j = 0; j < n; ++j) {
            const uint32_t o0 = base + 2 * static_cast<uint32_t>(j), i0 = o0 + 1;
            const uint32_t o1 = base + 2 * static_cast<uint32_t>((j + 1) % n), i1 = o1 + 1;
            // Seen from +Z phi runs counter-clockwise: inner0 -> outer0 -> outer1 is CCW; the back face reverses it.
            if (face == 0) m.indices.insert(m.indices.end(), {i0, o0, o1, i0, o1, i1});
            else m.indices.insert(m.indices.end(), {i0, o1, o0, i0, i1, o1});
        }
    }
    return m;
}

}  // namespace ts
