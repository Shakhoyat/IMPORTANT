// Instanced foliage cards: every card is the lab's unit cube (createCube), flattened to a thin plate in the
// vertex shader. Cards are scattered over the surface of every foliage cone (pine whorl) and sphere (broadleaf
// clump, bush): the solid primitive stays as the dark inner volume, the cards make the silhouette fluffy.
// A procedural needle / leaf pattern in card UV space cuts the plate out (fragment discard, no image files).
// Same technique as GrassField: one VAO, per-instance buffer, culling cells, one instanced draw per visible cell.
#pragma once

#include <glad/gl.h>

#include <memory>
#include <vector>

#include "math/Frustum.h"
#include "rendering/Shader.h"
#include "scene/Scene.h"

namespace ts {

class FoliageField {
public:
    ~FoliageField();
    bool init();                                   // shaders + card geometry (unit cube)
    void build(const Scene& scene, float density); // (re)generates instances if density changed or first call
    int count() const { return count_; }
    Shader& shader() { return *shader_; }

    // Returns the number of draw calls. 'prepare' sets the frame uniforms (view, proj, lights, shadows, units 5..9).
    template <typename Prepare>
    int draw(const Mat4& viewProj, Prepare prepare, float time);
    // Moon shadow casting with the same cut-out (own depth shader). Returns the number of draw calls.
    int drawDepth(const Mat4& lightViewProj, float time);

private:
    int drawCells(const Mat4& viewProj);

    struct Cell {
        int first = 0, count = 0;
        Vec3 centre;
        float radius = 0.0f;
    };
    std::unique_ptr<Shader> shader_, depthShader_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0;
    size_t plateOffset_ = 0;   // byte offset of the +Y face in the cube index buffer
    int count_ = 0;
    float density_ = -1.0f;
    size_t leafCount_ = 0;
    std::vector<Cell> cells_;
};

template <typename Prepare>
int FoliageField::draw(const Mat4& viewProj, Prepare prepare, float time) {
    if (count_ <= 0) return 0;
    shader_->use();
    prepare(*shader_);
    shader_->set("uTime", time);
    return drawCells(viewProj);
}

}  // namespace ts
