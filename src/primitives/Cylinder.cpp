#include "primitives/Cylinder.h"

namespace ts {

namespace {

void addCap(MeshData& mesh, int slices, float radius, float z, float normalZ) {
    const uint32_t centre = static_cast<uint32_t>(mesh.vertices.size());
    const Vec3 n{0.0f, 0.0f, normalZ};
    mesh.vertices.push_back({{0.0f, 0.0f, z}, n});
    for (int j = 0; j < slices; ++j) {
        const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
        mesh.vertices.push_back({{radius * std::cos(phi), radius * std::sin(phi), z}, n});
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t a = centre + 1 + static_cast<uint32_t>(j);
        const uint32_t b = centre + 1 + static_cast<uint32_t>((j + 1) % slices);
        // Seen from +Z, increasing phi is counter-clockwise; the bottom cap faces -Z so it is reversed.
        if (normalZ > 0.0f) mesh.indices.insert(mesh.indices.end(), {centre, a, b});
        else mesh.indices.insert(mesh.indices.end(), {centre, b, a});
    }
}

}  // namespace

MeshData createCylinder(int slices, float radiusBottom, float radiusTop, bool capped) {
    if (slices < 3) slices = 3;
    MeshData mesh;
    const bool apex = radiusTop <= 0.0f;  // cone: the top ring collapses into one point
    mesh.name = apex ? "Cone" : (radiusBottom == radiusTop) ? "Cylinder" : "Cylinder(frustum)";

    // Side normal of a frustum of height 1 leans along Z by (rBottom - rTop).
    const float slope = radiusBottom - radiusTop;
    for (int j = 0; j < slices; ++j) {
        const float phi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
        const float c = std::cos(phi), s = std::sin(phi);
        const Vec3 n = normalize(Vec3{c, s, slope});
        mesh.vertices.push_back({{radiusBottom * c, radiusBottom * s, -0.5f}, n});  // bottom ring: 0..slices-1
        mesh.vertices.push_back({{radiusTop * c, radiusTop * s, 0.5f}, n});         // top ring (interleaved)
    }
    for (int j = 0; j < slices; ++j) {
        const uint32_t b0 = static_cast<uint32_t>(2 * j), t0 = b0 + 1;
        const uint32_t b1 = static_cast<uint32_t>(2 * ((j + 1) % slices)), t1 = b1 + 1;
        mesh.indices.insert(mesh.indices.end(), {b0, b1, t0});
        // For a cone t0 and t1 coincide at the apex, so the second triangle of the quad would be degenerate.
        if (!apex) mesh.indices.insert(mesh.indices.end(), {b1, t1, t0});
    }

    if (capped) {
        if (!apex) addCap(mesh, slices, radiusTop, 0.5f, 1.0f);
        addCap(mesh, slices, radiusBottom, -0.5f, -1.0f);
    }
    return mesh;
}

}  // namespace ts
