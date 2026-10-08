// v7 WP-I Frame Explorer, part 2 (included by FrameExplorer.cpp): overlay drawing, per-stage diagrams, input.
// Layout: full-screen stage image; top bar (stage number, title, keys) + 16-step timeline; bottom panel (caption,
// concepts, key formula) with the layer chips above it; a stage card on the right (numbers at the probe pixel,
// diagrams); optional magnifier (follows the cursor), pixel inspector, wipe to the previous stage.

namespace ts {

namespace {

const Rgba kAmber{1.0f, 0.70f, 0.24f, 1.0f};
const Rgba kCyan{0.36f, 0.88f, 1.0f, 1.0f};
const Rgba kGreen{0.42f, 1.0f, 0.55f, 1.0f};
const Rgba kRed{1.0f, 0.36f, 0.30f, 1.0f};
const Rgba kText{0.93f, 0.95f, 0.98f, 1.0f};
const Rgba kDim{0.60f, 0.65f, 0.72f, 1.0f};
const Rgba kPanel{0.018f, 0.024f, 0.038f, 0.84f};

Rgba alpha(Rgba c, float a) { return {c.r, c.g, c.b, c.a * a}; }
Rgba rgba(const Vec3& v, float a = 1.0f) { return {v.x, v.y, v.z, a}; }

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof(buf), f, a);
    va_end(a);
    return buf;
}

// Deferred overlay: shapes go straight into the Hud (layer A, under the inset images); text, top lines and inset
// images are queued and drawn after them.
struct Ui {
    explicit Ui(Hud& h) : hud(h) {}
    Hud& hud;
    float k = 1.0f;
    float W = 1, H = 1;
    struct T {
        float x, y;
        std::string s;
        float size;
        Rgba c;
    };
    struct L {
        float x0, y0, x1, y1;
        Rgba c;
    };
    struct I {
        GLuint a, b;
        float x, y, w, h;
        int mode;
        float split;
        float uv[4];
        float alpha;
    };
    std::vector<T> texts;
    std::vector<L> lines;
    std::vector<I> insets;

    float s(float v) const { return v * k; }
    void rect(float x, float y, float w, float h, Rgba c) { hud.rect(x, y, w, h, c); }
    void line(float x0, float y0, float x1, float y1, Rgba c) { hud.line(x0, y0, x1, y1, c); }
    void lineTop(float x0, float y0, float x1, float y1, Rgba c) { lines.push_back({x0, y0, x1, y1, c}); }
    void frameTop(float x, float y, float w, float h, Rgba c) {
        lineTop(x, y, x + w, y, c);
        lineTop(x + w, y, x + w, y + h, c);
        lineTop(x + w, y + h, x, y + h, c);
        lineTop(x, y + h, x, y, c);
    }
    void text(float x, float y, const std::string& str, float size, Rgba c) { texts.push_back({x, y, str, size, c}); }
    float tw(const std::string& str, float size) const { return hud.textWidth(str, size); }
    void inset(GLuint a, float x, float y, float w, float h, int mode = 0, GLuint b = 0, float split = 0.5f, const float* uv = nullptr,
               float al = 1.0f) {
        I i{a, b, x, y, w, h, mode, split, {0, 0, 1, 1}, al};
        if (uv) std::copy(uv, uv + 4, i.uv);
        insets.push_back(i);
    }
    void panel(float x, float y, float w, float h, Rgba accent = kAmber) {
        rect(x, y, w, h, kPanel);
        rect(x, y, w, s(2.0f), alpha(accent, 0.9f));
    }
    // Word-wrapped text; returns the height used.
    float wrap(float x, float y, float width, const std::string& str, float size, Rgba c, float lineGap = 1.32f) {
        std::string line, word;
        float yy = y;
        const auto flush = [&]() {
            if (!line.empty()) text(x, yy, line, size, c);
            yy += size * lineGap;
            line.clear();
        };
        for (size_t i = 0; i <= str.size(); ++i) {
            const char ch = i < str.size() ? str[i] : ' ';
            if (ch != ' ') {
                word += ch;
                continue;
            }
            if (word.empty()) continue;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (tw(cand, size) > width && !line.empty()) {
                flush();
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        }
        if (!line.empty()) flush();
        return yy - y;
    }
    void arrow(float x0, float y0, float x1, float y1, Rgba c, float head = 9.0f, bool top = false) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::max(std::sqrt(dx * dx + dy * dy), 1e-3f);
        const float ux = dx / len, uy = dy / len, hs = s(head);
        const auto L = [&](float a, float b, float cc, float d) { top ? lineTop(a, b, cc, d, c) : line(a, b, cc, d, c); };
        L(x0, y0, x1, y1);
        L(x1, y1, x1 - ux * hs - uy * hs * 0.5f, y1 - uy * hs + ux * hs * 0.5f);
        L(x1, y1, x1 - ux * hs + uy * hs * 0.5f, y1 - uy * hs - ux * hs * 0.5f);
    }
    void ring(float cx, float cy, float r, Rgba c, bool top = false) {
        const int n = 20;
        for (int i = 0; i < n; ++i) {
            const float a0 = 6.28318f * i / n, a1 = 6.28318f * (i + 1) / n;
            if (top) lineTop(cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1), c);
            else line(cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1), c);
        }
    }
    void dot(float cx, float cy, float r, Rgba c) { rect(cx - r, cy - r, 2 * r, 2 * r, c); }
};

std::string v3(const Vec3& v) { return fmt("(%+.2f, %+.2f, %+.2f)", v.x, v.y, v.z); }

// Pixel grid of the raster stage around the probe triangle: origin (snapshot px), cell size k (px per cell).
struct RasterGrid {
    static constexpr int cols = 22, rows = 14;
    float ox = 0, oy = 0;
    int k = 1;
};
RasterGrid rasterGrid(const ProbeTri& p) {
    RasterGrid g;
    const Vec2f v[3] = {p.tr[0].pixel, p.tr[1].pixel, p.tr[2].pixel};
    const float x0 = std::min({v[0].x, v[1].x, v[2].x}), x1 = std::max({v[0].x, v[1].x, v[2].x});
    const float y0 = std::min({v[0].y, v[1].y, v[2].y}), y1 = std::max({v[0].y, v[1].y, v[2].y});
    g.k = std::max(1, static_cast<int>(std::ceil(std::max((x1 - x0) / (RasterGrid::cols - 3), (y1 - y0) / (RasterGrid::rows - 3)))));
    g.ox = std::floor((x0 + x1) * 0.5f - RasterGrid::cols * g.k * 0.5f);
    g.oy = std::floor((y0 + y1) * 0.5f - RasterGrid::rows * g.k * 0.5f);
    return g;
}
// The scanline (snapshot pixel row) at animation time t: fast down to the grid, slow through it (one cell row per
// step the eye can follow), fast to the bottom, then a hold. Returns h when the scan is complete.
constexpr float kScanPeriod = 9.0f, kScanHold = 2.5f;
float scanRow(float t, float gridTop, float gridBottom, float h) {
    t = std::fmod(std::max(t, 0.0f), kScanPeriod + kScanHold);
    gridTop = clampf(gridTop, 0.0f, h);
    gridBottom = clampf(gridBottom, gridTop, h);
    const float a = 1.2f, b = 6.6f, cEnd = kScanPeriod;  // phase ends
    if (t < a) return gridTop * t / a;
    if (t < b) return gridTop + (gridBottom - gridTop) * (t - a) / (b - a);
    if (t < cEnd) return gridBottom + (h - gridBottom) * (t - b) / (cEnd - b);
    return h;
}

}  // namespace

