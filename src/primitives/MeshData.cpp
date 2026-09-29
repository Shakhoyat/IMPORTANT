#include "primitives/MeshData.h"

namespace ts {

std::vector<Vertex> expandToTriangleList(const MeshData& mesh) {
    std::vector<Vertex> out;
    out.reserve(mesh.indices.size());
    for (uint32_t index : mesh.indices) out.push_back(mesh.vertices[index]);
    return out;
}

}  // namespace ts
