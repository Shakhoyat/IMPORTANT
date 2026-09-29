#include "rendering/Mesh.h"

#include <cstddef>

namespace ts {

namespace {

// Attribute table (location, components, offset, stride):
//   0 position  3 floats  offset 0              stride 24 bytes
//   1 normal    3 floats  offset 12 (3 floats)  stride 24 bytes
void describeVertexLayout() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
}

}  // namespace

Mesh::~Mesh() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (flatVbo_) glDeleteBuffers(1, &flatVbo_);
    if (flatVao_) glDeleteVertexArrays(1, &flatVao_);
}

void Mesh::upload(const MeshData& data) {
    indexCount_ = static_cast<GLsizei>(data.indices.size());
    vertexCount_ = static_cast<GLsizei>(data.vertices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);  // the EBO binding is stored in the VAO
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_STATIC_DRAW);
    describeVertexLayout();

    const std::vector<Vertex> expanded = expandToTriangleList(data);
    glGenVertexArrays(1, &flatVao_);
    glGenBuffers(1, &flatVbo_);
    glBindVertexArray(flatVao_);
    glBindBuffer(GL_ARRAY_BUFFER, flatVbo_);
    glBufferData(GL_ARRAY_BUFFER, expanded.size() * sizeof(Vertex), expanded.data(), GL_STATIC_DRAW);
    describeVertexLayout();
    glBindVertexArray(0);
}

void Mesh::bind(bool indexed) const { glBindVertexArray(indexed ? vao_ : flatVao_); }

void Mesh::draw(bool indexed) const {
    if (indexed) glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    else glDrawArrays(GL_TRIANGLES, 0, indexCount_);
}

}  // namespace ts
