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
    void end();
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
    int palette_ = 0;
};

}  // namespace ts
