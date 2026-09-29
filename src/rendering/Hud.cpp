#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "rendering/Hud.h"

#include <cstring>

#include "rendering/ShaderSources.h"

namespace ts {

namespace {

constexpr int kFirstChar = 32, kLastChar = 126, kAtlasCols = 16;

}  // namespace

Hud::~Hud() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (atlas_) glDeleteTextures(1, &atlas_);
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

    // --- Rasterise printable ASCII with GDI into a 16-column glyph atlas ---
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontA(-32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    cellW_ = tm.tmAveCharWidth + 2;
    cellH_ = tm.tmHeight + 2;
    const int rows = (kLastChar - kFirstChar + 1 + kAtlasCols - 1) / kAtlasCols;
    atlasW_ = cellW_ * kAtlasCols;
    atlasH_ = cellH_ * rows;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = atlasW_;
    bi.bmiHeader.biHeight = -atlasH_;  // top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, static_cast<size_t>(atlasW_) * atlasH_ * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int ch = kFirstChar; ch <= kLastChar; ++ch) {
        const int i = ch - kFirstChar;
        const char c = static_cast<char>(ch);
        TextOutA(dc, (i % kAtlasCols) * cellW_ + 1, (i / kAtlasCols) * cellH_ + 1, &c, 1);
    }
    GdiFlush();
    std::vector<uint8_t> alpha(static_cast<size_t>(atlasW_) * atlasH_);
    const uint8_t* px = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < alpha.size(); ++i) alpha[i] = px[i * 4 + 1];  // green channel of white text

    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);

    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW_, atlasH_, 0, GL_RED, GL_UNSIGNED_BYTE, alpha.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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
    upload(lines_, GL_LINES, 0, 0);
    upload(glyphs_, GL_TRIANGLES, 1, atlas_);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace ts