// ================================================================================================ display helper
void FrameExplorer::Impl::image(GLuint a, float x, float y, float w, float h, int W, int H, int mode, GLuint b, float split,
                                const float* uv, float al, float extra) const {
    if (!a) return;
    display->use();
    const float x0 = x / W * 2.0f - 1.0f, x1 = (x + w) / W * 2.0f - 1.0f;
    const float y0 = 1.0f - (y + h) / H * 2.0f, y1 = 1.0f - y / H * 2.0f;
    const GLint rectLoc = glGetUniformLocation(display->id(), "uRect"), uvLoc = glGetUniformLocation(display->id(), "uUv");
    glUniform4f(rectLoc, x0, y0, x1, y1);
    if (uv) glUniform4f(uvLoc, uv[0], uv[1], uv[2], uv[3]);
    else glUniform4f(uvLoc, 0.0f, 0.0f, 1.0f, 1.0f);
    display->set("uMode", mode);
    display->set("uSplit", split);
    display->set("uAlpha", al);
    display->set("uDim", mode == 3 ? 0.22f : 1.0f);
    display->set("uGrid", extra);
    display->set("uMethod", static_cast<int>(extra));
    display->set("uFactor", std::max(1.0f, split));
    display->set("uTexA", 0);
    display->set("uTexB", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, b ? b : a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a);
    if (al < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

// ================================================================================================ image lab
void FrameExplorer::Impl::buildLab(const fx::FrameSnapshot& s, int kernelIndex, float gamma) {
    (void)s;
    if (kernelIndex == labKernel && gamma == labGamma && labTex) return;
    labKernel = kernelIndex;
    labGamma = gamma;
    const fx::Kernel kern = fx::kernel(static_cast<fx::KernelType>(kernelIndex));
    const int w = finalImg.w, h = finalImg.h;
    Img out(w, h);
    fx::GrayImage ch;
    ch.w = w;
    ch.h = h;
    ch.v.resize(static_cast<size_t>(w) * h);
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = to8(fx::applyGamma(i / 255.0f, gamma));
    for (int c = 0; c < 3; ++c) {
        for (size_t i = 0; i < ch.v.size(); ++i) ch.v[i] = finalImg.rgb[i * 3 + static_cast<size_t>(c)] / 255.0f;
        const fx::GrayImage r = kern.type == fx::KernelType::Identity ? ch : fx::convolve(ch, kern);
        for (size_t i = 0; i < r.v.size(); ++i) out.rgb[i * 3 + static_cast<size_t>(c)] = lut[to8(r.v[i])];
    }
    if (labTex) {
        owned.erase(std::remove(owned.begin(), owned.end(), labTex), owned.end());
        glDeleteTextures(1, &labTex);
    }
    labTex = upload(out);
    labHist[0] = fx::histogram(out.rgb, 64);
    for (int c = 0; c < 3; ++c) labHist[c + 1] = fx::histogram(out.rgb, 64, c);
}

// ================================================================================================ input / update
void FrameExplorer::update(float dt) {
    if (view_.playing) anim_ += dt;
    trans_ = std::min(1.0f, trans_ + dt / 0.55f);
}

void FrameExplorer::frame(Renderer& r, GLuint targetFbo, int w, int h, float dt) {
    update(dt);
    draw(r, targetFbo, w, h);
}

bool FrameExplorer::onKey(int key) {
    if (!open_) return false;
    const int n = std::max(1, layerCount(view_.stage));
    switch (key) {
        case KeyGrave:
        case KeyEscape: close(); return true;
        case KeyRight: setStage(view_.stage + 1); return true;
        case KeyLeft: setStage(view_.stage - 1); return true;
        case KeyHome: setStage(0); return true;
        case KeyEnd: setStage(fx::kStageCount - 1); return true;
        case KeyDown: view_.layer = (view_.layer + 1) % n; view_.split = -1.0f; return true;
        case KeyUp: view_.layer = (view_.layer + n - 1) % n; view_.split = -1.0f; return true;
        case KeyW: view_.wipe = !view_.wipe; return true;
        case KeyM: view_.magnifier = !view_.magnifier; return true;
        case KeyI: view_.inspector = !view_.inspector; return true;
        case KeyP: view_.lab = !view_.lab; return true;
        case KeyK: view_.kernel = (view_.kernel + 1) % fx::kKernelCount; return true;
        case KeyLeftBracket: view_.gamma = std::max(0.3f, view_.gamma / 1.12f); return true;
        case KeyRightBracket: view_.gamma = std::min(3.0f, view_.gamma * 1.12f); return true;
        case KeyS: {
            static const int f[4] = {4, 6, 8, 12};
            int i = 0;
            while (i < 3 && f[i] != view_.sampleFactor) ++i;
            view_.sampleFactor = f[(i + 1) % 4];
            return true;
        }
        case KeySpace: view_.playing = !view_.playing; return true;
        case KeyR: anim_ = 0.0f; view_.playing = true; return true;
        case KeyTab: view_.captions = !view_.captions; return true;
        default: break;
    }
    if (key >= KeyZero && key <= KeyNine) {
        setStage(key == KeyZero ? 9 : key - KeyZero - 1);
        return true;
    }
    return false;
}

void FrameExplorer::onCursor(float x, float y) {
    view_.cursorX = x;
    view_.cursorY = y;
    if (dragging_) {
        const float f = clampf(x / std::max(1.0f, lastW_), 0.02f, 0.98f);
        if (view_.wipe) view_.wipeX = f;
        else view_.split = f;
    }
}

void FrameExplorer::onMouseButton(int button, bool down) {
    if (!open_ || button != 0) return;
    if (!down) {
        dragging_ = false;
        return;
    }
    const bool splitLayer = layerCount(view_.stage) > 0 && impl_->layers[view_.stage][static_cast<size_t>(
                                std::min(view_.layer, layerCount(view_.stage) - 1))].b != 0 &&
                            !impl_->layers[view_.stage][static_cast<size_t>(std::min(view_.layer, layerCount(view_.stage) - 1))].reveal;
    const float splitAt = view_.wipe ? view_.wipeX : (view_.split >= 0.0f ? view_.split : 0.5f);
    if (!view_.lab && (view_.wipe || splitLayer) && std::fabs(view_.cursorX - splitAt * lastW_) < 40.0f) {
        dragging_ = true;  // grab the split line
        return;
    }
    if (view_.cursorX >= 0.0f && lastW_ > 0.0f && !view_.lab) {
        const int px = static_cast<int>(view_.cursorX / lastW_ * snap_.w), py = static_cast<int>(view_.cursorY / lastH_ * snap_.h);
        setProbe(px, py);
        anim_ = 0.0f;
    }
}

void FrameExplorer::onScroll(float dy) {
    view_.zoom = clampf(view_.zoom * std::pow(1.25f, dy), 2.0f, 40.0f);
}

// ================================================================================================ stage cards
namespace {

struct Ctx {
    FrameExplorer::Impl& m;
    const fx::FrameSnapshot& s;
    Ui& ui;
    float W, H;
    float cardX, cardY, cardW, cardBottom;  // the stage card column
    float anim;
    int layer;
    int px, py;   // this stage's probe pixel (snapshot coordinates)
    bool mosaic;  // 2 x 2 layer: markers go into every quadrant
    // snapshot pixel -> screen
    float sx(float px) const { return px * W / s.w; }
    float sy(float py) const { return py * H / s.h; }
    bool project(const Mat4& vp, const Vec3& p, float& x, float& y) const {
        const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
        if (c.w <= 1e-4f) return false;
        x = (c.x / c.w * 0.5f + 0.5f) * W;
        y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * H;
        return true;
    }
    Vec3 worldAt(int px, int py) const {
        const float u = (px + 0.5f) / s.w, v = 1.0f - (py + 0.5f) / s.h;
        const float* d = s.depth.at(u, v);
        const float dd = d ? d[0] : 1.0f;
        const Vec4 p = inverse(s.viewProjJ) * Vec4{u * 2 - 1, v * 2 - 1, dd * 2 - 1, 1};
        return Vec3{p.x, p.y, p.z} / p.w;
    }
    // Starts a card; returns the y where its body begins.
    float card(const std::string& title, float h, Rgba accent = kAmber) {
        ui.panel(cardX, cardY, cardW, h, accent);
        ui.text(cardX + ui.s(12), cardY + ui.s(9), title, ui.s(12.5f), accent);
        return cardY + ui.s(30);
    }
    void row(float& y, const std::string& label, const std::string& value, Rgba vc = kText) {
        ui.text(cardX + ui.s(12), y, label, ui.s(12), kDim);
        ui.text(cardX + ui.s(12) + ui.s(118), y, value, ui.s(12), vc);
        y += ui.s(16);
    }
    void marker(float x, float y, Rgba c) {
        if (mosaic) {
            for (int q = 0; q < 4; ++q) marker1((q % 2) * W * 0.5f + x * 0.5f, (q / 2) * H * 0.5f + y * 0.5f, c);
            return;
        }
        marker1(x, y, c);
    }
    void marker1(float x, float y, Rgba c) {
        const float r = ui.s(7);
        ui.ring(x, y, r, c, true);
        ui.lineTop(x - r * 1.8f, y, x - r * 0.6f, y, c);
        ui.lineTop(x + r * 0.6f, y, x + r * 1.8f, y, c);
        ui.lineTop(x, y - r * 1.8f, x, y - r * 0.6f, c);
        ui.lineTop(x, y + r * 0.6f, x, y + r * 1.8f, c);
    }
};

void probeTriangleOnImage(Ctx& c, bool arrows) {
    const ProbeTri& p = c.m.probe;
    if (!p.valid) return;
    float x[3], y[3];
    for (int i = 0; i < 3; ++i) {
        x[i] = c.sx(p.tr[i].pixel.x);
        y[i] = c.sy(p.tr[i].pixel.y);
    }
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        if (arrows) c.ui.arrow(x[i], y[i], x[i] + (x[j] - x[i]) * 0.55f, y[i] + (y[j] - y[i]) * 0.55f, kAmber, 10.0f, true);
        c.ui.lineTop(x[i], y[i], x[j], y[j], kAmber);
        c.ui.text(x[i] + c.ui.s(5), y[i] - c.ui.s(16), fmt("v%d", i), c.ui.s(13), kAmber);
    }
}

// 1 ---------------------------------------------------------------------------------------------- scene graph
void cardGraph(Ctx& c) {
    const ProbeTri& p = c.m.probe;
    if (!p.valid || !c.m.scene) return;
    const SceneNode* n = c.m.scene->leaves()[static_cast<size_t>(p.leaf)];
    // Projected unit box of the leaf (its primitive spans +-0.5 in model space)
    float px[8], py[8];
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
        ok = c.project(c.s.viewProj, transformPoint(n->world, corner), px[i], py[i]) && ok;
    }
    if (ok) {
        static const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& ed : e) c.ui.lineTop(px[ed[0]], py[ed[0]], px[ed[1]], py[ed[1]], kAmber);
    }
    std::vector<const SceneNode*> chain;
    for (const SceneNode* q = n; q; q = q->parent) chain.push_back(q);
    std::reverse(chain.begin(), chain.end());
    const int shown = std::min(static_cast<int>(chain.size()), 7);
    float y = c.card("SCENE GRAPH  (root -> picked leaf)", c.ui.s(30 + 17 * shown + 16 + 18 * 5 + 40));
    for (int i = static_cast<int>(chain.size()) - shown; i < static_cast<int>(chain.size()); ++i) {
        const SceneNode* q = chain[static_cast<size_t>(i)];
        const int depth = i;
        const std::string name = q->name.empty() ? (q->object.empty() ? q->id : q->object) : q->name;
        const float x = c.cardX + c.ui.s(12 + 10 * std::min(depth, 8));
        c.ui.text(x, y, (q->isLeaf() ? "* " : "+ ") + name.substr(0, 26), c.ui.s(12), q == n ? kAmber : kText);
        c.ui.text(c.cardX + c.cardW - c.ui.s(150), y, fmt("T(%.1f, %.1f, %.1f)", q->local.translation.x, q->local.translation.y, q->local.translation.z),
                  c.ui.s(10.5f), kDim);
        y += c.ui.s(17);
    }
    y += c.ui.s(4);
    c.ui.text(c.cardX + c.ui.s(12), y, fmt("M_world of %s (column-major)", primitiveName(n->primitive)), c.ui.s(11.5f), kCyan);
    y += c.ui.s(18);
    for (int r = 0; r < 4; ++r) {
        c.ui.text(c.cardX + c.ui.s(20), y, fmt("[ %8.3f %8.3f %8.3f %8.3f ]", n->world.at(r, 0), n->world.at(r, 1), n->world.at(r, 2), n->world.at(r, 3)),
                  c.ui.s(12), kText);
        y += c.ui.s(17);
    }
    y += c.ui.s(4);
    c.ui.text(c.cardX + c.ui.s(12), y, fmt("%zu leaves in the scene, %d drawn this frame", c.m.scene->leaves().size(), c.m.leavesDrawn), c.ui.s(11), kDim);
}

// 2 ---------------------------------------------------------------------------------------------- vertex transform
void cardVertex(Ctx& c) {
    const ProbeTri& p = c.m.probe;
    if (!p.valid) return;
    const fx::VertexTrace& t = p.tr[0];
    float x, y;
    if (c.layer == 0) {
        if (c.project(c.m.birdVP, t.world, x, y)) {
            c.marker(x, y, kAmber);
            c.ui.text(x + c.ui.s(12), y - c.ui.s(20), "v0", c.ui.s(13), kAmber);
            float cx, cy;
            if (c.project(c.m.birdVP, c.s.camPos, cx, cy)) {
                c.ui.lineTop(cx, cy, x, y, alpha(kAmber, 0.6f));
                c.ui.text(cx + c.ui.s(8), cy + c.ui.s(4), "camera (eye)", c.ui.s(12), kText);
            }
        }
    } else {
        // NDC grid over the camera view
        for (int i = 0; i <= 4; ++i) {
            const float gx = c.W * i / 4.0f, gy = c.H * i / 4.0f;
            c.ui.line(gx, 0, gx, c.H, alpha(kCyan, i == 2 ? 0.45f : 0.18f));
            c.ui.line(0, gy, c.W, gy, alpha(kCyan, i == 2 ? 0.45f : 0.18f));
            c.ui.text(gx + c.ui.s(4), c.H * 0.5f + c.ui.s(4), fmt("x=%+.1f", -1.0f + i * 0.5f), c.ui.s(10.5f), alpha(kCyan, 0.8f));
            if (i != 2) c.ui.text(c.W * 0.5f + c.ui.s(4), gy + c.ui.s(2), fmt("y=%+.1f", 1.0f - i * 0.5f), c.ui.s(10.5f), alpha(kCyan, 0.8f));
        }
        probeTriangleOnImage(c, false);
        c.marker(c.sx(t.pixel.x), c.sy(t.pixel.y), kAmber);
    }
    float yy = c.card("ONE VERTEX THROUGH THE PIPELINE (v0)", c.ui.s(30 + 6 * 34 + 8));
    const struct {
        const char* stage;
        const char* op;
        std::string value;
    } rows[6] = {
        {"model", "", v3(t.model)},
        {"world", "x M (scene graph)", v3(t.world)},
        {"view", "x V (camera at 0, looks -z)", v3(t.view)},
        {"clip", "x P (projection)", fmt("(%+.2f, %+.2f, %+.2f, w %.2f)", t.clip.x, t.clip.y, t.clip.z, t.clip.w)},
        {"NDC", "/ w (perspective divide)", fmt("(%+.3f, %+.3f, %+.4f)%s", t.ndc.x, t.ndc.y, t.ndc.z, t.clipped ? " clipped" : "")},
        {"pixel", "viewport", fmt("(%.1f, %.1f)  depth %.5f", t.pixel.x, t.pixel.y, t.depth01)},
    };
    for (int i = 0; i < 6; ++i) {
        if (rows[i].op[0]) {
            c.ui.text(c.cardX + c.ui.s(30), yy - c.ui.s(2), rows[i].op, c.ui.s(10.5f), kAmber);
        }
        yy += c.ui.s(12);
        c.ui.rect(c.cardX + c.ui.s(12), yy - c.ui.s(2), c.ui.s(52), c.ui.s(18), alpha(kCyan, 0.18f));
        c.ui.text(c.cardX + c.ui.s(16), yy, rows[i].stage, c.ui.s(12), kCyan);
        c.ui.text(c.cardX + c.ui.s(72), yy, rows[i].value, c.ui.s(12), kText);
        yy += c.ui.s(22);
    }
    if (c.layer == 0) {
        // Inset: what the camera sees
        const float iw = c.cardW - c.ui.s(24), ih = iw * c.H / c.W;
        const float ix = c.cardX + c.ui.s(12), iy = c.cardY + c.ui.s(30 + 6 * 34 + 18);
        c.ui.panel(c.cardX, iy - c.ui.s(24), c.cardW, ih + c.ui.s(36), kCyan);
        c.ui.text(ix, iy - c.ui.s(18), "CAMERA IMAGE (inside the frustum)", c.ui.s(11.5f), kCyan);
        c.ui.inset(c.m.finalTex, ix, iy, iw, ih);
        c.ui.frameTop(ix, iy, iw, ih, alpha(kCyan, 0.6f));
        const float mx = ix + t.pixel.x / c.s.w * iw, my = iy + t.pixel.y / c.s.h * ih;
        c.ui.ring(mx, my, c.ui.s(5), kAmber, true);
    }
}

