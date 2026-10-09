// v9 M6: shimmer metric (App::measureShimmer, used by --bench-shots). Shimmer is the frame-to-frame flicker of thin,
// alpha-tested detail (grass blades, needles, leaf edges) under the sub-pixel TAA jitter: with a frozen scene and a
// still camera, a temporally stable image does not change between frames. Measured on the final 8-bit frame.
#include <glad/gl.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "app/App.h"
#include "rendering/ImageWriter.h"

namespace ts {

namespace {

void readLuma(int w, int h, std::vector<float>& out) {
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    out.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = 0.299f * px[4 * i] + 0.587f * px[4 * i + 1] + 0.114f * px[4 * i + 2];
}

}  // namespace

App::ShimmerResult App::measureShimmer(int w, int h, int warm, int frames) {
    ShimmerResult r;
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    auto render = [&]() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        renderFrame(w, h, true);   // no stepSim / camera update: the scene, wind and grain stand still
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(fbo));
    };
    // Vegetation mask: pixels that change by more than 6 / 255 when grass and vegetation are switched off.
    const bool vegOn = settings_.vegetationOn;
    settings_.vegetationOn = false;
    for (int i = 0; i < warm; ++i) render();
    std::vector<float> bare;
    readLuma(w, h, bare);
    settings_.vegetationOn = vegOn;
    for (int i = 0; i < warm; ++i) render();
    std::vector<float> prev, cur;
    readLuma(w, h, prev);
    std::vector<unsigned char> mask(prev.size());
    size_t maskCount = 0;
    for (size_t i = 0; i < prev.size(); ++i) {
        mask[i] = std::fabs(prev[i] - bare[i]) > 6.0f ? 1 : 0;
        maskCount += mask[i];
    }
    double sumVeg = 0.0, sumAll = 0.0;
    for (int f = 0; f < frames; ++f) {
        render();
        readLuma(w, h, cur);
        double veg = 0.0, all = 0.0;
        for (size_t i = 0; i < cur.size(); ++i) {
            const double d = std::fabs(cur[i] - prev[i]);
            all += d;
            if (mask[i]) veg += d;
        }
        sumAll += all / static_cast<double>(cur.size());
        if (const char* dump = std::getenv("TS_SHIMMER_DUMP"); dump && *dump && f < 2) {   // debugging: two frames + the mask
            std::vector<uint8_t> rgb(cur.size() * 3);
            for (size_t i = 0; i < cur.size(); ++i) {
                const size_t row = static_cast<size_t>(h - 1 - static_cast<int>(i / static_cast<size_t>(w))) * static_cast<size_t>(w) + i % static_cast<size_t>(w);
                rgb[3 * row] = static_cast<uint8_t>(cur[i]);
                rgb[3 * row + 1] = static_cast<uint8_t>(std::min(255.0, std::fabs(cur[i] - prev[i]) * 16.0));
                rgb[3 * row + 2] = mask[i] ? 255 : 0;
            }
            writePng(std::string(dump) + "/shimmer_" + std::to_string(f) + ".png", w, h, rgb);
        }
        sumVeg += maskCount ? veg / static_cast<double>(maskCount) : 0.0;
        prev.swap(cur);
    }
    r.vegetation = sumVeg / frames;
    r.all = sumAll / frames;
    r.maskFraction = static_cast<double>(maskCount) / static_cast<double>(prev.size());
    return r;
}

}  // namespace ts
