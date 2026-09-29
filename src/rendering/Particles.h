// ParticleField: fireflies and drifting motes, instanced sprites built from the lab's unit cube.
// The cube is used as a camera-facing quad (its two faces facing the camera coincide, so each point is drawn twice at half weight) and the
// fragment shader turns it into a soft round point. Additive HDR (bloom makes the glow), depth test on,
// depth write off.
//   Fireflies: hover 0.3-2.5 m above the ground inside the compound, wander on smooth sine paths and blink
//              individually (1-3 s period, mostly dark). Self-lit, so they need no lights.
//   Motes:     dust / pollen specks in a box that wraps around the camera. They are only visible where the
//              searchlight cone or a practical light scatters into them (like the rain drops).
// Visible view only: the shader outputs nothing in any other view mode.
#pragma once

#include <glad/gl.h>

#include <memory>

#include "math/Math3D.h"
#include "rendering/Shader.h"

namespace ts {

class ParticleField {
public:
    ~ParticleField();
    bool init();
    void setCount(int n);   // fireflies + motes total (60 % / 40 %); deterministic
    int count() const { return count_; }
    Shader& shader() { return *shader_; }

    // prepare(Shader&) sets the standard frame uniforms (lights, uView, uProj, uCamPos, practicals ...).
    // Returns the number of instances drawn.
    template <typename Prepare>
    int draw(const Mat4& viewProj, const Vec3& camPos, Prepare prepare, float time) {
        (void)viewProj;
        if (count_ <= 0 || !shader_) return 0;
        shader_->use();
        prepare(*shader_);
        shader_->set("uCamPos", camPos);
        shader_->set("uTime", time);
        shader_->set("uBox", Vec3{18.0f, 9.0f, 18.0f});
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        drawInstances();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        return count_;
    }

private:
    void drawInstances();

    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0;
    int count_ = -1;
};

}  // namespace ts
