// CPU-side geometry produced by the primitive generators.
// A vertex is interleaved as (x, y, z, nx, ny, nz): 6 floats, 24 bytes (see Lab-04 sheet, page 2).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "math/Math3D.h"

namespace ts {

struct Vertex {
    Vec3 position;
    Vec3 normal;
};

struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;     // unique vertices  -> VBO
    std::vector<uint32_t> indices;    // triangle list    -> EBO (3 indices per triangle, CCW = front)

    size_t vertexCount() const { return vertices.size(); }
    size_t indexCount() const { return indices.size(); }
    size_t triangleCount() const { return indices.size() / 3; }

    // Memory if every triangle stores its own 3 vertices (Lab-04 "technique 1").
    size_t nonIndexedBytes() const { return indices.size() * sizeof(Vertex); }
    // Memory with unique vertices + index buffer (Lab-04 "technique 2").
    size_t indexedBytes() const {
        return vertices.size() * sizeof(Vertex) + indices.size() * sizeof(uint32_t);
    }
};

// Expands an indexed mesh into a flat triangle list (used only for the indexed vs non-indexed benchmark).
std::vector<Vertex> expandToTriangleList(const MeshData& mesh);

}  // namespace ts