// 3 ---------------------------------------------------------------------------------------------- assembly
void cardAssembly(Ctx& c) {
    probeTriangleOnImage(c, true);
    const ProbeTri& p = c.m.probe;
    float y = c.card("PRIMITIVE ASSEMBLY", c.ui.s(30 + 16 * 7 + 12));
    const int n = std::max(c.m.trisInView, 1);
    c.row(y, "triangles", fmt("%d in the drawn leaves", c.m.trisInView));
    c.row(y, "front (CCW)", fmt("%d  (%.0f%%)", c.m.trisFront, 100.0f * c.m.trisFront / n), kGreen);
    c.row(y, "back (CW)", fmt("%d  (%.0f%%) culled", c.m.trisBack, 100.0f * c.m.trisBack / n), kRed);
    if (p.valid) {
        const float a2 = fx::signedArea2(p.tr[0].pixel, p.tr[1].pixel, p.tr[2].pixel);
        // pixel y points down: flip the sign so > 0 means counter-clockwise on screen as OpenGL sees it
        c.row(y, "picked tri", fmt("#%d of leaf %d", p.tri, p.leaf));
        c.row(y, "signed area", fmt("%+.1f px^2 -> %s", -0.5f * a2, -a2 > 0 ? "CCW: front, kept" : "CW: back, culled"), -a2 > 0 ? kGreen : kRed);
    }
    c.row(y, "index buffer", "3 indices per triangle (EBO)");
}

// 4 ---------------------------------------------------------------------------------------------- rasterisation
void cardRaster(Ctx& c) {
    const ProbeTri& p = c.m.probe;
    if (!p.valid) return;
    const int cols = RasterGrid::cols, rows = RasterGrid::rows;
    const Vec2f v[3] = {p.tr[0].pixel, p.tr[1].pixel, p.tr[2].pixel};
    const RasterGrid G = rasterGrid(p);
    const int kpx = G.k;
    const float ox = G.ox, oy = G.oy;
    const float gw = c.cardW - c.ui.s(24);
    const float cs = gw / cols, gh = cs * rows;
    const float gx = c.cardX + c.ui.s(12), gy = c.cardY + c.ui.s(30);
    const float hCard = c.ui.s(30) + gh + c.ui.s(12 + 16 * 7 + 8);
    c.card(fmt("PIXEL GRID  (1 cell = %d x %d px)", kpx, kpx), hCard);
    // Source rectangle on the image + callout lines
    const float rx = c.sx(ox), ry = c.sy(oy), rw = c.sx(ox + cols * kpx) - rx, rh = c.sy(oy + rows * kpx) - ry;
    c.ui.frameTop(rx, ry, rw, rh, alpha(kCyan, 0.9f));
    for (int i = 0; i < 3; ++i) c.ui.lineTop(c.sx(v[i].x), c.sy(v[i].y), c.sx(v[(i + 1) % 3].x), c.sy(v[(i + 1) % 3].y), {1, 1, 1, 0.9f});
    c.ui.lineTop(rx + rw, ry, gx, gy, alpha(kCyan, 0.35f));
    c.ui.lineTop(rx + rw, ry + rh, gx, gy + gh, alpha(kCyan, 0.35f));
    // The same scanline as the image: a cell is done once the scanline has passed its centre; in the cell row the
    // scanline is crossing, the cells fill left to right.
    const float scan = scanRow(c.anim, oy, oy + rows * kpx, static_cast<float>(c.s.h));
    const int total = cols * rows;
    int done = 0;
    for (int j = 0; j < rows; ++j) {
        const float top = oy + j * kpx, f = clampf((scan - top) / kpx, 0.0f, 1.0f);
        done = j * cols + static_cast<int>(f * cols);
        if (f < 1.0f) break;
        done = (j + 1) * cols;
    }
    int cur = std::min(done, total - 1);
    fx::Barycentric curB;
    Vec2f curP;
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            const int idx = j * cols + i;
            const Vec2f pc{ox + (i + 0.5f) * kpx, oy + (j + 0.5f) * kpx};
            const fx::Barycentric b = fx::rasterize(v[0], v[1], v[2], pc);
            if (idx == cur) {
                curB = b;
                curP = pc;
            }
            const float cx = gx + i * cs, cy = gy + j * cs;
            Rgba col{0.07f, 0.08f, 0.11f, 1.0f};
            if (idx < done) col = b.inside ? Rgba{0.15f + 0.85f * b.w0, 0.15f + 0.85f * b.w1, 0.15f + 0.85f * b.w2, 1.0f}
                                           : Rgba{0.13f, 0.15f, 0.20f, 1.0f};
            c.ui.rect(cx + 1, cy + 1, cs - 2, cs - 2, col);
            if (idx < done && b.inside) c.ui.dot(cx + cs * 0.5f, cy + cs * 0.5f, c.ui.s(1.2f), {0, 0, 0, 0.6f});
        }
    // current cell + scanline row
    if (done > 0 && done < total) {
        const int ci = cur % cols, cj = cur / cols;
        c.ui.frameTop(gx, gy + cj * cs, cols * cs, cs, alpha(kCyan, 0.5f));
        c.ui.frameTop(gx + ci * cs, gy + cj * cs, cs, cs, {1, 1, 1, 1});
        const float sxp = c.sx(curP.x), syp = c.sy(curP.y);
        c.ui.ring(sxp, syp, c.ui.s(3), {1, 1, 1, 1}, true);
    }
    // triangle edges in grid space
    float tx[3], ty[3];
    for (int i = 0; i < 3; ++i) {
        tx[i] = gx + (v[i].x - ox) / kpx * cs;
        ty[i] = gy + (v[i].y - oy) / kpx * cs;
    }
    const Rgba vc[3] = {{1, 0.35f, 0.3f, 1}, {0.4f, 1, 0.45f, 1}, {0.4f, 0.6f, 1, 1}};
    for (int i = 0; i < 3; ++i) {
        c.ui.lineTop(tx[i], ty[i], tx[(i + 1) % 3], ty[(i + 1) % 3], {1, 1, 1, 0.95f});
        c.ui.text(tx[i] + c.ui.s(4), ty[i] - c.ui.s(15), fmt("v%d", i), c.ui.s(12), vc[i]);
    }
    // the probe pixel: its cell gets an amber ring, its numbers stay on the card
    const Vec2f pp{c.px + 0.5f, c.py + 0.5f};
    const fx::Barycentric pb = fx::rasterize(v[0], v[1], v[2], pp);
    {
        const int pi = static_cast<int>((pp.x - ox) / kpx), pj = static_cast<int>((pp.y - oy) / kpx);
        if (pi >= 0 && pi < cols && pj >= 0 && pj < rows) c.ui.ring(gx + (pi + 0.5f) * cs, gy + (pj + 0.5f) * cs, cs * 0.42f, kAmber, true);
        c.marker(c.sx(pp.x), c.sy(pp.y), kAmber);
    }
    float y = gy + gh + c.ui.s(12);
    c.row(y, "probe pixel", fmt("(%d, %d) %s  w %.2f %.2f %.2f", c.px, c.py, pb.inside ? "covered" : "outside", pb.w0, pb.w1, pb.w2),
          pb.inside ? kAmber : kRed);
    const bool fin = done >= total, before = done == 0;
    c.row(y, "scan cell", fin ? std::string("grid finished - R restarts") : before ? std::string("scanline on its way...")
                                                                                  : fmt("centre (%.1f, %.1f)", curP.x, curP.y));
    if (!fin && !before) {
        c.row(y, "edge fns", fmt("E12 %+.0f  E20 %+.0f  E01 %+.0f", curB.e0, curB.e1, curB.e2));
        c.row(y, "covered", curB.inside ? "yes: all three agree in sign" : "no: outside an edge", curB.inside ? kGreen : kRed);
        c.row(y, "barycentric", fmt("w0 %.2f  w1 %.2f  w2 %.2f", curB.w0, curB.w1, curB.w2));
        if (curB.inside) {
            const float z = fx::interpolatePerspective(curB, p.tr[0].clip.w, p.tr[1].clip.w, p.tr[2].clip.w, p.tr[0].view.z,
                                                       p.tr[1].view.z, p.tr[2].view.z);
            c.row(y, "depth here", fmt("%.2f m (perspective-correct)", -z));
        }
    }
    int covered = 0;
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i)
            if (fx::rasterize(v[0], v[1], v[2], {ox + (i + 0.5f) * kpx, oy + (j + 0.5f) * kpx}).inside) ++covered;
    c.row(y, "coverage", fmt("%d of %d cells (2A = %.0f px^2)", covered, total, std::fabs(fx::signedArea2(v[0], v[1], v[2]))));
}

// 5 ---------------------------------------------------------------------------------------------- interpolation
void cardInterp(Ctx& c) {
    const ProbeTri& p = c.m.interpTri;
    float y = c.card("AFFINE vs PERSPECTIVE AT THE PROBE", c.ui.s(30 + 16 * 5 + 8));
    if (p.valid) {
        for (int i = 0; i < 3; ++i)
            c.ui.lineTop(c.sx(p.tr[i].pixel.x), c.sy(p.tr[i].pixel.y), c.sx(p.tr[(i + 1) % 3].pixel.x), c.sy(p.tr[(i + 1) % 3].pixel.y), kAmber);
        const Vec2f pc{c.px + 0.5f, c.py + 0.5f};
        const fx::Barycentric b = fx::rasterize(p.tr[0].pixel, p.tr[1].pixel, p.tr[2].pixel, pc);
        const float a[3] = {p.world[0].x, p.world[1].x, p.world[2].x};
        const float aff = fx::interpolateAffine(b, a[0], a[1], a[2]);
        const float per = fx::interpolatePerspective(b, p.tr[0].clip.w, p.tr[1].clip.w, p.tr[2].clip.w, a[0], a[1], a[2]);
        c.row(y, "screen w_i", fmt("%.3f  %.3f  %.3f", b.w0, b.w1, b.w2));
        c.row(y, "clip w_i", fmt("%.2f  %.2f  %.2f", p.tr[0].clip.w, p.tr[1].clip.w, p.tr[2].clip.w));
        c.row(y, "world x", fmt("affine %.2f, true %.2f m", aff, per));
        c.row(y, "error", fmt("%.2f m = %.1f checker squares", std::fabs(aff - per), std::fabs(aff - per)), std::fabs(aff - per) > 0.5f ? kRed : kText);
        c.marker(c.sx(pc.x), c.sy(pc.y), kAmber);
    }
    // The probe triangle rasterised both ways on the CPU
    float top = c.cardY + c.ui.s(30 + 16 * 5 + 8 + 10);
    const float tw = (c.cardW - c.ui.s(36)) * 0.5f, th = tw * 160.0f / 240.0f;
    if (c.m.triAffine) {
        c.ui.panel(c.cardX, top, c.cardW, th + c.ui.s(50), kAmber);
        c.ui.text(c.cardX + c.ui.s(12), top + c.ui.s(8), fmt("THIS TRIANGLE, %.2f m CHECKER", c.m.insetCell), c.ui.s(12), kAmber);
        const float iy = top + c.ui.s(28);
        c.ui.inset(c.m.triAffine, c.cardX + c.ui.s(12), iy, tw, th);
        c.ui.inset(c.m.triPersp, c.cardX + c.ui.s(24) + tw, iy, tw, th);
        c.ui.text(c.cardX + c.ui.s(12), iy + th + c.ui.s(4), "affine: squares bend", c.ui.s(11), kRed);
        c.ui.text(c.cardX + c.ui.s(24) + tw, iy + th + c.ui.s(4), "perspective: straight", c.ui.s(11), kGreen);
        top += th + c.ui.s(62);
    }
    // Coarse sphere close-up: Gouraud vs Phong
    const float sz = std::min((c.cardW - c.ui.s(36)) * 0.5f, c.cardBottom - top - c.ui.s(56));
    if (sz < c.ui.s(60)) return;
    c.ui.panel(c.cardX, top, c.cardW, sz + c.ui.s(50), kCyan);
    c.ui.text(c.cardX + c.ui.s(12), top + c.ui.s(8), "SPHERE 10 x 6: GOURAUD vs PHONG", c.ui.s(12), kCyan);
    const float iy = top + c.ui.s(28);
    const float x1 = c.cardX + (c.cardW - 2 * sz - c.ui.s(12)) * 0.5f;
    c.ui.inset(c.m.sphereGouraud, x1, iy, sz, sz);
    c.ui.inset(c.m.spherePhong, x1 + sz + c.ui.s(12), iy, sz, sz);
    c.ui.text(x1, iy + sz + c.ui.s(4), "per vertex: faceted", c.ui.s(11), kDim);
    c.ui.text(x1 + sz + c.ui.s(12), iy + sz + c.ui.s(4), "per pixel: smooth", c.ui.s(11), kDim);
}

