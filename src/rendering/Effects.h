// EffectsRenderer: explosions and weapon effects (fire, smoke, dust, sparks, embers, flashes, shock waves,
// tracers, missile / shell bodies and exhaust, scorch decals).
//
// Sprites are instanced quads taken from the lab's unit cube (only its +Z face is used, the other faces are
// collapsed in the vertex shader). One instance = 4 x vec4 (centre + radius, velocity + brightness,
// age/life + seed + heat + kind, age + life + half length). All effect kinds share ONE program and ONE
// premultiplied-alpha blend state (glBlendFunc(ONE, ONE_MINUS_SRC_ALPHA)): smoke writes (lit colour * alpha, alpha),
// emissive sprites write (HDR colour, ~0), so "additive" and "alpha" are the same blend with different alpha.
// Draw order: smoke / trail / dust sorted back to front, then the emissive group (fire, flash, sparks,
// embers, tracers, glows, shock waves) sorted back to front as well; the fire's soot edge needs the order.
//
// Uniforms the caller's prepare(Shader&) must set (all names are the renderer's usual ones, see
// Renderer::setFrameUniforms): uView uProj uCamPos uAmbient uMoonDir uMoonColor uSkyZenith uSpot* uPractical*
// uViewMode uHdr uPalette uFog* uVolumetric uTime uResolution uNearFar uWorldRadius.
// Optional soft-particle depth: bind the scene pre-pass depth texture to texture UNIT 10 and set
// "uHasSceneDepth" = 1 in prepare (default 0 = no soft fade). The sampler "uSceneDepth" is set to unit 10 here.
// The scene depth must be the window-space depth of the same camera (uNearFar linearises it).
//
// Views: visible (uViewMode 1) = lit / emissive HDR. Thermal (0) and raw heat (4) = heat scalar through
// finalColor, alpha = coverage. Diagnostic / depth views (2, 3) draw nothing.
#pragma once

#include <glad/gl.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "math/Math3D.h"
#include "rendering/Shader.h"
#include "primitives/MeshData.h"
#include "scene/EffectsData.h"

namespace ts {

struct EffectsFrame {  // views into Combat's state
    const std::vector<EffectParticle>* particles = nullptr;
    const std::vector<Projectile>* projectiles = nullptr;
    const std::vector<ScorchMark>* scorches = nullptr;
};

class EffectsRenderer {
public:
    EffectsRenderer();
    ~EffectsRenderer();
    EffectsRenderer(const EffectsRenderer&) = delete;
    EffectsRenderer& operator=(const EffectsRenderer&) = delete;

    bool init();

    // Scorch decals on the ground and projectile bodies; after the solids, depth test on.
    // Returns the number of draw calls.
    template <typename Prepare>
    int drawOpaque(const EffectsFrame& f, const Mat4& viewProj, const Vec3& camPos, Prepare prepare, float time) {
        (void)viewProj;
        if (!ready_) return 0;
        int calls = 0;
        if (f.scorches && !f.scorches->empty()) {
            beginSprites(prepare, camPos, time);
            calls += drawScorches(*f.scorches);
        }
        if (f.projectiles && !f.projectiles->empty()) {
            body_->use();
            prepare(*body_);
            body_->set("uCamPos", camPos);
            body_->set("uTime", time);
            body_->set("uFogged", 1.0f);
            calls += drawBodies(*f.projectiles);
        }
        return calls;
    }

    // Smoke / dust / trails (alpha), then fire / flash / sparks / embers / tracers / glows / shock waves.
    template <typename Prepare>
    int drawTransparent(const EffectsFrame& f, const Mat4& viewProj, const Vec3& camPos, Prepare prepare, float time) {
        (void)viewProj;
        if (!ready_) return 0;
        const bool any = (f.particles && !f.particles->empty()) || (f.projectiles && !f.projectiles->empty());
        if (!any) return 0;
        beginSprites(prepare, camPos, time);
        return drawSprites(f, camPos);
    }

private:
    template <typename Prepare>
    void beginSprites(Prepare& prepare, const Vec3& camPos, float time) {
        sprite_->use();
        prepare(*sprite_);
        sprite_->set("uCamPos", camPos);
        sprite_->set("uTime", time);
        sprite_->set("uFogged", 1.0f);
    }
    int drawScorches(const std::vector<ScorchMark>& scorches);
    int drawBodies(const std::vector<Projectile>& projectiles);
    int drawSprites(const EffectsFrame& f, const Vec3& camPos);

    struct GlMesh {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
    };
    static void makeMesh(GlMesh& m, const MeshData& data);
    void drawPart(const GlMesh& m, const Mat4& basis, const Vec3& scale, const Vec3& offset, const Vec3& colour,
                  const Vec3& emit, float heat);
    void drawRange(size_t first, size_t count);

    std::unique_ptr<Shader> sprite_, body_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, instanceVbo_ = 0;
    GLsizei indexCount_ = 0;
    GlMesh cylinder_, cone_, cube_;
    std::vector<float> scratch_;                  // instance staging (16 floats each)
    struct Sortable {
        float key;
        unsigned index;
    };
    std::vector<Sortable> order_;
    bool ready_ = false;
};

}  // namespace ts
