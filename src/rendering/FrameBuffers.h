// GPU render targets of the v2 pipeline:
//   HDR scene (multisampled RGBA16F + depth) -> resolved RGBA16F texture
//   half-resolution pre-pass (depth + view normal) -> SSAO (ping-pong R8) and volumetric fog (RGBA16F)
//   bloom mip chain (RGBA16F), shadow maps (moon: 2-layer depth array, searchlight: depth texture).
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
    // (Re)creates everything when the size or sample count changes. Returns false if incomplete.
    bool ensure(int width, int height, int samples, int fogDivisor);
    void bindScene() const;           // multisampled HDR target, full viewport
    void resolveScene() const;        // MSAA -> resolved texture
    void bindPrepass() const;         // half-resolution depth + normal

    int width() const { return w_; }
    int height() const { return h_; }
    int samples() const { return samples_; }

    GLuint sceneFbo = 0, sceneColorRb = 0, sceneDepthRb = 0;
    ColorTarget resolved;                         // full resolution HDR
    GLuint prepassFbo = 0, prepassDepth = 0, prepassNormal = 0;
    int halfW = 0, halfH = 0;
    ColorTarget ao[2];                            // raw SSAO, blurred SSAO
    ColorTarget fog;                              // rgb in-scatter, a transmittance
    std::vector<ColorTarget> bloom;               // mip 0 = quarter resolution

private:
    void release();
    int w_ = 0, h_ = 0, samples_ = 0, fogDiv_ = 0;
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
