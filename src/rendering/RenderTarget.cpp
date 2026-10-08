#include "rendering/RenderTarget.h"

#include <algorithm>
#include <cstdio>

namespace ts {

RenderTarget::~RenderTarget() { release(); }

void RenderTarget::release() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (colorRb_) glDeleteRenderbuffers(1, &colorRb_);
    if (depthRb_) glDeleteRenderbuffers(1, &depthRb_);
    if (resolveFbo_) glDeleteFramebuffers(1, &resolveFbo_);
    if (resolveTex_) glDeleteTextures(1, &resolveTex_);
    fbo_ = colorRb_ = depthRb_ = resolveFbo_ = resolveTex_ = 0;
    for (GLuint& b : pbo_)
        if (b) {
            glDeleteBuffers(1, &b);
            b = 0;
        }
}

bool RenderTarget::create(int width, int height, int samples, bool singleChannel) {
    release();
    width_ = width;
    height_ = height;
    samples_ = samples;
    single_ = singleChannel;
    const GLenum colorFormat = singleChannel ? GL_R8 : GL_RGBA8;

    glGenTextures(1, &resolveTex_);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, colorFormat, width, height, 0, singleChannel ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &resolveFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex_, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glGenRenderbuffers(1, &colorRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, colorRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, colorFormat, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRb_);
    glGenRenderbuffers(1, &depthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);

    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) std::fprintf(stderr, "Framebuffer %dx%d (samples %d) incomplete\n", width, height, samples);
    return ok;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void RenderTarget::resolve() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
}

void RenderTarget::flipRows(const uint8_t* bottomUp, std::vector<uint8_t>& out) const {
    const size_t row = static_cast<size_t>(width_) * (single_ ? 1 : 3);
    out.resize(row * static_cast<size_t>(height_));
    for (int y = 0; y < height_; ++y)
        std::copy(bottomUp + (height_ - 1 - y) * row, bottomUp + (height_ - y) * row, out.begin() + y * row);
}

void RenderTarget::read(std::vector<uint8_t>& out) {
    resolve();
    const int channels = single_ ? 1 : 3;
    std::vector<uint8_t> bottomUp(static_cast<size_t>(width_) * height_ * channels);
    glReadPixels(0, 0, width_, height_, single_ ? GL_RED : GL_RGB, GL_UNSIGNED_BYTE, bottomUp.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    flipRows(bottomUp.data(), out);
}

void RenderTarget::readAsync(int slot) {
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(width_) * height_ * (single_ ? 1 : 3);
    if (!pbo_[slot]) {
        glGenBuffers(1, &pbo_[slot]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
    resolve();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
    glReadPixels(0, 0, width_, height_, single_ ? GL_RED : GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::collect(int slot, std::vector<uint8_t>& out) {
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(width_) * height_ * (single_ ? 1 : 3);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
    const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    if (p) flipRows(static_cast<const uint8_t*>(p), out);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

}  // namespace ts
