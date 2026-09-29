// GPU render targets of the v3 pipeline:
//   HDR scene: multisampled RGBA16F colour + RGBA8 surface (world normal, reflectivity) + depth
//     -> resolved colour, surface and depth textures (full resolution)
//   pre-pass (depth + view normal, full or half resolution) -> SSAO (ping-pong R8) and volumetric fog (RGBA16F)
//   lit (fog + reflections applied), TAA history ping-pong, DOF, bloom mip chain, lens flare,
//   luminance / heat reduction chain + 1x1 adaptation ping-pong (auto-exposure, thermal AGC)
//   shadow maps (moon: 2-layer depth array + static cache, searchlight: depth texture).
#pragma once

#include <glad/gl.h>

#include <vector>

namespace ts {

struct ColorTarget {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    bool create(int width, int height, GLenum internalFormat, GLenum format, GLenum type, GLenum filter);
    void release();
    void bind() const;
};

class FrameBuffers {
public:
    ~FrameBuffers();
    // (Re)creates everything when a size or sample count changes. Returns false if incomplete.
    bool ensure(int width, int height, int samples, int fogDivisor, int prepassDivisor);
    void bindScene(bool withSurface) const;  // multisampled HDR target; surface attachment on or off
    void resolveScene() const;               // MSAA -> resolved colour, surface and depth textures
    void bindPrepass() const;                // depth + normal pre-pass

    int width() const { return w_; }
    int height() const { return h_; }
    int samples() const { return samples_; }

    GLuint sceneFbo = 0, sceneColorRb = 0, sceneSurfaceRb = 0, sceneDepthRb = 0;
    ColorTarget resolved;                         // full resolution HDR colour
    ColorTarget surface;                          // rgb world normal * 0.5 + 0.5, a reflectivity (SSR weight)
    GLuint depthTex = 0, depthFbo = 0;            // resolved full-resolution depth
    GLuint prepassFbo = 0, prepassDepth = 0, prepassNormal = 0;
    int halfW = 0, halfH = 0;                     // pre-pass / SSAO resolution
    ColorTarget ao[2];                            // raw SSAO, blurred SSAO
    ColorTarget fog;                              // rgb in-scatter, a transmittance
    ColorTarget lit;                              // scene with fog and reflections (TAA input)
    ColorTarget history[2];                       // TAA output / history ping-pong
    ColorTarget dof;                              // half resolution bokeh (rgb) + CoC (a)
    ColorTarget dofCombined;                      // full resolution image after depth of field
    std::vector<ColorTarget> bloom;               // mip 0 = quarter resolution
    ColorTarget flare, streak;                    // lens ghosts + halo, anamorphic streak (quarter resolution)
    std::vector<ColorTarget> reduce;              // 64x64 -> 16x16 -> 4x4 -> 1x1 statistics
    ColorTarget adapt[2];                         // 1x1 temporally adapted statistics (ping-pong)

private:
    void release();
    int w_ = 0, h_ = 0, samples_ = -1, fogDiv_ = 0, preDiv_ = 0;
};

class ShadowMaps {
public:
    ~ShadowMaps();
    void ensure(int moonResolution, int spotResolution);
    void bindMoonLayer(int layer) const;
    void bindSpot() const;
    void bindStaticCache() const;           // static casters of the fixed world cascade, rendered once
    void copyStaticCacheToLayer(int layer) const;

    GLuint moonTex = 0, spotTex = 0, staticTex = 0;
    int moonRes = 0, spotRes = 0;
    bool staticValid = false;
    float staticKey = -1.0f;                // explode factor the cache was rendered with

private:
    void release();
    GLuint moonFbo_ = 0, spotFbo_ = 0, staticFbo_ = 0;
};

}  // namespace ts