// 6 ---------------------------------------------------------------------------------------------- depth
void cardDepth(Ctx& c) {
    // legend
    const float lx = c.ui.s(24), ly = c.H - c.ui.s(196), lw = c.ui.s(300), lh = c.ui.s(12);
    c.ui.panel(lx - c.ui.s(10), ly - c.ui.s(30), lw + c.ui.s(20), c.ui.s(62), kCyan);
    const bool od = c.layer == 2;
    const bool raw = c.layer == 1;
    for (int i = 0; i < 60; ++i) {
        const float t = i / 59.0f;
        const Vec3 col = raw ? Vec3{t, t, t} : od ? ramp(t) : ramp(1.0f - t);
        c.ui.rect(lx + lw * i / 60.0f, ly, lw / 60.0f + 1, lh, rgba(col));
    }
    if (od) {
        c.ui.text(lx, ly - c.ui.s(22), "FRAGMENTS PER PIXEL", c.ui.s(11), kCyan);
        c.ui.text(lx, ly + lh + c.ui.s(3), "1", c.ui.s(11), kText);
        c.ui.text(lx + lw - c.ui.s(24), ly + lh + c.ui.s(3), "10+", c.ui.s(11), kText);
        c.ui.text(lx + lw * 0.3f, ly + lh + c.ui.s(3), fmt("mean %.2f  max %.0f", c.m.overdrawMean, c.m.overdrawMax), c.ui.s(11), kText);
    } else if (raw) {
        c.ui.text(lx, ly - c.ui.s(22), "STORED DEPTH d (window z)", c.ui.s(11), kCyan);
        c.ui.text(lx, ly + lh + c.ui.s(3), "0.90", c.ui.s(11), kText);
        c.ui.text(lx + lw - c.ui.s(30), ly + lh + c.ui.s(3), "1.00", c.ui.s(11), kText);
    } else {
        c.ui.text(lx, ly - c.ui.s(22), "DISTANCE (log scale)", c.ui.s(11), kCyan);
        c.ui.text(lx, ly + lh + c.ui.s(3), fmt("%.1f m", c.m.depthNear), c.ui.s(11), kText);
        c.ui.text(lx + lw - c.ui.s(52), ly + lh + c.ui.s(3), fmt("%.0f m", c.m.depthFar), c.ui.s(11), kText);
    }
    float y = c.card("Z-BUFFER", c.ui.s(30 + 16 * 8 + 12));
    const float n = c.s.nearZ, f = c.s.farZ;
    c.row(y, "near / far", fmt("%.2f m / %.0f m", n, f));
    if (const float* d = c.s.depth.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h)) {
        c.row(y, "probe d", d[0] >= 1.0f ? std::string("1.0 (sky)") : fmt("%.6f -> %.2f m", d[0], fx::linearizeDepth(d[0], n, f)));
        c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    }
    c.ui.text(c.cardX + c.ui.s(12), y, "24-bit resolution dz = z^2 (f-n) / (f n 2^24)", c.ui.s(11), kCyan);
    y += c.ui.s(18);
    for (float z : {1.0f, 10.0f, 100.0f}) {
        const float dz = z * z * (f - n) / (f * n) / 16777216.0f;
        c.row(y, fmt("at %.0f m", z), dz < 1e-3f ? fmt("%.1f um", dz * 1e6f) : dz < 0.01f ? fmt("%.2f mm", dz * 1000.0f) : fmt("%.1f cm", dz * 100.0f));
    }
    c.row(y, "overdraw", fmt("mean %.2f, max %.0f fragments", c.m.overdrawMean, c.m.overdrawMax));
}

// 7 ---------------------------------------------------------------------------------------------- G-buffer
void cardGBuffer(Ctx& c) {
    if (c.mosaic) {
        const char* names[4] = {"ALBEDO (base colour)", "WORLD NORMAL  n*0.5+0.5", "ROUGHNESS (R) / METALNESS (G)", "DEPTH (near = white)"};
        for (int i = 0; i < 4; ++i) {
            const float x = (i % 2) * c.W * 0.5f + c.ui.s(14), y = (i / 2) * c.H * 0.5f + c.ui.s(i < 2 ? 96 : 10);
            c.ui.rect(x - c.ui.s(6), y - c.ui.s(4), c.ui.tw(names[i], c.ui.s(14)) + c.ui.s(12), c.ui.s(22), kPanel);
            c.ui.text(x, y, names[i], c.ui.s(14), kAmber);
        }
    }
    const float mb = c.s.w * c.s.h * (4 + 4 + 4 + 3) / 1048576.0f;
    float y = c.card("BYTES PER PIXEL", c.ui.s(30 + 16 * 5 + 10), kCyan);
    c.row(y, "albedo", "RGBA8   4 B");
    c.row(y, "normal + refl", "RGBA8   4 B (surface target)");
    c.row(y, "rough / metal", "RGBA8   4 B (explorer pass)");
    c.row(y, "depth", "24-bit  3 B");
    c.row(y, "total", fmt("15 B x %d x %d = %.1f MB", c.s.w, c.s.h, mb));
}

// 8 ---------------------------------------------------------------------------------------------- shadow map
void cardShadow(Ctx& c) {
    const Vec3 P = c.worldAt(c.px, c.py);
    const float dist = length(P - c.s.camPos);
    const int layer = dist < c.s.cascadeSplit ? 0 : 1;
    const Vec4 lp = c.s.moonViewProj[layer] * Vec4{P.x, P.y, P.z, 1.0f};
    const Vec3 l{lp.x / lp.w * 0.5f + 0.5f, lp.y / lp.w * 0.5f + 0.5f, lp.z / lp.w * 0.5f + 0.5f};
    c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    const float* lt = c.s.light.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    float y = c.card("SHADOW TEST AT THE PROBE", c.ui.s(30 + 16 * 6 + 10));
    c.row(y, "key light", c.m.keyIsSun ? "sun (directional, orthographic map)" : "moon (directional, orthographic map)");
    c.row(y, "map", fmt("%d x %d, 2 cascades, split %.0f m", c.s.moonShadowRes, c.s.moonShadowRes, c.s.cascadeSplit));
    c.row(y, "cascade", fmt("%d (probe %.1f m away)", layer, dist));
    c.row(y, "light uv, z", fmt("(%.3f, %.3f)  z %.4f", l.x, l.y, l.z));
    c.row(y, "PCF 4x4", lt ? fmt("lit fraction %.2f", lt[0]) : std::string("-"), lt && lt[0] > 0.5f ? kGreen : kRed);
    c.row(y, "bias", "normal offset + 0.0004 depth");
    // Inset: the light's view (cascade 0 image) with the probe, and the PCF tap pattern
    if (c.m.shadowMapImg.valid() && c.layer != 1) {
        const float top = c.cardY + c.ui.s(30 + 16 * 6 + 22);
        const float sz = c.cardW - c.ui.s(24) - c.ui.s(100);
        c.ui.panel(c.cardX, top, c.cardW, sz + c.ui.s(40), kCyan);
        c.ui.text(c.cardX + c.ui.s(12), top + c.ui.s(8), "THE LIGHT'S VIEW (cascade 0)", c.ui.s(11.5f), kCyan);
        const GLuint tex = c.m.layers[7].size() > 1 ? c.m.layers[7][1].a : 0;
        const float ix = c.cardX + c.ui.s(12), iy = top + c.ui.s(28);
        c.ui.inset(tex, ix, iy, sz, sz);
        c.ui.frameTop(ix, iy, sz, sz, alpha(kCyan, 0.5f));
        if (layer == 0 && l.x >= 0 && l.x <= 1 && l.y >= 0 && l.y <= 1) c.ui.ring(ix + l.x * sz, iy + (1 - l.y) * sz, c.ui.s(5), kAmber, true);
        // PCF taps
        const float px = ix + sz + c.ui.s(14), py = iy + c.ui.s(10), cell = c.ui.s(18);
        c.ui.text(px, py - c.ui.s(4), "PCF taps", c.ui.s(10.5f), kDim);
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                const bool lit = lt && (j * 4 + i) < static_cast<int>(std::lround(lt[0] * 16));
                c.ui.rect(px + i * cell, py + c.ui.s(14) + j * cell, cell - 2, cell - 2, lit ? alpha(kGreen, 0.8f) : alpha(kRed, 0.7f));
            }
    }
}

// 9 ---------------------------------------------------------------------------------------------- lighting
void cardLighting(Ctx& c) {
    if (c.mosaic) {
        const char* names[4] = {"AMBIENT  k_a * A", "DIFFUSE  k_d (N.L) * shadow", "SPECULAR  k_s (N.H)^n * shadow", "SUM"};
        for (int i = 0; i < 4; ++i) {
            const float x = (i % 2) * c.W * 0.5f + c.ui.s(14), y = (i / 2) * c.H * 0.5f + c.ui.s(i < 2 ? 96 : 10);
            c.ui.rect(x - c.ui.s(6), y - c.ui.s(4), c.ui.tw(names[i], c.ui.s(14)) + c.ui.s(12), c.ui.s(22), kPanel);
            c.ui.text(x, y, names[i], c.ui.s(14), kAmber);
        }
    }
    const Vec3 P = c.worldAt(c.px, c.py);
    const float* sf = c.s.surface.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    const Vec3 N = sf ? normalize(Vec3{sf[0] * 2 - 1, sf[1] * 2 - 1, sf[2] * 2 - 1}) : Vec3{0, 1, 0};
    const Vec3 L = normalize(-c.m.lightDir), V = normalize(c.s.camPos - P), Hh = normalize(L + V);
    float y = c.card("TERMS AT THE PROBE", c.ui.s(30 + 16 * 6 + 10 + 130));
    c.row(y, "N (normal)", v3(N));
    c.row(y, "L (to light)", v3(L));
    c.row(y, "V (to eye)", v3(V));
    c.row(y, "H = |L+V|", v3(Hh));
    c.row(y, "N.L, N.H", fmt("%.3f   %.3f", std::max(0.0f, dot(N, L)), std::max(0.0f, dot(N, Hh))));
    c.row(y, "light", fmt("%s colour x I = (%.2f %.2f %.2f)", c.m.keyIsSun ? "sun" : "moon", c.m.lightColor.x, c.m.lightColor.y, c.m.lightColor.z));
    c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    // vector diagram in the plane of N and L
    const float ox = c.cardX + c.cardW * 0.5f, oy = y + c.ui.s(108), len = c.ui.s(90);
    c.ui.line(ox - c.ui.s(130), oy, ox + c.ui.s(130), oy, kDim);
    for (int i = 0; i < 12; ++i) c.ui.line(ox - c.ui.s(130) + i * c.ui.s(22), oy, ox - c.ui.s(140) + i * c.ui.s(22), oy + c.ui.s(10), alpha(kDim, 0.5f));
    Vec3 T = L - N * dot(N, L);
    T = length(T) > 1e-3f ? normalize(T) : Vec3{1, 0, 0};
    const auto vec = [&](const Vec3& d, Rgba col, const char* name) {
        const float a = dot(d, T), b = dot(d, N);
        const float x = ox - a * len, yy = oy - b * len;  // the light on the left
        c.ui.arrow(ox, oy, x, yy, col, 9.0f);
        c.ui.text(x + c.ui.s(4), yy - c.ui.s(14), name, c.ui.s(12), col);
    };
    vec(N, kText, "N");
    vec(L, kAmber, "L");
    vec(normalize(V - N * dot(N, V) + N * dot(N, V)), kCyan, "V");
    vec(Hh, kGreen, "H");
}

