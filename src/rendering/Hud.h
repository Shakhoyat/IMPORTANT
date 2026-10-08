// 2D overlay in pixel coordinates (origin top-left): lines, filled rectangles, text and the UAV camera feed.
// Text uses a glyph atlas baked into the program (tools/bake_font.py), so no font files or OS font API are needed.
#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>
#include <vector>

#include "rendering/Shader.h"

namespace ts {

struct Rgba {
    float r, g, b, a;
};

class Hud {
public:
    ~Hud();
    bool init();
    void begin(int width, int height);
    void line(float x0, float y0, float x1, float y1, Rgba c);
    void rect(float x, float y, float w, float h, Rgba c);         // filled
    void frame(float x, float y, float w, float h, Rgba c);        // outline
    void brackets(float x, float y, float w, float h, float len, Rgba c);
    void text(float x, float y, const std::string& s, float pixelHeight, Rgba c);
    float textWidth(const std::string& s, float pixelHeight) const;
    void heatImage(float x, float y, float w, float h, GLuint texture);  // drawn through the thermal palette
    // v7.1 WP-U: game UI primitives (all filled shapes join the rect batch, so they layer with rects in call order).
    void rectGrad(float x, float y, float w, float h, Rgba top, Rgba bottom);     // vertical gradient
    void rectGradH(float x, float y, float w, float h, Rgba left, Rgba right);    // horizontal gradient
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba c);
    void disc(float cx, float cy, float r, Rgba c, int segments = 0);             // 0 = from the radius
    void arc(float cx, float cy, float r0, float r1, float a0, float a1, Rgba c, int segments = 0);  // filled ring sector (rad)
    void thickLine(float x0, float y0, float x1, float y1, float width, Rgba c);
    void roundRect(float x, float y, float w, float h, float r, Rgba c);
    void roundFrame(float x, float y, float w, float h, float r, float width, Rgba c);   // outline of a rounded rect
    // Heat image, several per frame; topDown = row 0 of the texture is the top of the picture (CPU images).
    void heatImage(float x, float y, float w, float h, GLuint texture, bool topDown);
    // R8 texture 'slot' (0..3) holding a w x h 8-bit image (UAV sensor frames for the feeds); created on first use.
    GLuint uploadGray(int slot, int w, int h, const unsigned char* data);
    void end();
    void flush();  // draws what is queued so far: later calls paint over it (modal panels over world markers)
    void setPalette(int p) { palette_ = p; }

private:
    struct V {
        float x, y, r, g, b, a, u, v;
    };
    void toNdc(float px, float py, float& nx, float& ny) const;
    void push(std::vector<V>& list, float px, float py, Rgba c, float u = 0, float v = 0);
    void upload(const std::vector<V>& verts, GLenum mode, int shaderMode, GLuint tex);

    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0, vbo_ = 0, atlas_ = 0;
    int atlasW_ = 0, atlasH_ = 0, cellW_ = 0, cellH_ = 0;
    int width_ = 1, height_ = 1;
    std::vector<V> tris_, lines_, glyphs_, image_;
    GLuint imageTex_ = 0;
    struct Image { std::vector<V> verts; GLuint tex; };
    std::vector<Image> images_;           // v7.1: extra heat images of this batch
    GLuint gray_[4] = {0, 0, 0, 0};
    int grayW_[4] = {0, 0, 0, 0}, grayH_[4] = {0, 0, 0, 0};
    int palette_ = 0;
};

}  // namespace ts
