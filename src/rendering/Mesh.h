// GPU copy of a MeshData: one VAO + VBO + EBO (Lab-04: "polygon -> VAO, VBO, EBO").
// Each primitive is uploaded once and shared by every component that uses it.
#pragma once

#include <glad/gl.h>

#include "primitives/MeshData.h"

namespace ts {

class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void upload(const MeshData& data);
    void bind(bool indexed) const;
    void draw(bool indexed) const;  // assumes bind() was called

    GLsizei indexCount() const { return indexCount_; }
    GLsizei vertexCount() const { return vertexCount_; }

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLuint flatVao_ = 0, flatVbo_ = 0;  // non-indexed copy, only for the indexed vs non-indexed comparison
    GLsizei indexCount_ = 0, vertexCount_ = 0;
};

}  // namespace ts
