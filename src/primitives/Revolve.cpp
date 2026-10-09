#include "primitives/Revolve.h"

#include <cmath>

namespace ts {

namespace {

uint32_t addRing(MeshData& m, const RevolveRing& r, int slices, bool flatNormal, float flatNz) {
    const uint32_t first = static_cast<uint32_t>(m.vertices.size());
    for (int j = 0; j < slices; ++j) {
        const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
        const float c = std::cos(phi), s = std::sin(phi);
        const Vec3 n = flatNormal ? Vec3{0.0f, 0.0f, flatNz} : normalize(Vec3{r.nr * c, r.nr * s, r.nz});
        m.vertices.push_back({{r.radius * c, r.radius * s, r.z}, n});
    }
    return first;
}

// One triangle per slice from 'centre' to the ring; 'up' = the fan faces +Z (seen from +Z, phi runs CCW).
void fan(MeshData& m, uint32_t centre, uint32_t ring, int slices, bool up) {
    for (int j = 0; j < slices; ++j) {
        const uint32_t a = ring + static_cast<uint32_t>(j), b = ring + static_cast<uint32_t>((j + 1) % slices);
        if (up) m.indices.insert(m.indices.end(), {centre, a, b});
        else m.indices.insert(m.indices.end(), {centre, b, a});
    }
}

void closeEnd(MeshData& m, const RevolveRing& r, uint32_t ring, int slices, RevolveEnd end, float poleZ, bool top) {
    const float nz = top ? 1.0f : -1.0f;
    if (end == RevolveEnd::Pole) {
        const uint32_t pole = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{0.0f, 0.0f, poleZ}, {0.0f, 0.0f, nz}});
        fan(m, pole, ring, slices, top);
    } else {
        const uint32_t centre = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{0.0f, 0.0f, r.z}, {0.0f, 0.0f, nz}});
        const uint32_t capRing = addRing(m, r, slices, true, nz);
        fan(m, centre, capRing, slices, top);
    }
}

}  // namespace

MeshData revolve(const std::vector<RevolveRing>& rings, int slices, RevolveEnd topEnd, float topPoleZ, RevolveEnd bottomEnd,
                 float bottomPoleZ) {
    MeshData m;
    if (rings.empty() || slices < 3) return m;
    std::vector<uint32_t> first;
    for (const RevolveRing& r : rings) first.push_back(addRing(m, r, slices, false, 0.0f));
    for (size_t k = 0; k + 1 < rings.size(); ++k) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a0 = first[k] + static_cast<uint32_t>(j), a1 = first[k] + static_cast<uint32_t>((j + 1) % slices);
            const uint32_t b0 = first[k + 1] + static_cast<uint32_t>(j), b1 = first[k + 1] + static_cast<uint32_t>((j + 1) % slices);
            m.indices.insert(m.indices.end(), {a0, b0, b1, a0, b1, a1});
        }
    }
    closeEnd(m, rings.front(), first.front(), slices, topEnd, topPoleZ, true);
    closeEnd(m, rings.back(), first.back(), slices, bottomEnd, bottomPoleZ, false);
    return m;
}

}  // namespace ts
