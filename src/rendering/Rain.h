// Rain: instanced streaks, each the lab's unit cube stretched along the fall direction (motion-blurred drop).
// The drops live in a box that wraps around the camera (so rain never "runs out"), fall and drift with the
// wind in the vertex shader, and are shaded by the scene's lights with forward scattering (a drop glows when
// it is between the eye and a lamp or inside the searchlight cone). Additive blending, no depth writes.
#pragma once

#include <glad/gl.h>

#include <memory>

#include "rendering/Shader.h"

namespace ts {

class RainField {
public:
    ~RainField();
    bool init();
    void setCount(int drops);
    int count() const { return count_; }
    Shader& shader() { return *shader_; }
    void draw();  // uniforms (camera, lights, time, intensity) must be set on shader() first

private:
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0;
    int count_ = -1;
};

}  // namespace ts
