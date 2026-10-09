#include "primitives/FlatFaces.h"

#include <cmath>

namespace ts {

void addFlatFace(MeshData& m, std::initializer_list<Vec3> corners) {
    const std::vector<Vec3> p(corners);
    // Newell's method: the polygon's area-weighted normal (exact for planar polygons, robust for slivers).
    Vec3 n{0, 0, 0};
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3& a = p[i];
        const Vec3& b = p[(i + 1) % p.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    n = normalize(n);
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    for (const Vec3& c : p) m.vertices.push_back({c, n});
    for (size_t k = 1; k + 1 < p.size(); ++k)
        m.indices.insert(m.indices.end(), {base, base + static_cast<uint32_t>(k), base + static_cast<uint32_t>(k + 1)});
}

FlatCorners flatCorners(const MeshData& m) {
    FlatCorners out;
    auto same = [](const Vec3& a, const Vec3& b) { return length(a - b) < 1e-5f; };
    auto cornerOf = [&](const Vec3& p) {
        for (size_t i = 0; i < out.corners.size(); ++i)
            if (same(out.corners[i], p)) return static_cast<int>(i);
        out.corners.push_back(p);
        return static_cast<int>(out.corners.size()) - 1;
    };
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        if (i == 0 || !same(m.vertices[i].normal, m.vertices[i - 1].normal)) out.faces.emplace_back();
        out.faces.back().push_back(cornerOf(m.vertices[i].position));
    }
    return out;
}

}  // namespace ts