// 10 --------------------------------------------------------------------------------------------- reflection
void cardReflection(Ctx& c) {
    const fx::SsrMarch& mr = c.m.march;
    const float x0 = c.sx(c.m.ssrX + 0.5f), y0 = c.sy(c.m.ssrY + 0.5f);
    // the march, step by step
    const int shown = std::min(static_cast<int>(mr.steps.size()), static_cast<int>(std::fmod(c.anim, 10.0f) * 9.0f));
    float px = x0, py = y0;
    for (int i = 0; i < shown; ++i) {
        const fx::SsrStep& st = mr.steps[static_cast<size_t>(i)];
        const float x = st.uv.x * c.W, y = (1.0f - st.uv.y) * c.H;
        c.ui.lineTop(px, py, x, y, alpha(kCyan, 0.75f));
        c.ui.rect(x - c.ui.s(2.5f), y - c.ui.s(2.5f), c.ui.s(5), c.ui.s(5), st.behind ? kRed : kCyan);
        px = x;
        py = y;
    }
    const bool hitShown = mr.hit && shown >= static_cast<int>(mr.steps.size());
    if (hitShown) {
        const float hx = mr.hitUV.x * c.W, hy = (1.0f - mr.hitUV.y) * c.H;
        c.ui.ring(hx, hy, c.ui.s(9), kAmber, true);
        c.ui.ring(hx, hy, c.ui.s(13), alpha(kAmber, 0.5f), true);
        c.ui.text(hx + c.ui.s(14), hy - c.ui.s(8), "hit: reuse this colour", c.ui.s(12), kAmber);
    }
    // the normal at the reflecting pixel
    float nx, ny;
    if (c.project(c.s.viewProj, mr.origin + mr.normal * 0.6f, nx, ny)) c.ui.arrow(x0, y0, nx, ny, kGreen, 9.0f, true);
    c.marker(x0, y0, kAmber);
    // mirror-law diagram
    const float cosI = clampf(-dot(mr.view, mr.normal), -1.0f, 1.0f);
    const float thetaDeg = degrees(std::acos(std::fabs(cosI)));
    float y = c.card("MIRROR LAW: SSR MARCH vs RAY TRACED", c.ui.s(30 + 16 * 12 + 150));
    c.row(y, "D (view ray)", v3(mr.view));
    c.row(y, "N (normal)", v3(mr.normal));
    c.row(y, "R = D-2(D.N)N", v3(mr.reflected), kCyan);
    c.row(y, "angles", fmt("in %.1f deg = out %.1f deg", thetaDeg, thetaDeg));
    c.row(y, "Fresnel", fmt("F0 0.02 -> F = %.3f (Schlick)", fx::schlickFresnel(0.02f, std::fabs(cosI))));
    c.row(y, "march", fmt("%zu steps", mr.steps.size()), mr.hit ? kGreen : kRed);
    c.row(y, "result", mr.stop, mr.hit ? kGreen : kRed);
    // WP-H: the exact mirror ray through the BVH, next to the screen-space estimate
    c.row(y, "SSR hit", c.m.ssrHitDist >= 0.0f ? fmt("%.2f m along R (screen space)", c.m.ssrHitDist) : std::string("none"), kCyan);
    if (c.m.rtValid) {
        const rt::ReflectionSample& rr = c.m.rtRefl;
        const std::string obj = rr.reflectedHit ? rr.reflectedObject.substr(0, 20) : std::string("sky");
        c.row(y, "RT hit (BVH)", rr.reflectedHit ? fmt("%s at %.2f m", obj.c_str(), length(rr.reflectedPoint - rr.point)) : std::string("sky: nothing hit"),
              {1.0f, 0.45f, 0.95f, 1.0f});
        c.row(y, "RT N (exact)", v3(rr.normal));
        c.row(y, "RT R, F", fmt("(%+.2f, %+.2f, %+.2f)  F %.3f", rr.reflectDir.x, rr.reflectDir.y, rr.reflectDir.z, rr.fresnel));
        float ax, ay, bx, by;  // the traced reflection ray on the image
        if (c.project(c.s.viewProj, rr.point, ax, ay) && c.project(c.s.viewProj, rr.reflectedPoint, bx, by)) {
            for (int k = 0; k < 24; k += 2)  // dashed
                c.ui.lineTop(ax + (bx - ax) * k / 24.0f, ay + (by - ay) * k / 24.0f, ax + (bx - ax) * (k + 1) / 24.0f, ay + (by - ay) * (k + 1) / 24.0f,
                             {1.0f, 0.35f, 0.95f, 1.0f});
            if (rr.reflectedHit) c.ui.ring(bx, by, c.ui.s(6), {1.0f, 0.35f, 0.95f, 1.0f}, true);
            c.ui.text(bx + c.ui.s(8), by + c.ui.s(4), "RT ray", c.ui.s(11), {1.0f, 0.35f, 0.95f, 1.0f});
        }
    } else {
        c.row(y, "RT hit (BVH)", "ray tracer not available", kDim);
    }
    c.row(y, "step rule", "t0 = 0.04 m, t' = 1.09 t + 0.03 m");
    const float ox = c.cardX + c.cardW * 0.5f, oy = y + c.ui.s(118), len = c.ui.s(105);
    c.ui.rect(c.cardX + c.ui.s(20), oy, c.cardW - c.ui.s(40), c.ui.s(8), alpha(kCyan, 0.25f));
    c.ui.line(c.cardX + c.ui.s(20), oy, c.cardX + c.cardW - c.ui.s(20), oy, kCyan);
    const float th = radians(thetaDeg);
    c.ui.line(ox, oy, ox, oy - len, alpha(kText, 0.5f));
    c.ui.text(ox + c.ui.s(4), oy - len - c.ui.s(4), "N", c.ui.s(12), kText);
    c.ui.arrow(ox - std::sin(th) * len, oy - std::cos(th) * len, ox, oy, kAmber, 10.0f);
    c.ui.arrow(ox, oy, ox + std::sin(th) * len, oy - std::cos(th) * len, kGreen, 10.0f);
    c.ui.text(ox - std::sin(th) * len - c.ui.s(10), oy - std::cos(th) * len - c.ui.s(18), "D", c.ui.s(12), kAmber);
    c.ui.text(ox + std::sin(th) * len + c.ui.s(4), oy - std::cos(th) * len - c.ui.s(18), "R", c.ui.s(12), kGreen);
    for (int i = 1; i < 10; ++i) {  // the two equal angles
        const float a0 = th * (i - 1) / 9, a1 = th * i / 9, r = len * 0.32f;
        c.ui.line(ox - std::sin(a0) * r, oy - std::cos(a0) * r, ox - std::sin(a1) * r, oy - std::cos(a1) * r, kAmber);
        c.ui.line(ox + std::sin(a0) * r, oy - std::cos(a0) * r, ox + std::sin(a1) * r, oy - std::cos(a1) * r, kGreen);
    }
    if (c.m.rtValid) {  // the traced mirror direction, at its own angle to the traced normal (dashed magenta)
        const float rth = std::acos(clampf(dot(c.m.rtRefl.reflectDir, c.m.rtRefl.normal), -1.0f, 1.0f));
        const float ex = ox + std::sin(rth) * len * 0.92f, ey = oy - std::cos(rth) * len * 0.92f;
        for (int k = 0; k < 12; k += 2)
            c.ui.lineTop(ox + (ex - ox) * k / 12.0f, oy + (ey - oy) * k / 12.0f, ox + (ex - ox) * (k + 1) / 12.0f, oy + (ey - oy) * (k + 1) / 12.0f,
                         {1.0f, 0.35f, 0.95f, 1.0f});
        c.ui.text(ex + c.ui.s(4), ey + c.ui.s(4), "RT (exact normal)", c.ui.s(11), {1.0f, 0.35f, 0.95f, 1.0f});
    }
    c.ui.text(ox - c.ui.s(70), oy + c.ui.s(12), fmt("theta_i = theta_r = %.0f deg", thetaDeg), c.ui.s(11), kDim);
}

// 11 --------------------------------------------------------------------------------------------- SSAO
void cardSsao(Ctx& c) {
    const float* ao = c.s.ao.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    float y = c.card("HEMISPHERE SAMPLING", c.ui.s(30 + 16 * 3 + 170));
    c.row(y, "samples", fmt("%d per pixel (this preset), blurred", c.m.quality.ssaoSamples));
    c.row(y, "probe AO", ao ? fmt("%.3f of the sky visible", ao[0]) : std::string("- (off)"));
    c.row(y, "resolution", c.s.ao.valid() ? fmt("%d x %d", c.s.ao.w, c.s.ao.h) : std::string("-"));
    // diagram: a crease (floor + wall); samples behind the depth buffer are occluded
    const float ox = c.cardX + c.cardW * 0.42f, oy = y + c.ui.s(120), r = c.ui.s(95);
    const float wallX = ox + r * 0.45f;
    c.ui.rect(c.cardX + c.ui.s(16), oy, c.cardW - c.ui.s(32), c.ui.s(10), alpha(kDim, 0.35f));
    c.ui.rect(wallX, oy - r * 1.05f, c.ui.s(22), r * 1.05f, alpha(kDim, 0.35f));
    for (int i = 0; i <= 24; ++i) {
        const float a0 = PI * i / 24, a1 = PI * (i + 1) / 24;
        if (i < 24) c.ui.line(ox + std::cos(a0) * r, oy - std::sin(a0) * r, ox + std::cos(a1) * r, oy - std::sin(a1) * r, alpha(kCyan, 0.5f));
    }
    c.ui.arrow(ox, oy, ox, oy - r * 0.6f, kText, 8.0f);
    int occ = 0;
    const int n = 14;
    for (int i = 0; i < n; ++i) {
        const float a = PI * (fx::halton(static_cast<unsigned>(i + 1), 2) * 0.92f + 0.04f);
        const float rr = r * (0.25f + 0.75f * fx::halton(static_cast<unsigned>(i + 1), 3));
        const float x = ox + std::cos(a) * rr, yy = oy - std::sin(a) * rr;
        const bool o = x >= wallX;
        occ += o;
        c.ui.dot(x, yy, c.ui.s(3.5f), o ? kRed : kGreen);
        c.ui.line(ox, oy, x, yy, alpha(o ? kRed : kGreen, 0.25f));
    }
    c.ui.text(c.cardX + c.ui.s(16), oy + c.ui.s(16), fmt("%d of %d samples inside geometry -> AO = %.2f", occ, n, 1.0f - occ / float(n)), c.ui.s(11), kDim);
}

// 12 --------------------------------------------------------------------------------------------- fog
void cardFog(Ctx& c) {
    const float* fg = c.s.fog.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    const float* d = c.s.depth.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    const float dist = d && d[0] < 1.0f ? fx::linearizeDepth(d[0], c.s.nearZ, c.s.farZ) : 0.0f;
    c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    float y = c.card("TRANSMITTANCE ALONG THE RAY", c.ui.s(30 + 16 * 3 + 160));
    c.row(y, "probe distance", fmt("%.1f m", dist));
    c.row(y, "T at probe", fg ? fmt("%.3f (in-scatter L %.4f)", fg[3], lum3(fg)) : std::string("- (fog off)"));
    const float sigma = fg && fg[3] > 0.0f && fg[3] < 1.0f && dist > 0.1f ? -std::log(fg[3]) / dist : 0.02f;
    c.row(y, "effective sigma", fmt("%.4f / m", sigma));
    // plot T(d) = exp(-sigma d) for d = 0..100 m
    const float gx = c.cardX + c.ui.s(36), gy = y + c.ui.s(8), gw = c.cardW - c.ui.s(56), gh = c.ui.s(120);
    c.ui.rect(gx, gy, gw, gh, {1, 1, 1, 0.04f});
    c.ui.line(gx, gy + gh, gx + gw, gy + gh, kDim);
    c.ui.line(gx, gy, gx, gy + gh, kDim);
    const float dmax = std::max(60.0f, dist * 1.5f);
    float lx = gx, ly = gy;
    for (int i = 1; i <= 50; ++i) {
        const float dd = dmax * i / 50.0f, T = std::exp(-sigma * dd);
        const float x = gx + gw * i / 50.0f, yy = gy + gh * (1.0f - T);
        c.ui.line(lx, ly, x, yy, kCyan);
        lx = x;
        ly = yy;
    }
    if (dist > 0.0f) {
        const float x = gx + gw * dist / dmax, yy = gy + gh * (1.0f - std::exp(-sigma * dist));
        c.ui.line(x, gy + gh, x, yy, alpha(kAmber, 0.6f));
        c.ui.dot(x, yy, c.ui.s(3.5f), kAmber);
    }
    c.ui.text(gx - c.ui.s(26), gy - c.ui.s(4), "T=1", c.ui.s(10), kDim);
    c.ui.text(gx + gw - c.ui.s(50), gy + gh + c.ui.s(4), fmt("%.0f m", dmax), c.ui.s(10), kDim);
}

