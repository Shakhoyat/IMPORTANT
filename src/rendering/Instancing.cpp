#include "rendering/Instancing.h"

namespace ts {

InstanceStream::~InstanceStream() {
    if (!textures_.empty()) glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
    if (!buffers_.empty()) glDeleteBuffers(static_cast<GLsizei>(buffers_.size()), buffers_.data());
}

bool InstanceStream::init(int ringSize) {
    GLint maxTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
    maxTexels_ = maxTexels;
    if (maxTexels_ <= 0) return false;
    // A ring, so a pass never refills a buffer that an earlier pass of the same frame may still be reading (the
    // driver would have to orphan or stall); one frame uses ~8 (shadow cascades, pre-pass, scene, 2 UAV cameras).
    buffers_.resize(static_cast<size_t>(ringSize));
    textures_.resize(static_cast<size_t>(ringSize));
    glGenBuffers(ringSize, buffers_.data());
    glGenTextures(ringSize, textures_.data());
    for (int i = 0; i < ringSize; ++i) {
        glBindBuffer(GL_TEXTURE_BUFFER, buffers_[i]);
        glBufferData(GL_TEXTURE_BUFFER, 16, nullptr, GL_STREAM_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, textures_[i]);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffers_[i]);
    }
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    return true;
}

bool InstanceStream::upload(const std::vector<float>& texels, int unit) {
    if (buffers_.empty() || texels.empty()) return false;
    if (static_cast<long long>(texels.size() / 4) > maxTexels_) return false;
    const size_t i = static_cast<size_t>(next_);
    next_ = (next_ + 1) % static_cast<int>(buffers_.size());
    glBindBuffer(GL_TEXTURE_BUFFER, buffers_[i]);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(texels.size() * sizeof(float)), texels.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_BUFFER, textures_[i]);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffers_[i]);   // re-attach the (orphaned) store
    glActiveTexture(GL_TEXTURE0);
    return true;
}

}  // namespace ts
