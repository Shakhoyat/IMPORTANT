#include "primitives/Sphere.h"

#include <vector>

namespace ts {

MeshData createSphere(int slices, int stacks) {
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    if (stacks % 2 != 0) ++stacks;  // the equator must be a ring so the two halves can share it

    const float r = 0.5f;
    const int half = stacks / 2;  // ring index of the equator
    MeshData mesh;
    mesh.name = "Sphere";

    // ---- 1. Northern hemisphere: pole + rings 1..half (ring 'half' is the equator) ----
    mesh.vertices.push_back({{0.0f, r, 0.0f}, {0.0f, 1.0f, 0.0f}});
    for (int k = 1; k <= half; ++k) {
        const float theta = PI * static_cast<float>(k) / static_cast<float>(stacks);
        for (int j = 0; j < slices; ++j) {
            const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
            const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            mesh.vertices.push_back({n * r, n});
        }
    }
    auto ring = [slices](int k, int j) { return static_cast<uint32_t>(1 + (k - 1) * slices + (j % slices)); };

    std::vector<uint32_t> north;
    for (int j = 0; j < slices; ++j) {  // polar cap: triangle fan around the pole
        north.insert(north.end(), {0u, ring(1, j + 1), ring(1, j)});
    }
    for (int k = 1; k < half; ++k) {  // bands between ring k and ring k+1
        for (int j = 0; j < slices; ++j) {
            const uint32_t a0 = ring(k, j), a1 = ring(k, j + 1);
            const uint32_t b0 = ring(k + 1, j), b1 = ring(k + 1, j + 1);
            north.insert(north.end(), {a0, a1, b0, a1, b1, b0});
        }
    }

    // ---- 2. Southern hemisphere by reflection (y -> -y), the equator ring is shared ----
    const uint32_t northCount = static_cast<uint32_t>(mesh.vertices.size());
    std::vector<uint32_t> mirror(northCount);
    for (uint32_t i = 0; i < northCount; ++i) {
        const bool onEquator = i >= static_cast<uint32_t>(1 + (half - 1) * slices);
        if (onEquator) {
            mirror[i] = i;
            continue;
        }
        Vertex v = mesh.vertices[i];
        v.position.y = -v.position.y;
        v.normal.y = -v.normal.y;
        mirror[i] = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(v);
    }

    mesh.indices = north;
    for (size_t t = 0; t < north.size(); t += 3) {
        // A reflection flips orientation, so the 2nd and 3rd index are swapped to stay CCW.
        mesh.indices.insert(mesh.indices.end(), {mirror[north[t]], mirror[north[t + 2]], mirror[north[t + 1]]});
    }
    return mesh;
}

}  // namespace ts