// 13 --------------------------------------------------------------------------------------------- bloom
void cardBloom(Ctx& c) {
    if (c.layer == 1) {
        for (size_t k = 0; k < c.m.bloomRects.size(); ++k) {
            const auto& r = c.m.bloomRects[k];
            c.ui.frameTop(r.x * c.W, r.y * c.H, r.w * c.W, r.h * c.H, alpha(kAmber, 0.6f));
            const int lw = std::max(1, c.s.w >> (k + 1)), lh = std::max(1, c.s.h >> (k + 1));
            c.ui.text(r.x * c.W, r.y * c.H - c.ui.s(16), k == 0 ? fmt("level 0: threshold, %dx%d", lw, lh) : fmt("%d: %dx%d", static_cast<int>(k), lw, lh),
                      c.ui.s(k < 3 ? 12.0f : 9.5f), kAmber);
        }
    }
    float y = c.card("FILTER KERNELS", c.ui.s(30 + 16 * 2 + 150));
    c.row(y, "threshold T", fmt("%.2f (follows the exposure key)", c.m.bloomThreshold));
    c.row(y, "levels", fmt("%d mips, then tent upsample + add", std::max(c.s.bloomMips, static_cast<int>(c.m.bloomRects.size()))));
    // 13-tap pattern on a 5x5 texel grid
    const float cell = c.ui.s(22), gx = c.cardX + c.ui.s(20), gy = y + c.ui.s(18);
    c.ui.text(gx, gy - c.ui.s(16), "13-tap down (x 1/32)", c.ui.s(10.5f), kDim);
    const int wts[5][5] = {{1, 0, 2, 0, 1}, {0, 4, 0, 4, 0}, {2, 0, 4, 0, 2}, {0, 4, 0, 4, 0}, {1, 0, 2, 0, 1}};
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5; ++i) {
            const int wv = wts[j][i];
            c.ui.rect(gx + i * cell, gy + j * cell, cell - 2, cell - 2, wv ? alpha(kAmber, 0.15f + 0.2f * wv) : Rgba{1, 1, 1, 0.05f});
            if (wv) c.ui.text(gx + i * cell + c.ui.s(6), gy + j * cell + c.ui.s(4), std::to_string(wv), c.ui.s(11), kText);
        }
    const float tx = gx + 5 * cell + c.ui.s(36);
    c.ui.text(tx, gy - c.ui.s(16), "3x3 tent up (x 1/16)", c.ui.s(10.5f), kDim);
    const int tent[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            c.ui.rect(tx + i * cell * 1.3f, gy + j * cell * 1.3f, cell * 1.3f - 2, cell * 1.3f - 2, alpha(kCyan, 0.12f + 0.15f * tent[j][i]));
            c.ui.text(tx + i * cell * 1.3f + c.ui.s(9), gy + j * cell * 1.3f + c.ui.s(7), std::to_string(tent[j][i]), c.ui.s(12), kText);
        }
}

// 14 --------------------------------------------------------------------------------------------- tone mapping
void cardTone(Ctx& c) {
    float y = c.card("TONE CURVE + LUMINANCE HISTOGRAM", c.ui.s(30 + 16 * 3 + 210));
    c.row(y, "exposure e", fmt("%.2f (key %.1f, mean log2 L %.2f)", c.m.exposure, c.m.keyLog2, c.m.meanLog));
    c.row(y, "middle grey", fmt("1.6 e L = 0.18 at L = %.4f", 0.18f / (c.m.exposure * 1.6f)));
    const float* pp = c.s.taa.at((c.px + 0.5f) / c.s.w, 1.0f - (c.py + 0.5f) / c.s.h);
    const float pl = pp ? lum3(pp) : 0.0f;
    c.row(y, "probe L", fmt("%.4f -> ACES %.2f, linear %.2f", pl, fx::toneCurve(pl, c.m.exposure), srgb(pl * c.m.exposure * 1.6f)));
    c.marker(c.sx(c.px + 0.5f), c.sy(c.py + 0.5f), kAmber);
    const float gx = c.cardX + c.ui.s(30), gy = y + c.ui.s(10), gw = c.cardW - c.ui.s(46), gh = c.ui.s(150);
    c.ui.rect(gx, gy, gw, gh, {1, 1, 1, 0.04f});
    const float lo = c.m.logHistLo, hi = c.m.logHistHi;
    int mx = 1;
    for (int v : c.m.logHist) mx = std::max(mx, v);
    for (size_t i = 0; i < c.m.logHist.size(); ++i) {
        const float hgt = gh * 0.85f * std::sqrt(static_cast<float>(c.m.logHist[i]) / mx);
        const float bw = gw / c.m.logHist.size();
        c.ui.rect(gx + i * bw, gy + gh - hgt, bw - 1, hgt, alpha(kCyan, 0.28f));
    }
    for (int st = static_cast<int>(lo); st <= static_cast<int>(hi); st += 2) {
        const float x = gx + gw * (st - lo) / (hi - lo);
        c.ui.line(x, gy + gh, x, gy + gh + c.ui.s(4), kDim);
        c.ui.text(x - c.ui.s(8), gy + gh + c.ui.s(5), std::to_string(st), c.ui.s(9.5f), kDim);
    }
    c.ui.text(gx + gw - c.ui.s(70), gy + gh + c.ui.s(17), "log2 L (stops)", c.ui.s(10), kDim);
    float plx = gx, ply = gy + gh, pax = gx, pay = gy + gh;
    for (int i = 0; i <= 80; ++i) {
        const float L2 = lo + (hi - lo) * i / 80.0f, L = std::exp2(L2);
        const float x = gx + gw * i / 80.0f;
        const float yl = gy + gh * (1.0f - srgb(std::min(1.0f, L * c.m.exposure * 1.6f)));
        const float ya = gy + gh * (1.0f - fx::toneCurve(L, c.m.exposure));
        if (i) {
            c.ui.line(plx, ply, x, yl, alpha(kText, 0.55f));
            c.ui.line(pax, pay, x, ya, kAmber);
        }
        plx = x;
        ply = yl;
        pax = x;
        pay = ya;
    }
    const float mxl = gx + gw * (c.m.meanLog - lo) / (hi - lo);
    if (mxl > gx && mxl < gx + gw) {
        c.ui.line(mxl, gy, mxl, gy + gh, alpha(kGreen, 0.6f));
        c.ui.text(mxl + c.ui.s(3), gy + c.ui.s(2), "mean", c.ui.s(10), kGreen);
    }
    if (pl > 0.0f) {
        const float x = gx + gw * (std::log2(pl) - lo) / (hi - lo);
        if (x > gx && x < gx + gw) c.ui.dot(x, gy + gh * (1.0f - fx::toneCurve(pl, c.m.exposure)), c.ui.s(4), kAmber);
    }
    c.ui.text(gx + c.ui.s(6), gy + c.ui.s(16), "ACES", c.ui.s(11), kAmber);
    c.ui.text(gx + c.ui.s(6), gy + c.ui.s(30), "linear, clipped", c.ui.s(11), alpha(kText, 0.7f));
}

// 15 --------------------------------------------------------------------------------------------- TAA
void cardTaa(Ctx& c) {
    float y = c.card("HALTON (2,3) JITTER, 8 FRAMES", c.ui.s(30 + 16 * 3 + 200));
    const int cur = static_cast<int>((c.s.frameIndex + 7) % 8) + 1;  // index used by the frozen frame
    c.row(y, "this frame", fmt("k = %d: offset (%+.3f, %+.3f) px", cur, fx::halton(cur, 2) - 0.5f, fx::halton(cur, 3) - 0.5f));
    c.row(y, "history weight", "0.9 -> ~10 frames averaged");
    c.row(y, "rejection", "neighbourhood clip in YCoCg");
    const float sz = c.ui.s(170), gx = c.cardX + (c.cardW - sz) * 0.5f, gy = y + c.ui.s(14);
    c.ui.rect(gx, gy, sz, sz, {1, 1, 1, 0.05f});
    c.ui.line(gx + sz * 0.5f, gy, gx + sz * 0.5f, gy + sz, alpha(kDim, 0.4f));
    c.ui.line(gx, gy + sz * 0.5f, gx + sz, gy + sz * 0.5f, alpha(kDim, 0.4f));
    c.ui.text(gx, gy - c.ui.s(14), "one pixel", c.ui.s(10), kDim);
    const int shown = 1 + static_cast<int>(std::fmod(c.anim * 2.0f, 9.0f));
    for (int k = 1; k <= 8; ++k) {
        const float x = gx + fx::halton(k, 2) * sz, yy = gy + (1.0f - fx::halton(k, 3)) * sz;
        if (k > shown) continue;
        c.ui.dot(x, yy, c.ui.s(k == cur ? 5.0f : 3.5f), k == cur ? kAmber : kCyan);
        c.ui.text(x + c.ui.s(6), yy - c.ui.s(6), std::to_string(k), c.ui.s(11), k == cur ? kAmber : kText);
    }
}

// 16 --------------------------------------------------------------------------------------------- final + CV
void cardFinal(Ctx& c) {
    const std::string name = c.layer < static_cast<int>(c.m.layers[15].size()) ? c.m.layers[15][static_cast<size_t>(c.layer)].name : "";
    const bool cvLayer = name.find("CV") != std::string::npos || name.find("thermal sensor") != std::string::npos;
    if (cvLayer && c.m.cvW > 0) {
        for (const Measurement2D& b : c.m.blobs) {
            const Rgba col = b.cls == BlobClass::Target ? kRed : b.cls == BlobClass::Person ? kGreen : b.cls == BlobClass::HotClutter ? kAmber : kDim;
            const float x0 = b.minX * c.W / c.m.cvW - 3, y0 = b.minY * c.H / c.m.cvH - 3;
            const float x1 = (b.maxX + 1) * c.W / c.m.cvW + 3, y1 = (b.maxY + 1) * c.H / c.m.cvH + 3;
            c.ui.frameTop(x0, y0, x1 - x0, y1 - y0, col);
            if (b.cls != BlobClass::Clutter || b.area > 20)
                c.ui.text(x0, y0 - c.ui.s(14), fmt("%s %dpx", blobClassName(b.cls), b.area), c.ui.s(11), col);
        }
    }
    float y = c.card("COMPUTER VISION ON THE HEAT IMAGE", c.ui.s(30 + 16 * 6 + (c.m.cvStats.hotPixels == 0 ? 42 : 10)), kRed);
    c.row(y, "image", fmt("%d x %d raw heat (UAV camera size)", c.m.cvW, c.m.cvH));
    c.row(y, "threshold", fmt("%d (Otsu %d)", c.m.cvStats.threshold, c.m.cvStats.otsu));
    c.row(y, "hot pixels", fmt("%d, %d speckles removed", c.m.cvStats.hotPixels, c.m.cvStats.removedSpeckles));
    c.row(y, "components", fmt("%d (8-connected)", c.m.cvStats.components));
    c.row(y, "classes", fmt("%d target, %d person", c.m.cvStats.targets, c.m.cvStats.persons), kRed);
    c.row(y, "", fmt("%d hot clutter, %d clutter", c.m.cvStats.hotClutter, c.m.cvStats.clutter));
    if (c.m.cvStats.hotPixels == 0)
        c.ui.wrap(c.cardX + c.ui.s(12), y + c.ui.s(2), c.cardW - c.ui.s(24),
                  "Nothing in view reaches the floor (heat 0.8: engines, fires): amber = what Otsu alone would split off.", c.ui.s(11), kDim);
}

}  // namespace

float FrameExplorer::rasterProbeTime() const {
    const Impl& m = *impl_;
    if (!m.probe.valid) return 4.0f;
    const RasterGrid G = rasterGrid(m.probe);
    const int* p = m.stageProbe[static_cast<int>(fx::Stage::Raster)];
    const float top = G.oy, bottom = G.oy + RasterGrid::rows * G.k;
    const int pi = static_cast<int>((p[0] + 0.5f - G.ox) / G.k), pj = static_cast<int>((p[1] + 0.5f - G.oy) / G.k);
    const float scan = G.oy + pj * G.k + (pi + 0.6f) / RasterGrid::cols * G.k;  // inside that cell row, at its column
    return 1.2f + (6.6f - 1.2f) * clampf((scan - top) / std::max(bottom - top, 1.0f), 0.0f, 1.0f);
}

// ================================================================================================ progress
// Shown while open() works: the frozen frame (once rendered) dimmed behind a panel with the step and a progress bar.
void FrameExplorer::drawProgress(Renderer& r, GLuint targetFbo, int W, int H, float fraction, const std::string& label) {
    Impl& m = *impl_;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const float fW = static_cast<float>(W), fH = static_cast<float>(H), k = fH / 720.0f;
    if (m.finalTex) m.image(m.finalTex, 0, 0, fW, fH, W, H, 0, 0, 0.5f, nullptr, 0.35f);
    Hud& hud = r.hud();
    hud.begin(W, H);
    const float pw = 560 * k, ph = 132 * k, px = (fW - pw) * 0.5f, py = (fH - ph) * 0.5f;
    hud.rect(px, py, pw, ph, kPanel);
    hud.rect(px, py, pw, 2 * k, kAmber);
    hud.text(px + 18 * k, py + 14 * k, "FRAME EXPLORER", 11 * k, kAmber);
    hud.text(px + 18 * k, py + 34 * k, "Freezing the frame...", 22 * k, kText);
    hud.text(px + 18 * k, py + 66 * k, label.substr(0, 70), 12 * k, kDim);
    const float bx = px + 18 * k, by = py + 96 * k, bw = pw - 36 * k, bh = 8 * k;
    hud.rect(bx, by, bw, bh, {1, 1, 1, 0.08f});
    hud.rect(bx, by, bw * clampf(fraction, 0.0f, 1.0f), bh, kAmber);
    // the 16 stages to come, lit as the work progresses
    const float sw = bw / fx::kStageCount;
    for (int i = 0; i < fx::kStageCount; ++i)
        hud.rect(bx + i * sw + 1, by + 14 * k, sw - 2, 3 * k, (i + 0.5f) / fx::kStageCount <= fraction ? alpha(kCyan, 0.8f) : Rgba{1, 1, 1, 0.1f});
    hud.end();
}

