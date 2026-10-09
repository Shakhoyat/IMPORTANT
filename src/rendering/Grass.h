// Instanced grass: every blade is a tapered 3-segment strip (v9; the unit cube before), bent in the vertex shader.
// One VAO: strip VBO/EBO + a per-instance buffer (root position, yaw, height, width, lean, seed). Blades are sorted
// into a grid of cells (random order inside a cell); each visible cell is one glDrawElementsInstanced call that
// submits only the share of its blades its distance needs (v9 distance LOD).
#pragma once

#include <glad/gl.h>

#include <memory>
#include <vector>

#include "math/Frustum.h"
#include "rendering/Shader.h"
#include "rendering/GroundMap.h"

namespace ts {

class GrassField {
public:
    ~GrassField();
    bool init();                 // builds the shader and the cube geometry
    void setCount(int blades);   // (re)generates the instance buffer; deterministic placement
    void setDensityMap(const GroundMap* map) { map_ = map; count_ = requested_ = -1; }
    int count() const { return count_; }
    int submitted() const { return submitted_; }       // v9: blades submitted by the last draw (after distance LOD)
    int trianglesPerBlade() const { return indexCount_ / 3; }

    struct Pusher {              // something that flattens the grass (rover, rotor wash)
        Vec3 position;
        float radius = 0.0f;
        float strength = 0.0f;
    };
    // Returns the number of draw calls. 'prepare' sets the frame uniforms on the grass shader.
    template <typename Prepare>
    int draw(const Mat4& viewProj, const Vec3& camPos, Prepare prepare, const Pusher& a, const Pusher& b, float time);
    Shader& shader() { return *shader_; }

private:
    int drawCells(const Mat4& viewProj, const Vec3& camPos);

    struct Cell {
        int first = 0, count = 0;
        Vec3 centre;
        float radius = 0.0f;
    };
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0;
    int count_ = -1;
    int requested_ = -1;
    int submitted_ = 0;   // v7 WP-Q: blade budget of the last build (count_ = blades placed, usually fewer)
    std::vector<Cell> cells_;
    const GroundMap* map_ = nullptr;
};

template <typename Prepare>
int GrassField::draw(const Mat4& viewProj, const Vec3& camPos, Prepare prepare, const Pusher& a, const Pusher& b, float time) {
    if (count_ <= 0) return 0;
    shader_->use();
    prepare(*shader_);
    shader_->set("uTime", time);
    shader_->set("uPusherA", a.position);
    shader_->set("uPusherAParams", a.radius, a.strength);
    shader_->set("uPusherB", b.position);
    shader_->set("uPusherBParams", b.radius, b.strength);
    return drawCells(viewProj, camPos);
}

}  // namespace ts
