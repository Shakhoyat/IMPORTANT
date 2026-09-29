#include "primitives/Cube.h"

namespace ts {

namespace {

// A face is described by its outward normal n and two in-plane axes u, v with u x v = n.
// Corners are listed (-u-v), (+u-v), (+u+v), (-u+v), which is counter-clockwise seen from outside.
struct FaceFrame {
    Vec3 n, u, v;
};

const FaceFrame kFaces[6] = {
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +X
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // +Y
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},    // +Z
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},  // -Z
};

}  // namespace

MeshData createCube() {
    MeshData mesh;
    mesh.name = "Cube";
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float su[4] = {-1, 1, 1, -1};
    const float sv[4] = {-1, -1, 1, 1};

    for (const FaceFrame& f : kFaces) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (int c = 0; c < 4; ++c) {
            const Vec3 p = f.n * 0.5f + f.u * (0.5f * su[c]) + f.v * (0.5f * sv[c]);
            mesh.vertices.push_back({p, f.n});
        }
        // Two CCW triangles per face share the diagonal (0,2): 0-1-2 and 0-2-3.
        const uint32_t tri[6] = {0, 1, 2, 0, 2, 3};
        for (uint32_t t : tri) mesh.indices.push_back(base + t);
    }
    return mesh;
}

}  // namespace ts
