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
    // v7 WP-Q: the same read in two halves: start the copy into pixel buffer 'slot' (no wait), then collect it.
    // Commands issued in between (the main frame) do not delay collect(): it waits only for this copy.
    void readAsync(int slot);
    void collect(int slot, std::vector<uint8_t>& out);
    GLuint texture() const { return resolveTex_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void release();
    int width_ = 0, height_ = 0, samples_ = 0;
    bool single_ = false;
    GLuint fbo_ = 0, colorRb_ = 0, depthRb_ = 0;       // draw target
    GLuint resolveFbo_ = 0, resolveTex_ = 0;           // resolved colour
    static constexpr int kSlots = 4;
    GLuint pbo_[kSlots] = {0, 0, 0, 0};                // readAsync targets (created on first use)
    void resolve();
    void flipRows(const uint8_t* bottomUp, std::vector<uint8_t>& out) const;
};

}  // namespace ts
