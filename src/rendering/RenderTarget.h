// Off-screen framebuffer: multisampled colour + depth, resolved into a plain texture for read-back.
// Used for the UAV camera (tracker input) and for screenshot captures.
#pragma once

#include <glad/gl.h>

#include <cstdint>
#include <vector>

namespace ts {

class RenderTarget {
public:
    ~RenderTarget();
    // samples = 0 -> single-sampled. singleChannel = true -> GL_R8 colour (heat image).
    bool create(int width, int height, int samples, bool singleChannel);
    void bind() const;  // bind for drawing and set the viewport
    // Resolves MSAA (if any) and reads the pixels, rows top-to-bottom. RGB (3 bytes) or R (1 byte).
    void read(std::vector<uint8_t>& out);
    GLuint texture() const { return resolveTex_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();
    int width_ = 0, height_ = 0, samples_ = 0;
    bool single_ = false;
    GLuint fbo_ = 0, colorRb_ = 0, depthRb_ = 0;       // draw target
    GLuint resolveFbo_ = 0, resolveTex_ = 0;           // resolved colour
};

}  // namespace ts
