#include "rendering/FrameBuffers.h"

#include <algorithm>
#include <cstdio>

namespace ts {

namespace {

constexpr int kBloomLevels = 6;

GLuint makeDepthTexture(GLenum target, int size, int layers) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(target, tex);
    if (target == GL_TEXTURE_2D_ARRAY)
        glTexImage3D(target, 0, GL_DEPTH_COMPONENT24, size, size, layers, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    else
        glTexImage2D(target, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // LINEAR + compare = hardware 2x2 PCF
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    return tex;
}

bool complete(const char* what) {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "Framebuffer '%s' incomplete (0x%x)\n", what, status);
    return status == GL_FRAMEBUFFER_COMPLETE;
}

}  // namespace

bool ColorTarget::create(int width, int height, GLenum internalFormat, GLenum format, GLenum type, GLenum filter) {
    release();
    w = width;
    h = height;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return complete("colour target");
}

void ColorTarget::release() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (tex) glDeleteTextures(1, &tex);
    fbo = tex = 0;
}

void ColorTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
}

FrameBuffers::~FrameBuffers() { release(); }

void FrameBuffers::release() {
    if (sceneFbo) glDeleteFramebuffers(1, &sceneFbo);
    if (sceneColorRb) glDeleteRenderbuffers(1, &sceneColorRb);
    if (sceneDepthRb) glDeleteRenderbuffers(1, &sceneDepthRb);
    if (prepassFbo) glDeleteFramebuffers(1, &prepassFbo);
    if (prepassDepth) glDeleteTextures(1, &prepassDepth);
    if (prepassNormal) glDeleteTextures(1, &prepassNormal);
    sceneFbo = sceneColorRb = sceneDepthRb = prepassFbo = prepassDepth = prepassNormal = 0;
    resolved.release();
    ao[0].release();
    ao[1].release();
    fog.release();
    for (ColorTarget& b : bloom) b.release();
    bloom.clear();
    w_ = h_ = samples_ = fogDiv_ = 0;
}

bool FrameBuffers::ensure(int width, int height, int samples, int fogDivisor) {
    if (width == w_ && height == h_ && samples == samples_ && fogDivisor == fogDiv_) return true;
    release();
    fogDiv_ = fogDivisor;
    w_ = width;
    h_ = height;
    samples_ = samples;

    glGenFramebuffers(1, &sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glGenRenderbuffers(1, &sceneColorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneColorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_R11F_G11F_B10F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorRb);
    glGenRenderbuffers(1, &sceneDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRb);
    bool ok = complete("HDR scene");

    ok = resolved.create(width, height, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, GL_LINEAR) && ok;

    halfW = std::max(1, width / 2);
    halfH = std::max(1, height / 2);
    glGenTextures(1, &prepassDepth);
    glBindTexture(GL_TEXTURE_2D, prepassDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, halfW, halfH, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &prepassNormal);
    glBindTexture(GL_TEXTURE_2D, prepassNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, halfW, halfH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &prepassFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, prepassFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prepassNormal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, prepassDepth, 0);
    ok = complete("pre-pass") && ok;

    ok = ao[0].create(halfW, halfH, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR) && ok;
    ok = ao[1].create(halfW, halfH, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR) && ok;
    ok = fog.create(std::max(1, width / fogDivisor), std::max(1, height / fogDivisor), GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR) && ok;
    int bw = std::max(1, width / 4), bh = std::max(1, height / 4);
    for (int i = 0; i < kBloomLevels && bw >= 2 && bh >= 2; ++i) {
        bloom.emplace_back();
        ok = bloom.back().create(bw, bh, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, GL_LINEAR) && ok;
        bw /= 2;
        bh /= 2;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void FrameBuffers::bindScene() const {
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glViewport(0, 0, w_, h_);
}

void FrameBuffers::resolveScene() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved.fbo);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffers::bindPrepass() const {
    glBindFramebuffer(GL_FRAMEBUFFER, prepassFbo);
    glViewport(0, 0, halfW, halfH);
}

ShadowMaps::~ShadowMaps() { release(); }

void ShadowMaps::release() {
    if (moonFbo_) glDeleteFramebuffers(1, &moonFbo_);
    if (spotFbo_) glDeleteFramebuffers(1, &spotFbo_);
    if (moonTex) glDeleteTextures(1, &moonTex);
    if (spotTex) glDeleteTextures(1, &spotTex);
    if (staticFbo_) glDeleteFramebuffers(1, &staticFbo_);
    if (staticTex) glDeleteTextures(1, &staticTex);
    moonFbo_ = spotFbo_ = moonTex = spotTex = staticFbo_ = staticTex = 0;
    staticValid = false;
    moonRes = spotRes = 0;
}

void ShadowMaps::ensure(int moonResolution, int spotResolution) {
    spotResolution = std::max(spotResolution, 1);  // a 1x1 map keeps the sampler valid when spot shadows are off
    if (moonResolution == moonRes && spotResolution == spotRes) return;
    release();
    moonRes = moonResolution;
    spotRes = spotResolution;
    moonTex = makeDepthTexture(GL_TEXTURE_2D_ARRAY, moonRes, 2);
    glGenFramebuffers(1, &moonFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, moonFbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, moonTex, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    complete("moon shadow");
    staticTex = makeDepthTexture(GL_TEXTURE_2D, moonRes, 1);
    glGenFramebuffers(1, &staticFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, staticFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, staticTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    complete("static shadow cache");
    spotTex = makeDepthTexture(GL_TEXTURE_2D, spotRes, 1);
    glGenFramebuffers(1, &spotFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, spotFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, spotTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    complete("spot shadow");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMaps::bindMoonLayer(int layer) const {
    glBindFramebuffer(GL_FRAMEBUFFER, moonFbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, moonTex, 0, layer);
    glViewport(0, 0, moonRes, moonRes);
}

void ShadowMaps::bindStaticCache() const {
    glBindFramebuffer(GL_FRAMEBUFFER, staticFbo_);
    glViewport(0, 0, moonRes, moonRes);
}

void ShadowMaps::copyStaticCacheToLayer(int layer) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, staticFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, moonFbo_);
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, moonTex, 0, layer);
    glBlitFramebuffer(0, 0, moonRes, moonRes, 0, 0, moonRes, moonRes, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, moonFbo_);
    glViewport(0, 0, moonRes, moonRes);
}

void ShadowMaps::bindSpot() const {
    glBindFramebuffer(GL_FRAMEBUFFER, spotFbo_);
    glViewport(0, 0, spotRes, spotRes);
}

}  // namespace ts