// ================================================================================================ draw
void FrameExplorer::draw(Renderer& r, GLuint targetFbo, int W, int H) {
    lastW_ = static_cast<float>(W);
    lastH_ = static_cast<float>(H);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    Impl& m = *impl_;
    if (!open_ || m.layers[0].empty()) return;
    Hud& hud = r.hud();
    Ui ui(hud);
    ui.k = static_cast<float>(H) / 720.0f;
    ui.W = static_cast<float>(W);
    ui.H = static_cast<float>(H);
    const float fW = static_cast<float>(W), fH = static_cast<float>(H);
    const int stage = std::max(0, std::min(fx::kStageCount - 1, view_.stage));
    const auto& layers = m.layers[stage];
    const int li = std::max(0, std::min(static_cast<int>(layers.size()) - 1, view_.layer));
    const Layer& L = layers[static_cast<size_t>(li)];
    const fx::StageInfo& info = fx::stageInfo(stage);
    const float topH = ui.s(84);
    const float capH = ui.s(118), capY = fH - capH - ui.s(10);

    // ---------------------------------------------------------------- main image
    if (view_.lab) {
        m.buildLab(snap_, view_.kernel, view_.gamma);
    } else if (trans_ < 1.0f) {
        const float e = trans_ * trans_ * (3.0f - 2.0f * trans_);
        const Layer& prev = m.layers[std::max(0, std::min(fx::kStageCount - 1, prevStage_))].front();
        const bool forward = stage >= prevStage_;
        m.image(forward ? L.a : prev.a, 0, 0, fW, fH, W, H, 1, forward ? prev.a : L.a, forward ? e : 1.0f - e);
    } else if (view_.wipe && stage > 0) {
        m.image(m.layers[stage - 1].front().a, 0, 0, fW, fH, W, H, 1, L.a, view_.wipeX);
    } else if (L.reveal) {
        const RasterGrid G = rasterGrid(m.probe);
        const float row = m.probe.valid ? scanRow(anim_, G.oy, G.oy + RasterGrid::rows * G.k, static_cast<float>(snap_.h))
                                        : clampf(std::fmod(anim_, kScanPeriod + kScanHold) / kScanPeriod, 0.0f, 1.0f) * snap_.h;
        m.image(L.a, 0, 0, fW, fH, W, H, 3, L.b, 1.0f - row / static_cast<float>(snap_.h));
    } else if (L.b) {
        m.image(L.a, 0, 0, fW, fH, W, H, 1, L.b, view_.split >= 0.0f ? view_.split : L.split);
    } else {
        m.image(L.a, 0, 0, fW, fH, W, H);
    }

    hud.begin(W, H);
    // ---------------------------------------------------------------- stage overlay + card
    const bool inspectorLeft = view_.inspector;
    const float cardW = ui.s(stage == 3 ? 400 : 372);
    const int* sp = m.stageProbe[stage];
    const bool mosaicLayer = li == 0 && (stage == 6 || stage == 8) && !view_.wipe;
    Ctx c{m, snap_, ui, fW, fH, inspectorLeft ? ui.s(12) : fW - cardW - ui.s(12), topH + ui.s(10), cardW, capY - ui.s(40), anim_, li,
          sp[0], sp[1], mosaicLayer};
    if (view_.lab) {
        // ------------------------------------------------------------ image-processing lab
        const fx::Kernel kern = fx::kernel(static_cast<fx::KernelType>(view_.kernel));
        const float iw = fW * 0.54f, ih = iw * fH / fW, ix = ui.s(12), iy = topH + ui.s(10);
        ui.panel(ix - ui.s(4), iy - ui.s(4), iw + ui.s(8), ih + ui.s(30), kCyan);
        ui.inset(m.labTex, ix, iy, iw, ih);
        ui.text(ix, iy + ih + ui.s(6), fmt("kernel: %s  |  gamma %.2f  |  %s", kern.name, view_.gamma, kern.note), ui.s(12), kCyan);
        // sampling demo: the frame minified by 'factor', shown with nearest / bilinear / trilinear filtering
        const float sw = (iw - ui.s(16)) / 3.0f, sh = sw * fH / fW, sy = iy + ih + ui.s(34);
        const char* names[3] = {"NEAREST", "BILINEAR", "MIPMAP (trilinear)"};
        for (int i = 0; i < 3; ++i) {
            const float sx = ix + i * (sw + ui.s(8));
            ui.panel(sx - ui.s(2), sy - ui.s(2), sw + ui.s(4), sh + ui.s(24), kAmber);
            const float crop[4] = {0.34f, 0.36f, 0.34f + 0.3f, 0.36f + 0.3f};  // a 3.3x enlarged crop: the samples are visible
            ui.inset(m.finalTex, sx, sy, sw, sh, 4, 0, static_cast<float>(view_.sampleFactor), crop, 1.0f);
            ui.insets.back().b = static_cast<GLuint>(i);  // method travels in 'b' (see below)
            ui.text(sx + ui.s(4), sy + sh + ui.s(4), fmt("%s  1/%d", names[i], view_.sampleFactor), ui.s(11), kAmber);
        }
        // kernel card
        const float kx = ix + iw + ui.s(18), kw = fW - kx - ui.s(12);
        float y = topH + ui.s(10);
        ui.panel(kx, y, kw, ui.s(200), kAmber);
        ui.text(kx + ui.s(12), y + ui.s(8), fmt("KERNEL  %s  (K)", kern.name), ui.s(12.5f), kAmber);
        const float cell = std::min(ui.s(kern.size == 5 ? 30 : 44), (ui.s(150)) / kern.size);
        const float gx = kx + ui.s(16), gy = y + ui.s(32);
        float mxw = 1e-6f;
        for (int i = 0; i < kern.size * kern.size; ++i) mxw = std::max(mxw, std::fabs(kern.w[i]));
        for (int j = 0; j < kern.size; ++j)
            for (int i = 0; i < kern.size; ++i) {
                const float wv = kern.w[j * kern.size + i];
                const Rgba col = wv > 0 ? alpha(kGreen, 0.12f + 0.5f * wv / mxw) : wv < 0 ? alpha(kRed, 0.12f - 0.5f * wv / mxw) : Rgba{1, 1, 1, 0.05f};
                ui.rect(gx + i * cell, gy + j * cell, cell - 2, cell - 2, col);
                const std::string t = kern.type == fx::KernelType::Gaussian ? std::to_string(static_cast<int>(std::lround(wv * 256))) :
                                      kern.type == fx::KernelType::Box ? std::string("1/9") : fmt("%g", wv);
                ui.text(gx + i * cell + ui.s(4), gy + j * cell + cell * 0.3f, t, ui.s(kern.size == 5 ? 10 : 12), kText);
            }
        const float tx = gx + kern.size * cell + ui.s(16);
        float ty = gy;
        ui.text(tx, ty, fmt("sum of weights = %.3g", fx::kernelSum(kern)), ui.s(12), kText);
        ty += ui.s(18);
        if (kern.type == fx::KernelType::Gaussian) {
            ui.text(tx, ty, "(weights x 1/256)", ui.s(11), kDim);
            ty += ui.s(16);
        }
        ty += ui.wrap(tx, ty, kw - (tx - kx) - ui.s(10), kern.note, ui.s(11.5f), kDim);
        ty += ui.s(6);
        ui.wrap(tx, ty, kw - (tx - kx) - ui.s(10), "I'(x,y) = sum_ij K(i,j) I(x+i, y+j)", ui.s(12), kCyan);
        // gamma + histogram
        y += ui.s(212);
        const float hw = (kw - ui.s(10)) * 0.5f, hh = ui.s(200);
        ui.panel(kx, y, hw, hh, kCyan);
        ui.text(kx + ui.s(10), y + ui.s(8), fmt("GAMMA %.2f  ([ ])", view_.gamma), ui.s(12), kCyan);
        {
            const float gx2 = kx + ui.s(16), gy2 = y + ui.s(32), gs = std::min(hw - ui.s(32), hh - ui.s(60));
            ui.rect(gx2, gy2, gs, gs, {1, 1, 1, 0.04f});
            ui.line(gx2, gy2 + gs, gx2 + gs, gy2, alpha(kDim, 0.5f));
            float lx = gx2, ly = gy2 + gs;
            for (int i = 1; i <= 40; ++i) {
                const float vin = i / 40.0f, vout = fx::applyGamma(vin, view_.gamma);
                const float x = gx2 + gs * vin, yy = gy2 + gs * (1 - vout);
                ui.line(lx, ly, x, yy, kCyan);
                lx = x;
                ly = yy;
            }
            ui.text(gx2, gy2 + gs + ui.s(6), "out = in^(1/gamma)", ui.s(11), kDim);
        }
        ui.panel(kx + hw + ui.s(10), y, hw, hh, kAmber);
        ui.text(kx + hw + ui.s(20), y + ui.s(8), "HISTOGRAM (output)", ui.s(12), kAmber);
        {
            const float hx = kx + hw + ui.s(22), hy = y + ui.s(32), hwid = hw - ui.s(24), hht = hh - ui.s(56);
            int mx = 1;
            for (int v : m.labHist[0]) mx = std::max(mx, v);
            const float bw = hwid / 64.0f;
            for (int i = 0; i < 64 && i < static_cast<int>(m.labHist[0].size()); ++i) {
                const float hgt = hht * std::sqrt(static_cast<float>(m.labHist[0][static_cast<size_t>(i)]) / mx);
                ui.rect(hx + i * bw, hy + hht - hgt, bw - 0.5f, hgt, {0.85f, 0.88f, 0.92f, 0.45f});
            }
            const Rgba cc[3] = {{1, 0.3f, 0.3f, 0.9f}, {0.3f, 1, 0.4f, 0.9f}, {0.4f, 0.6f, 1, 0.9f}};
            for (int ch = 0; ch < 3; ++ch) {
                const auto& hs = m.labHist[ch + 1];
                for (int i = 1; i < 64 && i < static_cast<int>(hs.size()); ++i)
                    ui.line(hx + (i - 0.5f) * bw, hy + hht - hht * std::sqrt(static_cast<float>(hs[static_cast<size_t>(i - 1)]) / mx),
                            hx + (i + 0.5f) * bw, hy + hht - hht * std::sqrt(static_cast<float>(hs[static_cast<size_t>(i)]) / mx), cc[ch]);
            }
            ui.text(hx, hy + hht + ui.s(6), "0           luminance          255", ui.s(10), kDim);
        }
        // sampling explanation
        y += hh + ui.s(12);
        const float sh2 = capY - y + capH;
        ui.panel(kx, y, kw, std::max(sh2 - ui.s(4), ui.s(60)), kAmber);
        float yy = y + ui.s(8);
        ui.text(kx + ui.s(10), yy, fmt("SAMPLING / ALIASING  (S: 1/%d)", view_.sampleFactor), ui.s(12), kAmber);
        yy += ui.s(20);
        yy += ui.wrap(kx + ui.s(10), yy, kw - ui.s(20),
                      "Shrinking an image keeps one sample per output pixel. Nearest picks one texel: fine detail (grass, fences, "
                      "edges) aliases into noise and moire. Bilinear blends 4 texels: still aliased when shrinking more than 2x. "
                      "Mipmapping pre-filters the image into a pyramid and reads the level whose texels match the pixel size.",
                      ui.s(11.5f), kText);
        ui.wrap(kx + ui.s(10), yy + ui.s(4), kw - ui.s(20), "lod = log2(texels per pixel)     Nyquist: f_sample >= 2 f_max", ui.s(12), kCyan);
    } else if (view_.captions) {
        switch (stage) {
            case 0: cardGraph(c); break;
            case 1: cardVertex(c); break;
            case 2: cardAssembly(c); break;
            case 3: cardRaster(c); break;
            case 4: cardInterp(c); break;
            case 5: cardDepth(c); break;
            case 6: cardGBuffer(c); break;
            case 7: cardShadow(c); break;
            case 8: cardLighting(c); break;
            case 9: cardReflection(c); break;
            case 10: cardSsao(c); break;
            case 11: cardFog(c); break;
            case 12: cardBloom(c); break;
            case 13: cardTone(c); break;
            case 14: cardTaa(c); break;
            default: cardFinal(c); break;
        }
        // split labels
        const auto splitLabel = [&](float x, const std::string& t, bool right) {
            const float tw = ui.tw(t, ui.s(13));
            const float maxX = (view_.captions && !inspectorLeft ? c.cardX : fW) - tw - ui.s(20);  // keep clear of the card
            const float lx = clampf(right ? x + ui.s(10) : x - tw - ui.s(18), ui.s(14), maxX);
            ui.rect(lx - ui.s(6), topH + ui.s(10), tw + ui.s(14), ui.s(24), kPanel);
            ui.text(lx + 1, topH + ui.s(15), t, ui.s(13), kAmber);
        };
        if (trans_ >= 1.0f) {
            if (view_.wipe && stage > 0) {
                splitLabel(view_.wipeX * fW, fmt("%02d %s", stage, fx::stageInfo(stage - 1).shortName), false);
                splitLabel(view_.wipeX * fW, fmt("%02d %s", stage + 1, info.shortName), true);
            } else if (L.b && !L.reveal && !L.labelA.empty()) {
                const float sp = (view_.split >= 0.0f ? view_.split : L.split) * fW;
                splitLabel(sp, L.labelA, false);
                splitLabel(sp, L.labelB, true);
            }
        }
    }

    // ---------------------------------------------------------------- top bar + timeline
    if (view_.captions) {
        ui.rect(0, 0, fW, topH, {0.01f, 0.014f, 0.022f, 0.86f});
        ui.rect(0, topH - ui.s(1), fW, ui.s(1), alpha(kAmber, 0.35f));
        ui.text(ui.s(16), ui.s(8), view_.lab ? "FRAME EXPLORER  /  IMAGE-PROCESSING LAB" : "FRAME EXPLORER", ui.s(11), kAmber);
        const std::string num = fmt("%02d", stage + 1);
        ui.text(ui.s(16), ui.s(22), num, ui.s(30), kAmber);
        ui.text(ui.s(16) + ui.tw(num, ui.s(30)) + ui.s(2), ui.s(36), "/16", ui.s(13), kDim);
        ui.text(ui.s(16) + ui.tw(num, ui.s(30)) + ui.s(38), ui.s(28), view_.lab ? std::string("Convolution, gamma, histogram, sampling") : std::string(info.title),
                ui.s(19), kText);
        const std::string keys = "<- -> stage   Up/Dn layer   W wipe   M lens   I inspect   P image lab   Space play   Tab text   ` close";
        ui.text(fW - ui.tw(keys, ui.s(10.5f)) - ui.s(16), ui.s(10), keys, ui.s(10.5f), kDim);
        ui.text(fW - ui.tw(m.info, ui.s(10.5f)) - ui.s(16), ui.s(26), m.info, ui.s(10.5f), alpha(kDim, 0.8f));
        // timeline
        const float tlY = ui.s(62), segW = (fW - ui.s(32)) / fx::kStageCount;
        for (int i = 0; i < fx::kStageCount; ++i) {
            const float x = ui.s(16) + i * segW;
            const Rgba bar = i == stage ? kAmber : i < stage ? alpha(kAmber, 0.38f) : Rgba{1, 1, 1, 0.12f};
            ui.rect(x + 1, tlY + ui.s(13), segW - 2, ui.s(3), bar);
            const std::string sn = fx::stageInfo(i).shortName;
            const float tw = ui.tw(sn, ui.s(9.5f));
            ui.text(x + (segW - tw) * 0.5f, tlY, sn, ui.s(9.5f), i == stage ? kAmber : i < stage ? alpha(kText, 0.75f) : alpha(kDim, 0.6f));
        }
    } else if (view_.tag) {
        const std::string t = fmt("%02d  %s", stage + 1, info.title);
        ui.rect(ui.s(10), ui.s(10), ui.tw(t, ui.s(14)) + ui.s(16), ui.s(26), kPanel);
        ui.text(ui.s(18), ui.s(15), t, ui.s(14), kAmber);
    }

    // ---------------------------------------------------------------- caption panel + layer chips
    if (view_.captions && !view_.lab) {
        const float x = ui.s(12), wTot = fW - ui.s(24);
        const float leftW = wTot * 0.57f;
        ui.rect(x, capY, wTot, capH, kPanel);
        ui.rect(x, capY, ui.s(3), capH, kAmber);
        const float a = std::min(1.0f, trans_ * 1.6f);
        ui.text(x + ui.s(16), capY + ui.s(10), "CONCEPTS", ui.s(10), alpha(kAmber, a));
        ui.text(x + ui.s(82), capY + ui.s(10), info.concepts, ui.s(11), alpha(kDim, a));
        ui.wrap(x + ui.s(16), capY + ui.s(30), leftW - ui.s(26), info.caption, ui.s(13.2f), alpha(kText, a), 1.36f);
        const float fx0 = x + leftW + ui.s(4), fw = wTot - leftW - ui.s(16);
        ui.rect(fx0, capY + ui.s(10), fw, capH - ui.s(20), {0.36f, 0.88f, 1.0f, 0.06f});
        ui.rect(fx0, capY + ui.s(10), ui.s(2), capH - ui.s(20), alpha(kCyan, 0.8f));
        ui.text(fx0 + ui.s(12), capY + ui.s(18), "KEY FORMULA", ui.s(10), alpha(kCyan, a));
        {   // the formula's parts are separated by runs of spaces: one part per line
            std::string f = info.formula;
            float fy = capY + ui.s(36);
            size_t pos = 0;
            while (pos < f.size()) {
                size_t gap = f.find("   ", pos);
                const std::string part = f.substr(pos, gap == std::string::npos ? std::string::npos : gap - pos);
                if (!part.empty()) fy += ui.wrap(fx0 + ui.s(12), fy, fw - ui.s(22), part, ui.s(14), alpha(kCyan, a), 1.4f);
                if (gap == std::string::npos) break;
                pos = f.find_first_not_of(' ', gap);
                if (pos == std::string::npos) break;
            }
        }
        // layer chips
        float cx = x;
        const float cy = capY - ui.s(30);
        ui.rect(cx, cy, ui.tw("LAYER", ui.s(10)) + ui.s(14), ui.s(22), kPanel);
        ui.text(cx + ui.s(7), cy + ui.s(6), "LAYER", ui.s(10), kDim);
        cx += ui.tw("LAYER", ui.s(10)) + ui.s(20);
        for (size_t i = 0; i < layers.size(); ++i) {
            const std::string& nm = layers[i].name;
            const float tw = ui.tw(nm, ui.s(11.5f)) + ui.s(16);
            if (cx + tw > fW - ui.s(110)) break;
            const bool on = static_cast<int>(i) == li;
            ui.rect(cx, cy, tw, ui.s(22), on ? alpha(kAmber, 0.92f) : kPanel);
            ui.text(cx + ui.s(8), cy + ui.s(5), nm, ui.s(11.5f), on ? Rgba{0.05f, 0.04f, 0.02f, 1} : alpha(kText, 0.85f));
            cx += tw + ui.s(6);
        }
        ui.text(cx + ui.s(6), cy + ui.s(6), layers.size() > 1 ? "Up / Down" : "", ui.s(10), kDim);
    }

    // ---------------------------------------------------------------- inspector
    const bool haveCursor = view_.cursorX >= 0.0f && view_.cursorY >= 0.0f && view_.cursorX < fW && view_.cursorY < fH;
    // 2 x 2 mosaic layers: the cursor's quadrant-local position is the pixel the inspector reads
    const float mcx = mosaicLayer ? std::fmod(view_.cursorX, fW * 0.5f) * 2.0f : view_.cursorX;
    const float mcy = mosaicLayer ? std::fmod(view_.cursorY, fH * 0.5f) * 2.0f : view_.cursorY;
    const int cpx = static_cast<int>(mcx / fW * snap_.w), cpy = static_cast<int>(mcy / fH * snap_.h);
    if (view_.inspector && haveCursor && !view_.lab) {
        const auto rows = fx::inspect(snap_, cpx, cpy);
        const float pw = ui.s(372), rh = ui.s(16.5f);
        const float px = fW - pw - ui.s(12), py = topH + ui.s(10);
        ui.panel(px, py, pw, ui.s(34) + rh * rows.size(), kGreen);
        ui.text(px + ui.s(12), py + ui.s(9), "PIXEL INSPECTOR  (every buffer at the cursor)", ui.s(12), kGreen);
        float y = py + ui.s(30);
        for (const auto& row : rows) {
            ui.text(px + ui.s(12), y, row.label, ui.s(11.5f), kDim);
            float vx = px + ui.s(118);
            if (row.hasSwatch) {
                ui.rect(vx, y + ui.s(1), ui.s(11), ui.s(11), rgba(row.swatch));
                vx += ui.s(16);
            }
            ui.text(vx, y, row.value.substr(0, row.hasSwatch ? 37 : 40), ui.s(11.5f), kText);
            y += rh;
        }
        // cursor crosshair
        ui.lineTop(view_.cursorX - ui.s(10), view_.cursorY, view_.cursorX + ui.s(10), view_.cursorY, kGreen);
        ui.lineTop(view_.cursorX, view_.cursorY - ui.s(10), view_.cursorX, view_.cursorY + ui.s(10), kGreen);
    }
    // ---------------------------------------------------------------- magnifier
    if (view_.magnifier && haveCursor && !view_.lab) {
        const float ls = ui.s(220), zoom = view_.zoom;
        float lx = view_.cursorX + ui.s(26), ly = view_.cursorY + ui.s(26);
        if (lx + ls > fW - ui.s(8)) lx = view_.cursorX - ls - ui.s(26);
        if (ly + ls > fH - ui.s(8)) ly = view_.cursorY - ls - ui.s(26);
        GLuint tex = L.a;
        if (view_.wipe && stage > 0 && view_.cursorX < view_.wipeX * fW) tex = m.layers[stage - 1].front().a;
        else if (L.b && !L.reveal && view_.cursorX > (view_.split >= 0.0f ? view_.split : L.split) * fW) tex = L.b;
        const float hu = ls / zoom * 0.5f / fW, hv = ls / zoom * 0.5f / fH;
        const float u = view_.cursorX / fW, v = 1.0f - view_.cursorY / fH;
        const float uv[4] = {u - hu, v - hv, u + hu, v + hv};
        ui.rect(lx - ui.s(3), ly - ui.s(3), ls + ui.s(6), ls + ui.s(26), kPanel);
        ui.inset(tex, lx, ly, ls, ls, 2, 0, 0.5f, uv);
        ui.insets.back().b = zoom >= 6.0f ? 1u : 0u;  // grid flag
        ui.frameTop(lx, ly, ls, ls, kAmber);
        ui.lineTop(lx + ls * 0.5f - ui.s(8), ly + ls * 0.5f, lx + ls * 0.5f + ui.s(8), ly + ls * 0.5f, alpha(kAmber, 0.9f));
        ui.lineTop(lx + ls * 0.5f, ly + ls * 0.5f - ui.s(8), lx + ls * 0.5f, ly + ls * 0.5f + ui.s(8), alpha(kAmber, 0.9f));
        ui.text(lx + ui.s(4), ly + ls + ui.s(5), fmt("x%.0f   pixel (%d, %d)   wheel = zoom", zoom, cpx, cpy), ui.s(11), kAmber);
        const float hw = ls / zoom * 0.5f;
        ui.frameTop(view_.cursorX - hw, view_.cursorY - hw, 2 * hw, 2 * hw, alpha(kAmber, 0.8f));
    }
    hud.end();

    // ---------------------------------------------------------------- inset images, then text on top
    for (const Ui::I& i : ui.insets) {
        if (i.mode == 4) {
            // sampling demo: method in 'b'
            m.display->use();
            m.image(i.a, i.x, i.y, i.w, i.h, W, H, 4, 0, i.split, i.uv, i.alpha, static_cast<float>(i.b));
        } else if (i.mode == 2) {
            m.image(i.a, i.x, i.y, i.w, i.h, W, H, 2, 0, i.split, i.uv, i.alpha, static_cast<float>(i.b));
        } else {
            m.image(i.a, i.x, i.y, i.w, i.h, W, H, i.mode, i.b, i.split, i.uv, i.alpha);
        }
    }
    hud.begin(W, H);
    for (const Ui::L& l : ui.lines) hud.line(l.x0, l.y0, l.x1, l.y1, l.c);
    for (const Ui::T& t : ui.texts) hud.text(t.x, t.y, t.s, t.size, t.c);
    hud.end();
    glActiveTexture(GL_TEXTURE0);
}

}  // namespace ts
