#include "rendering/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "rendering/ShaderSources.h"

namespace ts {

namespace {

constexpr int kFirstChar = 32, kLastChar = 126, kAtlasCols = 16;
#include "rendering/FontAtlas.inc"

}  // namespace

Hud::~Hud() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (atlas_) glDeleteTextures(1, &atlas_);
    for (GLuint& t : gray_)
        if (t) glDeleteTextures(1, &t);
}

bool Hud::init() {
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("hud", glsl::kHudVert, glsl::withPalette(glsl::kHudFrag))) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);

    // --- Glyph atlas: printable ASCII in 16 columns, baked into the program by tools/bake_font.py ---
    cellW_ = kFontCellW;
    cellH_ = kFontCellH;
    atlasW_ = kFontAtlasW;
    atlasH_ = kFontAtlasH;
    std::vector<uint8_t> alpha;
    alpha.reserve(static_cast<size_t>(atlasW_) * atlasH_);
    for (size_t i = 0; i < sizeof(kFontAtlasRle); ++i) {
        if (kFontAtlasRle[i] == 0 && i + 1 < sizeof(kFontAtlasRle)) alpha.insert(alpha.end(), kFontAtlasRle[++i], 0);
        else alpha.push_back(kFontAtlasRle[i]);
    }
    if (alpha.size() != static_cast<size_t>(atlasW_) * atlasH_) return false;

    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW_, atlasH_, 0, GL_RED, GL_UNSIGNED_BYTE, alpha.data());
    glGenerateMipmap(GL_TEXTURE_2D);  // small HUD text is minified from the 48 px atlas
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Hud::begin(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    tris_.clear();
    lines_.clear();
    glyphs_.clear();
    image_.clear();
    imageTex_ = 0;
    images_.clear();
}

void Hud::toNdc(float px, float py, float& nx, float& ny) const {
    nx = px / static_cast<float>(width_) * 2.0f - 1.0f;
    ny = 1.0f - py / static_cast<float>(height_) * 2.0f;
}

void Hud::push(std::vector<V>& list, float px, float py, Rgba c, float u, float v) {
    float x, y;
    toNdc(px, py, x, y);
    list.push_back({x, y, c.r, c.g, c.b, c.a, u, v});
}

void Hud::line(float x0, float y0, float x1, float y1, Rgba c) {
    push(lines_, x0, y0, c);
    push(lines_, x1, y1, c);
}

void Hud::rect(float x, float y, float w, float h, Rgba c) {
    push(tris_, x, y, c);
    push(tris_, x, y + h, c);
    push(tris_, x + w, y + h, c);
    push(tris_, x, y, c);
    push(tris_, x + w, y + h, c);
    push(tris_, x + w, y, c);
}

void Hud::frame(float x, float y, float w, float h, Rgba c) {
    line(x, y, x + w, y, c);
    line(x + w, y, x + w, y + h, c);
    line(x + w, y + h, x, y + h, c);
    line(x, y + h, x, y, c);
}

void Hud::brackets(float x, float y, float w, float h, float len, Rgba c) {
    line(x, y, x + len, y, c);
    line(x, y, x, y + len, c);
    line(x + w, y, x + w - len, y, c);
    line(x + w, y, x + w, y + len, c);
    line(x, y + h, x + len, y + h, c);
    line(x, y + h, x, y + h - len, c);
    line(x + w, y + h, x + w - len, y + h, c);
    line(x + w, y + h, x + w, y + h - len, c);
}

float Hud::textWidth(const std::string& s, float pixelHeight) const {
    return static_cast<float>(s.size()) * pixelHeight * static_cast<float>(cellW_) / static_cast<float>(cellH_);
}

void Hud::text(float x, float y, const std::string& s, float pixelHeight, Rgba c) {
    const float scale = pixelHeight / static_cast<float>(cellH_);
    const float gw = static_cast<float>(cellW_) * scale;
    for (char ch : s) {
        const int code = static_cast<unsigned char>(ch);
        if (code >= kFirstChar && code <= kLastChar && ch != ' ') {
            const int i = code - kFirstChar;
            const float u0 = static_cast<float>((i % kAtlasCols) * cellW_) / atlasW_;
            const float v0 = static_cast<float>((i / kAtlasCols) * cellH_) / atlasH_;
            const float u1 = u0 + static_cast<float>(cellW_) / atlasW_;
            const float v1 = v0 + static_cast<float>(cellH_) / atlasH_;
            push(glyphs_, x, y, c, u0, v0);
            push(glyphs_, x, y + pixelHeight, c, u0, v1);
            push(glyphs_, x + gw, y + pixelHeight, c, u1, v1);
            push(glyphs_, x, y, c, u0, v0);
            push(glyphs_, x + gw, y + pixelHeight, c, u1, v1);
            push(glyphs_, x + gw, y, c, u1, v0);
        }
        x += gw;
    }
}

void Hud::heatImage(float x, float y, float w, float h, GLuint texture) {
    const Rgba white{1, 1, 1, 1};
    image_.clear();
    // Texture row 0 is the bottom of the camera image (OpenGL convention), hence v = 1 at the top.
    push(image_, x, y, white, 0, 1);
    push(image_, x, y + h, white, 0, 0);
    push(image_, x + w, y + h, white, 1, 0);
    push(image_, x, y, white, 0, 1);
    push(image_, x + w, y + h, white, 1, 0);
    push(image_, x + w, y, white, 1, 1);
    imageTex_ = texture;
}

void Hud::upload(const std::vector<V>& verts, GLenum mode, int shaderMode, GLuint tex) {
    if (verts.empty()) return;
    shader_->set("uMode", shaderMode);
    if (tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        shader_->set("uTex", 0);
    }
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(V), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));
}

void Hud::end() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_->use();
    shader_->set("uPalette", palette_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    upload(tris_, GL_TRIANGLES, 0, 0);
    if (imageTex_) upload(image_, GL_TRIANGLES, 2, imageTex_);
    for (const Image& im : images_) upload(im.verts, GL_TRIANGLES, 2, im.tex);
    upload(lines_, GL_LINES, 0, 0);
    upload(glyphs_, GL_TRIANGLES, 1, atlas_);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void Hud::flush() {
    end();
    tris_.clear();
    lines_.clear();
    glyphs_.clear();
    image_.clear();
    imageTex_ = 0;
    images_.clear();
}

// ---- v7.1 WP-U game UI primitives ---------------------------------------------------------------------------------------

void Hud::rectGrad(float x, float y, float w, float h, Rgba top, Rgba bottom) {
    push(tris_, x, y, top);
    push(tris_, x, y + h, bottom);
    push(tris_, x + w, y + h, bottom);
    push(tris_, x, y, top);
    push(tris_, x + w, y + h, bottom);
    push(tris_, x + w, y, top);
}

void Hud::rectGradH(float x, float y, float w, float h, Rgba left, Rgba right) {
    push(tris_, x, y, left);
    push(tris_, x, y + h, left);
    push(tris_, x + w, y + h, right);
    push(tris_, x, y, left);
    push(tris_, x + w, y + h, right);
    push(tris_, x + w, y, right);
}

void Hud::tri(float x0, float y0, float x1, float y1, float x2, float y2, Rgba c) {
    push(tris_, x0, y0, c);
    push(tris_, x1, y1, c);
    push(tris_, x2, y2, c);
}

void Hud::disc(float cx, float cy, float r, Rgba c, int segments) {
    arc(cx, cy, 0.0f, r, 0.0f, 6.2831853f, c, segments);
}

void Hud::arc(float cx, float cy, float r0, float r1, float a0, float a1, Rgba c, int segments) {
    if (segments <= 0) segments = std::max(12, std::min(96, static_cast<int>(r1 * std::fabs(a1 - a0) * 0.6f)));
    for (int i = 0; i < segments; ++i) {
        const float t0 = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float c0 = std::cos(t0), s0 = std::sin(t0), c1 = std::cos(t1), s1 = std::sin(t1);
        if (r0 <= 0.0f) {
            tri(cx, cy, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, c);
        } else {
            tri(cx + c0 * r0, cy + s0 * r0, cx + c0 * r1, cy + s0 * r1, cx + c1 * r1, cy + s1 * r1, c);
            tri(cx + c0 * r0, cy + s0 * r0, cx + c1 * r1, cy + s1 * r1, cx + c1 * r0, cy + s1 * r0, c);
        }
    }
}

void Hud::thickLine(float x0, float y0, float x1, float y1, float width, Rgba c) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    tri(x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 - nx, y1 - ny, c);
    tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, c);
}

void Hud::roundRect(float x, float y, float w, float h, float r, Rgba c) {
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    if (r < 0.5f) {
        rect(x, y, w, h, c);
        return;
    }
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    const float pi = 3.14159265f;
    arc(x + r, y + r, 0.0f, r, pi, 1.5f * pi, c, 8);
    arc(x + w - r, y + r, 0.0f, r, 1.5f * pi, 2.0f * pi, c, 8);
    arc(x + w - r, y + h - r, 0.0f, r, 0.0f, 0.5f * pi, c, 8);
    arc(x + r, y + h - r, 0.0f, r, 0.5f * pi, pi, c, 8);
}

void Hud::roundFrame(float x, float y, float w, float h, float r, float width, Rgba c) {
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    rect(x + r, y, w - 2 * r, width, c);
    rect(x + r, y + h - width, w - 2 * r, width, c);
    rect(x, y + r, width, h - 2 * r, c);
    rect(x + w - width, y + r, width, h - 2 * r, c);
    if (r < 0.5f) return;
    const float pi = 3.14159265f, ri = std::max(0.0f, r - width);
    arc(x + r, y + r, ri, r, pi, 1.5f * pi, c, 8);
    arc(x + w - r, y + r, ri, r, 1.5f * pi, 2.0f * pi, c, 8);
    arc(x + w - r, y + h - r, ri, r, 0.0f, 0.5f * pi, c, 8);
    arc(x + r, y + h - r, ri, r, 0.5f * pi, pi, c, 8);
}

void Hud::heatImage(float x, float y, float w, float h, GLuint texture, bool topDown) {
    if (!texture) return;
    const Rgba white{1, 1, 1, 1};
    Image im;
    im.tex = texture;
    const float vt = topDown ? 0.0f : 1.0f, vb = 1.0f - vt;
    push(im.verts, x, y, white, 0, vt);
    push(im.verts, x, y + h, white, 0, vb);
    push(im.verts, x + w, y + h, white, 1, vb);
    push(im.verts, x, y, white, 0, vt);
    push(im.verts, x + w, y + h, white, 1, vb);
    push(im.verts, x + w, y, white, 1, vt);
    images_.push_back(std::move(im));
}

GLuint Hud::uploadGray(int slot, int w, int h, const unsigned char* data) {
    if (slot < 0 || slot >= 4 || w <= 0 || h <= 0 || !data) return 0;
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!gray_[slot] || grayW_[slot] != w || grayH_[slot] != h) {
        if (!gray_[slot]) glGenTextures(1, &gray_[slot]);
        glBindTexture(GL_TEXTURE_2D, gray_[slot]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        grayW_[slot] = w;
        grayH_[slot] = h;
    } else {
        glBindTexture(GL_TEXTURE_2D, gray_[slot]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, data);
    }
    return gray_[slot];
}

}  // namespace ts
