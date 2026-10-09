// v7 WP-I Frame Explorer (see FrameExplorer.h). Part 1: GPU helpers, the explorer's own passes, open().
// Part 2 (FrameExplorerUi.inc, included at the end): overlay drawing, stage diagrams, input.
#include "rendering/FrameExplorer.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>

#include "math/Frustum.h"
#include "objects/Terrain.h"
#include "primitives/Library.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "rendering/Mesh.h"
#include "rendering/RayTracerGL.h"
#include "rendering/RenderTarget.h"
#include "rendering/Shader.h"
#include "vision/CvPipeline.h"

namespace ts::glsl {
extern const char* kExplGeoVert;
extern const char* kExplGeoGeom;
extern const char* kExplGeoFrag;
extern const char* kExplShadeVert;
extern const char* kExplShadeFrag;
extern const char* kExplSimpleVert;
extern const char* kExplOverdrawFrag;
extern const char* kExplBirdFrag;
extern const char* kExplWireFrag;
extern const char* kExplDisplayVert;
extern const char* kExplDisplayFrag;
extern const char* kExplShadowCopyFrag;
}  // namespace ts::glsl

namespace ts {

namespace {

using fx::Plane;
using fx::Vec2f;

// ------------------------------------------------------------------------------------------------ CPU images
struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;  // rows top to bottom
    Img() = default;
    Img(int W, int H, uint8_t v = 0) : w(W), h(H), rgb(static_cast<size_t>(W) * H * 3, v) {}
    bool valid() const { return w > 0 && h > 0 && rgb.size() == static_cast<size_t>(w) * h * 3; }
    uint8_t* px(int x, int y) { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
    const uint8_t* px(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
};

uint8_t to8(float v) {
    if (!(v == v)) v = 0.0f;
    return static_cast<uint8_t>(std::lround(clampf(v, 0.0f, 1.0f) * 255.0f));
}
float lum3(const float* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }
float srgb(float v) { return fx::srgbEncode(v); }

// Turbo-like false-colour ramp (dark blue -> cyan -> green -> yellow -> red -> white).
Vec3 ramp(float t) {
    static const Vec3 stops[] = {{0.03f, 0.02f, 0.18f}, {0.10f, 0.22f, 0.80f}, {0.05f, 0.75f, 0.90f}, {0.20f, 0.85f, 0.30f},
                                 {0.95f, 0.88f, 0.12f}, {0.98f, 0.45f, 0.08f}, {0.80f, 0.06f, 0.06f}, {1.0f, 0.95f, 0.95f}};
    const int n = static_cast<int>(sizeof(stops) / sizeof(stops[0]));
    if (!(t == t)) t = 0.0f;
    const float x = clampf(t, 0.0f, 1.0f) * (n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    return lerp(stops[i], stops[i + 1], x - static_cast<float>(i));
}

const Vec3 kSky{0.018f, 0.022f, 0.034f};  // background of the explorer's own images where nothing was drawn

// Image of a plane through f(texel) -> display RGB (0..1), flipped to top-down.
Img fromPlane(const Plane& p, const std::function<Vec3(const float*)>& f) {
    Img im(p.w, p.h);
    if (!p.valid()) return im;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const Vec3 c = f(&p.v[(static_cast<size_t>(p.h - 1 - y) * p.w + x) * p.c]);
            uint8_t* o = im.px(x, y);
            o[0] = to8(c.x);
            o[1] = to8(c.y);
            o[2] = to8(c.z);
        }
    return im;
}

Img resized(const Img& s, int dw, int dh) {
    Img d(dw, dh);
    if (!s.valid()) return d;
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * s.h / dh, y1 = std::max(y0 + 1, (y + 1) * s.h / dh);
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * s.w / dw, x1 = std::max(x0 + 1, (x + 1) * s.w / dw);
            int acc[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1 && yy < s.h; ++yy)
                for (int xx = x0; xx < x1 && xx < s.w; ++xx) {
                    const uint8_t* p = s.px(xx, yy);
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    ++n;
                }
            uint8_t* o = d.px(x, y);
            for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(acc[k] / std::max(n, 1));
        }
    }
    return d;
}

void blit(Img& dst, const Img& src, int ox, int oy) {
    for (int y = 0; y < src.h; ++y) {
        const int yy = y + oy;
        if (yy < 0 || yy >= dst.h) continue;
        for (int x = 0; x < src.w; ++x) {
            const int xx = x + ox;
            if (xx < 0 || xx >= dst.w) continue;
            std::memcpy(dst.px(xx, yy), src.px(x, y), 3);
        }
    }
}

void fill(Img& im, Vec3 c) {
    for (size_t i = 0; i + 2 < im.rgb.size(); i += 3) {
        im.rgb[i] = to8(c.x);
        im.rgb[i + 1] = to8(c.y);
        im.rgb[i + 2] = to8(c.z);
    }
}

// 2 x 2 mosaic (a b / c d) at w x h with a 2-pixel dark gutter.
Img mosaic(const Img& a, const Img& b, const Img& c, const Img& d, int w, int h) {
    Img out(w, h);
    fill(out, {0.01f, 0.012f, 0.018f});
    const int hw = (w - 2) / 2, hh = (h - 2) / 2;
    blit(out, resized(a, hw, hh), 0, 0);
    blit(out, resized(b, hw, hh), hw + 2, 0);
    blit(out, resized(c, hw, hh), 0, hh + 2);
    blit(out, resized(d, hw, hh), hw + 2, hh + 2);
    return out;
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<float>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

Plane planeOf(const Img& im) {
    Plane p;
    p.w = im.w;
    p.h = im.h;
    p.c = 3;
    p.v.resize(static_cast<size_t>(im.w) * im.h * 3);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            for (int k = 0; k < 3; ++k)
                p.v[(static_cast<size_t>(im.h - 1 - y) * im.w + x) * 3 + k] = im.px(x, y)[k] / 255.0f;
    return p;
}

// Bilinear sample of a plane at uv (v up), clamp to edge.
void bilinear(const Plane& p, float u, float v, float* out) {
    const float fx = u * p.w - 0.5f, fy = v * p.h - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - x0, ty = fy - y0;
    for (int k = 0; k < p.c; ++k) out[k] = 0.0f;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int x = std::max(0, std::min(p.w - 1, x0 + i)), y = std::max(0, std::min(p.h - 1, y0 + j));
            const float wgt = (i ? tx : 1.0f - tx) * (j ? ty : 1.0f - ty);
            const float* s = &p.v[(static_cast<size_t>(y) * p.w + x) * p.c];
            for (int k = 0; k < p.c; ++k) out[k] += wgt * s[k];
        }
}

// The bloom pass's 13-tap downsample (kBloomDownFrag), with its soft-knee threshold on the first level.
Plane bloomDown(const Plane& src, bool prefilter, float threshold) {
    Plane d;
    d.w = std::max(1, src.w / 2);
    d.h = std::max(1, src.h / 2);
    d.c = 3;
    d.v.assign(static_cast<size_t>(d.w) * d.h * 3, 0.0f);
    const float tx = 1.0f / src.w, ty = 1.0f / src.h;
    static const float taps[13][3] = {{-2, 2, 0.03125f}, {0, 2, 0.0625f}, {2, 2, 0.03125f}, {-2, 0, 0.0625f}, {0, 0, 0.125f},
                                      {2, 0, 0.0625f}, {-2, -2, 0.03125f}, {0, -2, 0.0625f}, {2, -2, 0.03125f},
                                      {-1, 1, 0.125f}, {1, 1, 0.125f}, {-1, -1, 0.125f}, {1, -1, 0.125f}};
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            const float u = (x + 0.5f) / d.w, v = (y + 0.5f) / d.h;
            float col[3] = {0, 0, 0}, s[4];
            for (const auto& t : taps) {
                bilinear(src, u + t[0] * tx, v + t[1] * ty, s);
                for (int k = 0; k < 3; ++k) col[k] += s[k] * t[2];
            }
            if (prefilter) {
                for (float& c : col)
                    if (!std::isfinite(c)) c = 0.0f;
                const float br = std::max({col[0], col[1], col[2]});
                const float knee = threshold * 0.5f;
                float soft = clampf(br - threshold + knee, 0.0f, 2.0f * knee);
                soft = soft * soft / (4.0f * knee + 1e-5f);
                const float k = std::max(soft, br - threshold) / std::max(br, 1e-5f);
                for (float& c : col) c = std::min(c * k, 40.0f);
            }
            float* o = &d.v[(static_cast<size_t>(y) * d.w + x) * 3];
            o[0] = col[0];
            o[1] = col[1];
            o[2] = col[2];
        }
    return d;
}

// ------------------------------------------------------------------------------------------------ GL helpers
GLuint makeTexture(int w, int h, GLenum internalFormat, GLenum format, GLenum type, const void* data, GLenum filter) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter == GL_NEAREST ? GL_NEAREST : GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

std::vector<float> readTexture(GLuint tex, int w, int h, GLenum format, int comps) {
    std::vector<float> v(static_cast<size_t>(w) * h * comps);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, GL_FLOAT, v.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return v;
}

Plane readPlane(GLuint tex, int w, int h, GLenum format, int comps) {
    Plane p;
    p.w = w;
    p.h = h;
    p.c = comps;
    p.v = readTexture(tex, w, h, format, comps);
    return p;
}

// Multiple render targets + a depth renderbuffer.
struct Mrt {
    struct Format {
        GLenum internal, format, type;
    };
    GLuint fbo = 0, depth = 0;
    std::vector<GLuint> tex;
    int w = 0, h = 0;
    bool create(int W, int H, const std::vector<Format>& formats) {
        release();
        w = W;
        h = H;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        std::vector<GLenum> bufs;
        for (size_t i = 0; i < formats.size(); ++i) {
            tex.push_back(makeTexture(W, H, formats[i].internal, formats[i].format, formats[i].type, nullptr, GL_LINEAR));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D, tex.back(), 0);
            bufs.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
        }
        glDrawBuffers(static_cast<GLsizei>(bufs.size()), bufs.data());
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!ok) std::fprintf(stderr, "FrameExplorer: MRT %dx%d (%zu targets) incomplete\n", W, H, formats.size());
        return ok;
    }
    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
    }
    // Keeps the textures (they become stage layers); frees only the framebuffer objects.
    void releaseFbo() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (depth) glDeleteRenderbuffers(1, &depth);
        fbo = depth = 0;
    }
    void release() {
        releaseFbo();
        if (!tex.empty()) glDeleteTextures(static_cast<GLsizei>(tex.size()), tex.data());
        tex.clear();
    }
};

const Mrt::Format kRgba8{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
const Mrt::Format kRgba16f{GL_RGBA16F, GL_RGBA, GL_FLOAT};
const Mrt::Format kRgba32f{GL_RGBA32F, GL_RGBA, GL_FLOAT};
const Mrt::Format kR16f{GL_R16F, GL_RED, GL_FLOAT};

std::string fmtStep(int k, int n) {
    char b[32];
    std::snprintf(b, sizeof(b), "%d/%d", k, n);
    return b;
}

GLuint currentFbo() {
    GLint f = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &f);
    return static_cast<GLuint>(f);
}

std::string leafLabel(const SceneNode& n) {
    std::string s = n.object.empty() ? std::string("scene") : n.object;
    if (!n.name.empty() && n.name != n.object) s += " / " + n.name;
    s += std::string(" (") + primitiveName(n.primitive) + ")";
    return s;
}

Vec3 hashColour(const std::string& s) {
    // A curated palette (distinct, not garish) indexed by an FNV-1a hash of the name.
    static const Vec3 pal[14] = {{0.90f, 0.62f, 0.28f}, {0.36f, 0.66f, 0.92f}, {0.55f, 0.80f, 0.38f}, {0.86f, 0.42f, 0.46f},
                                 {0.62f, 0.50f, 0.86f}, {0.30f, 0.78f, 0.72f}, {0.92f, 0.80f, 0.36f}, {0.78f, 0.52f, 0.70f},
                                 {0.46f, 0.58f, 0.70f}, {0.94f, 0.55f, 0.20f}, {0.40f, 0.86f, 0.52f}, {0.70f, 0.70f, 0.74f},
                                 {0.30f, 0.52f, 0.86f}, {0.82f, 0.68f, 0.52f}};
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return pal[h % 14u];
}

}  // namespace

// ================================================================================================ state
struct Layer {
    std::string name;
    GLuint a = 0, b = 0;            // b != 0: split view a | b at 0.5 (labels below)
    std::string labelA, labelB;
    bool reveal = false;            // scanline reveal animation (raster stage)
    float split = 0.5f;             // default position of the a | b split (drag to move)
};

struct ProbeTri {
    bool valid = false;
    int leaf = -1, tri = -1;
    Vec3 model[3], world[3];
    fx::VertexTrace tr[3];
    bool front = true;
};

struct FrameExplorer::Impl {
    bool ready = false, failed = false;
    std::unique_ptr<Shader> geo, shade, overdraw, bird, wire, display, shadowCopy;
    std::map<PrimitiveType, std::unique_ptr<Mesh>> meshes;
    std::map<PrimitiveType, MeshData> meshData;
    GLuint emptyVao = 0;
    std::vector<GLuint> owned;      // every texture of the current snapshot
    std::vector<Layer> layers[fx::kStageCount];
    GLuint finalTex = 0;            // final frame with mipmaps (sampling demo)
    Img finalImg, thermalImg, heatImg, noTaaImg, ssrOffImg, aoOffImg, fogOffImg, blinnImg, shadowMapImg;
    const Scene* scene = nullptr;
    RenderSettings settings;
    QualityProfile quality;
    // light rig of the frozen frame
    Vec3 lightDir{0, -1, 0}, lightColor{1, 1, 1}, ambient{0.05f, 0.05f, 0.06f};
    bool keyIsSun = false;
    float exposure = 1.0f, meanLog = 0.0f, keyLog2 = -5.2f, bloomThreshold = 1.2f;
    // stage data
    ProbeTri probe, interpTri;
    int probeX = 0, probeY = 0;
    int stageProbe[fx::kStageCount][2] = {};  // pixel each stage's card explains (auto-picked; a click sets them all)
    int ssrX = 0, ssrY = 0;
    fx::SsrMarch march;
    Mat4 birdVP;
    Vec3 birdEye;
    int trisInView = 0, trisFront = 0, trisBack = 0, leavesDrawn = 0;
    float overdrawMean = 0.0f, overdrawMax = 0.0f;
    float depthNear = 0.1f, depthFar = 100.0f;
    std::vector<int> logHist;       // 56 bins of log2 luminance, -12..+2
    float logHistLo = -12.0f, logHistHi = 2.0f;
    std::vector<Img> bloomChain;    // threshold + down mips (display)
    struct Rect { float x, y, w, h; };
    std::vector<Rect> bloomRects;   // in the 1280 x 720 reference of the mosaic image (normalised 0..1)
    GLuint sphereGouraud = 0, spherePhong = 0;
    std::vector<Measurement2D> blobs;
    CvFrameStats cvStats;
    int cvW = 0, cvH = 0;
    // image lab
    int labKernel = -1;
    float labGamma = -1.0f;
    GLuint labTex = 0;
    std::vector<int> labHist[4];
    std::string info;               // frozen-frame description for the top bar

    bool init();
    GLuint keep(GLuint t) {
        owned.push_back(t);
        return t;
    }
    GLuint upload(const Img& im, bool mips = false) {
        std::vector<uint8_t> up(im.rgb.size());
        for (int y = 0; y < im.h; ++y)
            std::memcpy(&up[static_cast<size_t>(im.h - 1 - y) * im.w * 3], im.px(0, y), static_cast<size_t>(im.w) * 3);
        GLuint t = makeTexture(im.w, im.h, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, up.data(), mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        if (mips) {
            glBindTexture(GL_TEXTURE_2D, t);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        return keep(t);
    }
    void releaseImages() {
        if (!owned.empty()) glDeleteTextures(static_cast<GLsizei>(owned.size()), owned.data());
        owned.clear();
        for (auto& l : layers) l.clear();
        finalTex = labTex = sphereGouraud = spherePhong = triAffine = triPersp = 0;
        rtImg = Img();
        rtValid = false;
        labKernel = -1;
        labGamma = -1.0f;
    }
    void add(int stage, const std::string& name, GLuint a, GLuint b = 0, const std::string& la = "", const std::string& lb = "") {
        Layer l;
        l.name = name;
        l.a = a;
        l.b = b;
        l.labelA = la;
        l.labelB = lb;
        layers[stage].push_back(l);
    }
    // Scene leaves the main view draws (same filter as Renderer::drawSolids), each with its leaf index.
    std::vector<std::pair<const SceneNode*, int>> drawList(const Mat4& viewProj) const;
    void drawLeaves(Shader& sh, const std::vector<std::pair<const SceneNode*, int>>& list, bool setMaterial) const;
    ProbeTri triAt(const fx::FrameSnapshot& s, int px, int py) const;
    void computeProbe(const fx::FrameSnapshot& s, int px, int py);
    void computeSsr(const fx::FrameSnapshot& s, int px, int py);
    void autoProbes(const fx::FrameSnapshot& s);
    void buildLab(const fx::FrameSnapshot& s, int kernel, float gamma);
    // v7 WP-H ray tracer: the frame with BVH reflection rays (hybrid mode) and the probe's traced mirror ray
    Img rtImg;
    Renderer* renderer = nullptr;
    bool rtValid = false;
    rt::ReflectionSample rtRefl;
    float ssrHitDist = -1.0f;      // metres from the reflecting point to the SSR hit (< 0: no hit)
    void traceRt(const fx::FrameSnapshot& s);
    GLuint triAffine = 0, triPersp = 0;   // the interpolation probe triangle rasterised on the CPU both ways
    float insetCell = 1.0f;               // its checker square (metres)
    void buildInterpInsets();
    // draw helpers (part 2)
    void image(GLuint a, float x, float y, float w, float h, int W, int H, int mode = 0, GLuint b = 0, float split = 0.5f,
               const float* uv = nullptr, float alpha = 1.0f, float extra = 0.0f) const;
};

bool FrameExplorer::Impl::init() {
    if (ready || failed) return ready;
    using namespace glsl;
    for (auto* p : {&geo, &shade, &overdraw, &bird, &wire, &display, &shadowCopy}) *p = std::make_unique<Shader>();
    bool ok = geo->build("explorer-geo", kExplGeoVert, kExplGeoFrag, kExplGeoGeom);
    ok = ok && shade->build("explorer-shade", kExplShadeVert, kExplShadeFrag);
    ok = ok && overdraw->build("explorer-overdraw", kExplSimpleVert, kExplOverdrawFrag);
    ok = ok && bird->build("explorer-bird", kExplSimpleVert, kExplBirdFrag);
    ok = ok && wire->build("explorer-wire", kExplGeoVert, kExplWireFrag, kExplGeoGeom);
    ok = ok && display->build("explorer-display", kExplDisplayVert, kExplDisplayFrag);
    ok = ok && shadowCopy->build("explorer-shadow-copy", kExplDisplayVert, kExplShadowCopyFrag);
    if (!ok) {
        failed = true;
        return false;
    }
    for (PrimitiveType type : kMeshTypes) {   // v10 M3: the same library meshes as the renderer (primitives/Library.cpp)
        const MeshData mesh = sceneMesh(type);
        meshes[type] = std::make_unique<Mesh>();
        meshes[type]->upload(mesh);
        meshData[type] = mesh;
    }
    glGenVertexArrays(1, &emptyVao);
    ready = true;
    return true;
}

std::vector<std::pair<const SceneNode*, int>> FrameExplorer::Impl::drawList(const Mat4& viewProj) const {
    std::vector<std::pair<const SceneNode*, int>> out;
    if (!scene) return out;
    const Frustum frustum(viewProj);
    const auto& leaves = scene->leaves();
    const bool day = scene->lights.atmo.night <= 0.0f;
    for (size_t i = 0; i < leaves.size(); ++i) {
        const SceneNode* n = leaves[i];
        if (!n->visible || n->primitive == PrimitiveType::BeamCone || n->primitive == PrimitiveType::None) continue;
        if (day && n->name == "Moon") continue;
        if (!settings.isolate.empty() && n->object != settings.isolate) continue;
        if (settings.hideUav && n->object == "Autonomous UAV") continue;
        if (n->material.opacity < 1.0f) continue;
        if (!meshes.count(n->primitive)) continue;
        const Vec3 c{n->world.at(0, 3), n->world.at(1, 3), n->world.at(2, 3)};
        if (n->primitive != PrimitiveType::TerrainDisc && !frustum.intersects(c, unitPrimitiveRadius(n->world))) continue;
        out.emplace_back(n, static_cast<int>(i));
    }
    return out;
}

void FrameExplorer::Impl::drawLeaves(Shader& sh, const std::vector<std::pair<const SceneNode*, int>>& list,
                                     bool setMaterial) const {
    const GLint matLoc = glGetUniformLocation(sh.id(), "uMaterial");
    for (const auto& [n, leaf] : list) {
        sh.set("uModel", n->world);
        sh.set("uNormalMat", normalMatrix(n->world));
        if (setMaterial) {
            sh.set("uAlbedo", n->material.baseColor);
            sh.set("uLeaf", static_cast<float>(leaf + 1));
            sh.set("uShininess", std::max(n->material.shininess, 1.0f));
            sh.set("uSpecular", n->material.specular);
            if (matLoc >= 0)
                glUniform4f(matLoc, pbrRoughness(n->material), n->material.metalness, n->material.emissive, n->material.specular);
        }
        const Mesh& m = *meshes.at(n->primitive);
        m.bind(true);
        m.draw(true);
    }
    glBindVertexArray(0);
}

// The triangle under snapshot pixel (px, py) from the ID buffer, through the vertex pipeline.
ProbeTri FrameExplorer::Impl::triAt(const fx::FrameSnapshot& s, int px, int py) const {
    ProbeTri t;
    px = std::max(0, std::min(s.w - 1, px));
    py = std::max(0, std::min(s.h - 1, py));
    const float* id = s.ids.at((px + 0.5f) / s.w, 1.0f - (py + 0.5f) / s.h);
    if (!id || !scene) return t;
    const int leaf = static_cast<int>(std::lround(id[0])) - 1, tri = static_cast<int>(std::lround(id[1]));
    if (leaf < 0 || leaf >= static_cast<int>(scene->leaves().size())) return t;
    const SceneNode* n = scene->leaves()[static_cast<size_t>(leaf)];
    auto it = meshData.find(n->primitive);
    if (it == meshData.end() || tri < 0 || static_cast<size_t>(tri) * 3 + 2 >= it->second.indices.size()) return t;
    t.valid = true;
    t.leaf = leaf;
    t.tri = tri;
    t.front = id[2] > 0.5f;
    for (int k = 0; k < 3; ++k) {
        t.model[k] = it->second.vertices[it->second.indices[static_cast<size_t>(tri) * 3 + k]].position;
        t.tr[k] = fx::traceVertex(t.model[k], n->world, s.view, s.proj, s.w, s.h);
        t.world[k] = t.tr[k].world;
    }
    return t;
}

void FrameExplorer::Impl::computeProbe(const fx::FrameSnapshot& s, int px, int py) {
    probeX = std::max(0, std::min(s.w - 1, px));
    probeY = std::max(0, std::min(s.h - 1, py));
    probe = triAt(s, probeX, probeY);
}

void FrameExplorer::Impl::computeSsr(const fx::FrameSnapshot& s, int px, int py) {
    ssrX = std::max(0, std::min(s.w - 1, px));
    ssrY = std::max(0, std::min(s.h - 1, py));
    const Vec2f uv{(ssrX + 0.5f) / s.w, 1.0f - (ssrY + 0.5f) / s.h};
    Vec3 n{0, 1, 0};
    if (const float* p = s.surface.at(uv.x, uv.y)) n = {p[0] * 2 - 1, p[1] * 2 - 1, p[2] * 2 - 1};
    fx::DepthView dv{s.depth.w, s.depth.h, s.depth.valid() ? s.depth.v.data() : nullptr};
    march = fx::marchSsr(dv, s.viewProjJ, s.camPos, s.nearZ, s.farZ, uv, n, std::max(s.ssrSteps, 48));
    ssrHitDist = -1.0f;
    if (march.hit) {
        const float d = dv.sample(march.hitUV.x, march.hitUV.y);
        const Vec4 p = inverse(s.viewProjJ) * Vec4{march.hitUV.x * 2 - 1, march.hitUV.y * 2 - 1, d * 2 - 1, 1};
        ssrHitDist = length(Vec3{p.x, p.y, p.z} / p.w - march.origin);
    }
}

// The same reflection through WP-H's CPU tracer (the BVH scene the last ray-traced frame synced): exact mirror ray.
void FrameExplorer::Impl::traceRt(const fx::FrameSnapshot& s) {
    rtValid = false;
    if (!renderer || !renderer->rayTracer() || rtImg.w == 0) return;
    const rt::RtCamera cam = rt::RtCamera::fromView(s.view, s.proj);
    rtRefl = rt::traceReflection(renderer->rayTracer()->scene(), cam, (ssrX + 0.5f) / s.w, (ssrY + 0.5f) / s.h);
    rtValid = rtRefl.hit;
}

// Default probes, one per stage: a medium, well-shaped front-facing triangle near the centre (scene graph, vertex,
// assembly, raster), the pixel where affine interpolation is most wrong (interpolation), a penumbra pixel (shadow),
// a well-lit pixel (lighting), and a reflective pixel whose SSR march travels a while before it hits (reflection).
void FrameExplorer::Impl::autoProbes(const fx::FrameSnapshot& s) {
    const auto at = [&s](const Plane& p, int x, int y) { return p.at((x + 0.5f) / s.w, 1.0f - (y + 0.5f) / s.h); };
    const auto centre = [&s](int x, int y) { return std::hypot((x - s.w * 0.5f) / s.w, (y - s.h * 0.55f) / s.h); };
    float best = -1e9f, bestI = -1e9f, bestS = -1e9f, bestL = -1e9f;
    int bx = s.w / 2, by = s.h / 2, ix = bx, iy = by, sx = bx, sy = by, lx = bx, ly = by;
    for (int j = 0; j < 27; ++j)
        for (int i = 0; i < 48; ++i) {
            const int x = s.w / 16 + i * (s.w * 7 / 8) / 47, y = s.h / 6 + j * (s.h * 2 / 3) / 26;
            const ProbeTri t = triAt(s, x, y);
            const bool inFront = t.valid && t.front && t.tr[0].clip.w > 0.1f && t.tr[1].clip.w > 0.1f && t.tr[2].clip.w > 0.1f;
            const bool unclipped = t.valid && !t.tr[0].clipped && !t.tr[1].clipped && !t.tr[2].clipped;
            if (inFront) {  // interpolation may use triangles reaching past the screen edge (big ground slabs)
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                for (const auto& v : t.tr) {
                    x0 = std::min(x0, v.pixel.x);
                    x1 = std::max(x1, v.pixel.x);
                    y0 = std::min(y0, v.pixel.y);
                    y1 = std::max(y1, v.pixel.y);
                }
                const float bw = x1 - x0, bh = y1 - y0, size = std::max(bw, bh);
                const float area = std::fabs(fx::signedArea2(t.tr[0].pixel, t.tr[1].pixel, t.tr[2].pixel)) * 0.5f;
                const float fillRatio = area / std::max(1.0f, bw * bh), aspect = std::min(bw, bh) / std::max(size, 1.0f);
                if (unclipped && size > 24.0f && size < 170.0f && fillRatio > 0.3f && aspect > 0.35f) {
                    const float score = -std::fabs(std::log(size / 80.0f)) - 2.5f * centre(x, y) + fillRatio + aspect;
                    // the probe is the triangle's centroid (weights 1/3: well inside), which must show this triangle
                    const int cx = static_cast<int>(std::floor((t.tr[0].pixel.x + t.tr[1].pixel.x + t.tr[2].pixel.x) / 3.0f));
                    const int cy = static_cast<int>(std::floor((t.tr[0].pixel.y + t.tr[1].pixel.y + t.tr[2].pixel.y) / 3.0f));
                    if (score > best) {
                        const ProbeTri tc = triAt(s, cx, cy);
                        const fx::Barycentric cb = fx::rasterize(t.tr[0].pixel, t.tr[1].pixel, t.tr[2].pixel, {cx + 0.5f, cy + 0.5f});
                        if (tc.valid && tc.leaf == t.leaf && tc.tri == t.tri && cb.inside && std::min({cb.w0, cb.w1, cb.w2}) >= 0.15f &&
                            std::max({cb.w0, cb.w1, cb.w2}) <= 0.7f) {
                            best = score;
                            bx = cx;
                            by = cy;
                        }
                    }
                }
                // interpolation: world-space error of screen-linear interpolation at this pixel
                const fx::Barycentric b = fx::rasterize(t.tr[0].pixel, t.tr[1].pixel, t.tr[2].pixel, {x + 0.5f, y + 0.5f});
                Vec3 aff, per;
                const float wv[3] = {t.tr[0].clip.w, t.tr[1].clip.w, t.tr[2].clip.w};
                aff = {fx::interpolateAffine(b, t.world[0].x, t.world[1].x, t.world[2].x), fx::interpolateAffine(b, t.world[0].y, t.world[1].y, t.world[2].y),
                       fx::interpolateAffine(b, t.world[0].z, t.world[1].z, t.world[2].z)};
                per = {fx::interpolatePerspective(b, wv[0], wv[1], wv[2], t.world[0].x, t.world[1].x, t.world[2].x),
                       fx::interpolatePerspective(b, wv[0], wv[1], wv[2], t.world[0].y, t.world[1].y, t.world[2].y),
                       fx::interpolatePerspective(b, wv[0], wv[1], wv[2], t.world[0].z, t.world[1].z, t.world[2].z)};
                // a well-shaped triangle (no sliver: area against the longest edge squared) of a useful size
                float longest = 0.0f;
                for (int e = 0; e < 3; ++e)
                    longest = std::max(longest, std::hypot(t.tr[(e + 1) % 3].pixel.x - t.tr[e].pixel.x, t.tr[(e + 1) % 3].pixel.y - t.tr[e].pixel.y));
                const bool shaped = area > 2000.0f && area / std::max(longest * longest, 1.0f) > 0.025f;
                // the bending shows best on a sizeable triangle whose corners lie at very different depths (w ratio)
                const float wr = std::max({wv[0], wv[1], wv[2]}) / std::min({wv[0], wv[1], wv[2]});
                const float err = std::log(wr) * std::min(1.0f, area / 6000.0f) + 0.2f * std::min(length(aff - per), 2.0f) - 0.3f * centre(x, y);
                if (b.inside && shaped && err > bestI) {
                    bestI = err;
                    ix = x;
                    iy = y;
                }
            }
            if (const float* l = at(s.light, x, y)) {
                if (l[3] > 0.0f) {
                    const float pen = l[0] > 0.15f && l[0] < 0.85f && l[1] > 0.3f ? 1.0f - std::fabs(l[0] - 0.5f) - centre(x, y) : -1e9f;
                    if (pen > bestS) {
                        bestS = pen;
                        sx = x;
                        sy = y;
                    }
                    const float lit = l[1] * l[0] + 0.5f * l[2] - centre(x, y);
                    if (l[0] > 0.9f && lit > bestL) {
                        bestL = lit;
                        lx = x;
                        ly = y;
                    }
                }
            }
        }
    computeProbe(s, bx, by);
    interpTri = triAt(s, ix, iy);
    for (auto& p : stageProbe) {
        p[0] = bx;
        p[1] = by;
    }
    stageProbe[static_cast<int>(fx::Stage::Interpolation)][0] = ix;
    stageProbe[static_cast<int>(fx::Stage::Interpolation)][1] = iy;
    stageProbe[static_cast<int>(fx::Stage::Shadow)][0] = sx;
    stageProbe[static_cast<int>(fx::Stage::Shadow)][1] = sy;
    stageProbe[static_cast<int>(fx::Stage::Lighting)][0] = lx;
    stageProbe[static_cast<int>(fx::Stage::Lighting)][1] = ly;
    // Reflection probe
    float bestR = -1e9f;
    int rx = s.w / 2, ry = s.h * 3 / 4;
    if (s.surface.valid()) {
        for (int j = 0; j < 24; ++j)
            for (int i = 0; i < 40; ++i) {
                const int x = s.w / 16 + i * (s.w * 7 / 8) / 39, y = s.h / 3 + j * (s.h * 3 / 5) / 23;
                const float* p = at(s.surface, x, y);
                if (!p || p[3] < 0.08f) continue;
                computeSsr(s, x, y);
                if (!march.hit || march.steps.size() < 6) continue;
                const float dx = (march.hitUV.x * s.w - x), dy = ((1.0f - march.hitUV.y) * s.h - y);
                const float travel = std::sqrt(dx * dx + dy * dy);
                const float score = p[3] + std::min(travel, 220.0f) / 220.0f - centre(x, y) + (p[1] * 2.0f - 1.0f > 0.8f ? 0.8f : 0.0f);  // prefer puddles
                if (travel < 40.0f || score <= bestR) continue;
                bestR = score;
                rx = x;
                ry = y;
            }
    }
    computeSsr(s, rx, ry);
    stageProbe[static_cast<int>(fx::Stage::Reflection)][0] = rx;
    stageProbe[static_cast<int>(fx::Stage::Reflection)][1] = ry;
}


// The interpolation probe triangle drawn twice by a tiny CPU rasteriser (fx::rasterize + the two interpolation
// rules) with a 1 m world checker: the affine copy bends the squares, the perspective-correct one keeps them straight.
void FrameExplorer::Impl::buildInterpInsets() {
    for (GLuint* t : {&triAffine, &triPersp})
        if (*t) {
            owned.erase(std::remove(owned.begin(), owned.end(), *t), owned.end());
            glDeleteTextures(1, t);
            *t = 0;
        }
    const ProbeTri& t = interpTri;
    if (!t.valid) return;
    const int iw = 240, ih = 160;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const auto& v : t.tr) {
        x0 = std::min(x0, v.pixel.x);
        x1 = std::max(x1, v.pixel.x);
        y0 = std::min(y0, v.pixel.y);
        y1 = std::max(y1, v.pixel.y);
    }
    // A window around the probe pixel (big triangles reach far off screen): at most 2.5 screen pixels per texel.
    const float k = clampf(std::max((x1 - x0) / (iw - 12), (y1 - y0) / (ih - 12)), 0.5f, 2.5f);
    const int* pr = stageProbe[static_cast<int>(fx::Stage::Interpolation)];
    const auto centreOn = [](float probe, float lo, float hi, float half) {
        return hi - lo <= 2.0f * half ? 0.5f * (lo + hi) : clampf(probe, lo + half, hi - half);
    };
    const float cx = centreOn(static_cast<float>(pr[0]), x0, x1, iw * k * 0.5f);
    const float cy = centreOn(static_cast<float>(pr[1]), y0, y1, ih * k * 0.5f);
    const Vec3 n = cross(t.world[1] - t.world[0], t.world[2] - t.world[0]);
    const Vec3 an{std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
    const int ua = an.y > std::max(an.x, an.z) ? 0 : (an.x > an.z ? 2 : 0), va = an.y > std::max(an.x, an.z) ? 2 : 1;
    const auto comp = [](const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; };
    const float w[3] = {t.tr[0].clip.w, t.tr[1].clip.w, t.tr[2].clip.w};
    // checker size: about 6 squares across the triangle's longest world edge (the same uv attribute both ways)
    float edge = 0.0f;
    for (int e = 0; e < 3; ++e) edge = std::max(edge, length(t.world[(e + 1) % 3] - t.world[e]));
    const float cell = std::max(edge / 6.0f, 1e-3f);
    insetCell = cell;
    for (int mode = 0; mode < 2; ++mode) {
        Img im(iw, ih);
        fill(im, {0.03f, 0.035f, 0.05f});
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x) {
                const Vec2f p{cx + (x + 0.5f - iw * 0.5f) * k, cy + (y + 0.5f - ih * 0.5f) * k};
                const fx::Barycentric b = fx::rasterize(t.tr[0].pixel, t.tr[1].pixel, t.tr[2].pixel, p);
                if (!b.inside) continue;
                float uv[2];
                for (int a = 0; a < 2; ++a) {
                    const int ax = a == 0 ? ua : va;
                    const float a0 = comp(t.world[0], ax), a1 = comp(t.world[1], ax), a2 = comp(t.world[2], ax);
                    uv[a] = mode == 0 ? fx::interpolateAffine(b, a0, a1, a2) : fx::interpolatePerspective(b, w[0], w[1], w[2], a0, a1, a2);
                }
                const bool odd = (static_cast<int>(std::floor(uv[0] / cell)) + static_cast<int>(std::floor(uv[1] / cell))) & 1;
                const Vec3 c = odd ? Vec3{0.92f, 0.78f, 0.50f} : Vec3{0.10f, 0.11f, 0.14f};
                uint8_t* o = im.px(x, y);
                o[0] = to8(c.x);
                o[1] = to8(c.y);
                o[2] = to8(c.z);
            }
        if (std::getenv("TS_EXPLORER_TRACE"))
            std::fprintf(stderr, "interp inset %d: k %.2f centre (%.0f, %.0f) tri (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f)\n", mode, k, cx, cy,
                         t.tr[0].pixel.x, t.tr[0].pixel.y, t.tr[1].pixel.x, t.tr[1].pixel.y, t.tr[2].pixel.x, t.tr[2].pixel.y);
        (mode == 0 ? triAffine : triPersp) = upload(im);
    }
}

// ================================================================================================ FrameExplorer
FrameExplorer::FrameExplorer() : impl_(std::make_unique<Impl>()) {}

FrameExplorer::~FrameExplorer() {
    if (impl_ && impl_->ready) impl_->releaseImages();  // GL objects of the shaders / meshes free themselves
    if (impl_ && impl_->emptyVao) glDeleteVertexArrays(1, &impl_->emptyVao);
}

void FrameExplorer::close() {
    open_ = false;
    dragging_ = false;
    impl_->scene = nullptr;
}

void FrameExplorer::setStage(int s) {
    s = std::max(0, std::min(fx::kStageCount - 1, s));
    if (s != view_.stage) {
        prevStage_ = view_.stage;
        trans_ = 0.0f;
    }
    view_.stage = s;
    view_.layer = 0;
    view_.split = -1.0f;
    anim_ = 0.0f;
}

int FrameExplorer::layerCount(int stage) const {
    if (stage < 0 || stage >= fx::kStageCount) return 0;
    return static_cast<int>(impl_->layers[stage].size());
}

const char* FrameExplorer::layerName(int stage, int layer) const {
    if (layer < 0 || layer >= layerCount(stage)) return "";
    return impl_->layers[stage][static_cast<size_t>(layer)].name.c_str();
}

void FrameExplorer::setProbe(int px, int py) {
    impl_->computeProbe(snap_, px, py);
    impl_->computeSsr(snap_, px, py);
    impl_->interpTri = impl_->probe;
    impl_->buildInterpInsets();
    impl_->traceRt(snap_);
    for (auto& p : impl_->stageProbe) {
        p[0] = impl_->probeX;
        p[1] = impl_->probeY;
    }
}

void FrameExplorer::setAnimation(float seconds) {
    anim_ = seconds;
    view_.playing = false;
}

bool FrameExplorer::open(Renderer& r, const Scene& scene, const CameraView& cam, const RenderSettings& live, int w, int h) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool trace = std::getenv("TS_EXPLORER_TRACE") != nullptr;
    const auto mark = [&](const char* what) {
        if (!trace) return;
        glFinish();
        std::fprintf(stderr, "explorer open: %-28s %.3f s\n", what, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    };
    Impl& m = *impl_;
    if (w <= 0 || h <= 0 || !m.init()) return false;
    m.releaseImages();
    m.scene = &scene;
    m.renderer = &r;
    const GLuint outerFbo = currentFbo();
    GLint outerViewport[4];
    glGetIntegerv(GL_VIEWPORT, outerViewport);

    // The explorer explains the visible HDR pipeline: force it (the other switches stay as the user set them).
    RenderSettings s = live;
    s.view = ViewMode::Visible;
    s.edges = EdgeMode::Solid;
    s.shading = ShadingOverride::PerObject;
    s.cinematic = true;
    s.depthTest = true;
    s.fade = 1.0f;
    s.letterbox = 0.0f;
    s.motionBlur = 0.0f;
    s.dof = 0.0f;
    s.nucShutter = 0.0f;
    m.settings = s;
    m.quality = qualityProfile(s.quality);
    m.lightDir = normalize(scene.lights.moon.direction);
    m.lightColor = scene.lights.moon.enabled && s.moonOn ? scene.lights.moon.color * scene.lights.moon.intensity : Vec3{0, 0, 0};
    m.ambient = scene.lights.ambient;
    m.keyIsSun = scene.lights.atmo.keyIsSun;
    m.keyLog2 = scene.lights.atmo.exposureKeyLog2;

    RenderTarget rt;
    rt.create(w, h, 0, false);
    FrameStats st;
    const auto report = [&](float f, const std::string& label) {
        if (progress_) progress_(f, label);
    };
    float pf0 = 0.0f, pf1 = 0.0f;  // progress range of the current render() call
    std::string plabel;
    const auto render = [&](const RenderSettings& vs, int frames, bool reset = true) {
        if (reset) r.resetHistory();
        for (int f = 0; f < frames; ++f) {
            if (progress_ && frames > 1) report(pf0 + (pf1 - pf0) * f / frames, plabel + fmtStep(f + 1, frames));
            rt.bind();
            const GLuint fbo = currentFbo();
            st = {};
            r.beginFrame(scene, cam, w, h, vs, st);
            r.endFrame(fbo, w, h, scene.time());
        }
        Img im;
        im.w = w;
        im.h = h;
        rt.read(im.rgb);
        if (im.rgb.size() != static_cast<size_t>(w) * h * 3) {  // RGBA read-back: drop alpha
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) std::memcpy(&rgb[i * 3], &im.rgb[i * 4], 3);
            im.rgb.swap(rgb);
        }
        return im;
    };

    // ---- 1. the frame itself (TAA converges over the 8 jittered frames of its Halton cycle, or continues the live
    //         view's history) + every internal buffer
    pf0 = 0.0f;
    pf1 = 0.32f;
    plabel = "rendering the frame (TAA convergence) ";
    m.finalImg = render(s, std::max(1, convergeFrames_), !keepHistory_);
    mark("frame rendered");
    m.finalTex = m.upload(m.finalImg, true);
    report(0.34f, "copying the internal buffers (HDR, depth, normals, SSAO, fog, bloom)");
    snap_ = fx::FrameSnapshot{};
    if (!r.labSnapshot(snap_)) {
        glBindFramebuffer(GL_FRAMEBUFFER, outerFbo);
        return false;
    }
    m.leavesDrawn = st.primitives;
    snap_.final = planeOf(m.finalImg);
    mark("frame rendered + labSnapshot");
    LabImage lab;
    // The light's view (cascade 0): drawn on the GPU into a 1024^2 float image (reading the full 4096^2 x 2 array
    // back cost 0.6 s), then stretched like Renderer::labReadBuffer (white = near the light, black = empty).
    if (snap_.moonShadowTex && snap_.moonShadowRes > 0) {
        const int n = std::min(1024, snap_.moonShadowRes);
        Mrt sm;
        sm.create(n, n, {Mrt::Format{GL_R32F, GL_RED, GL_FLOAT}});
        sm.bind();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        m.shadowCopy->use();
        const GLint rectLoc = glGetUniformLocation(m.shadowCopy->id(), "uRect"), uvLoc = glGetUniformLocation(m.shadowCopy->id(), "uUv");
        glUniform4f(rectLoc, -1, -1, 1, 1);
        glUniform4f(uvLoc, 0, 0, 1, 1);
        m.shadowCopy->set("uShadow", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, snap_.moonShadowTex);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glBindVertexArray(m.emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        const Plane d = readPlane(sm.tex[0], n, n, GL_RED, 1);
        sm.release();
        float lo = 1.0f, hi = 0.0f;
        for (float v : d.v)
            if (v < 0.9999f) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        if (hi <= lo) hi = lo + 1e-4f;
        m.shadowMapImg = fromPlane(d, [&](const float* p) {
            const float g = p[0] >= 0.9999f ? 0.0f : 1.0f - 0.85f * (p[0] - lo) / (hi - lo);
            return Vec3{g, g, g};
        });
    }
    mark("shadow map read");
    Img logLum;
    if (r.labReadBuffer(LabBuffer::HdrLogLuminance, lab)) {
        logLum.w = lab.w;
        logLum.h = lab.h;
        logLum.rgb = lab.rgb;
    }
    m.exposure = s.exposure * 1.5f;
    m.meanLog = snap_.stats[2];
    if (s.autoExposureOn && (snap_.stats[0] != 0.0f || snap_.stats[1] != 0.0f))
        m.exposure = fx::autoExposure(s.exposure * 1.5f, m.keyLog2, snap_.stats[2], 1.5f);
    m.bloomThreshold = 1.2f * std::exp2(m.keyLog2 + 5.2f);
    snap_.leafNames.clear();
    for (const SceneNode* n : scene.leaves()) snap_.leafNames.push_back(leafLabel(*n));

    mark("frame + snapshot");
    report(0.42f, "explorer passes: G-buffer, IDs, barycentrics, overdraw, back faces, frustum view");
    // ---- 2. the explorer's own passes over the same scene and camera
    const auto list = m.drawList(snap_.viewProj);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    // 2a. G-buffer + IDs + light terms + raster colours + affine / perspective checker + barycentrics (8 targets)
    Mrt geo;
    geo.create(w, h, {kRgba8, kRgba8, kRgba32f, kRgba16f, kRgba8, kRgba8, kRgba8, kRgba8});
    geo.bind();
    {
        const float zero[4] = {0, 0, 0, 0}, sky[4] = {kSky.x, kSky.y, kSky.z, 1};
        for (int i = 0; i < 8; ++i) glClearBufferfv(GL_COLOR, i, i >= 4 ? sky : zero);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    m.geo->use();
    m.geo->set("uViewProj", snap_.viewProj);
    m.geo->set("uCamPos", snap_.camPos);
    m.geo->set("uLightDir", m.lightDir);
    m.geo->set("uShadows", snap_.shadowMode > 0 && snap_.moonShadowTex ? 1 : 0);
    m.geo->set("uMoonVP0", snap_.moonViewProj[0]);
    m.geo->set("uMoonVP1", snap_.moonViewProj[1]);
    m.geo->set("uMoonTexel", snap_.moonTexelWorld[0], snap_.moonTexelWorld[1]);
    m.geo->set("uCascadeSplit", snap_.cascadeSplit);
    m.geo->set("uMoonShadow", 5);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, snap_.moonShadowTex);
    glActiveTexture(GL_TEXTURE0);
    m.drawLeaves(*m.geo, list, true);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE0);
    snap_.albedo = readPlane(geo.tex[0], w, h, GL_RGB, 3);
    snap_.material = readPlane(geo.tex[1], w, h, GL_RGB, 3);
    snap_.ids = readPlane(geo.tex[2], w, h, GL_RGB, 3);
    snap_.light = readPlane(geo.tex[3], w, h, GL_RGBA, 4);
    for (GLuint t : geo.tex) m.keep(t);
    geo.releaseFbo();
    const GLuint texRaster = geo.tex[4], texChkA = geo.tex[5], texChkP = geo.tex[6], texBary = geo.tex[7];

    mark("geo pass + readback");
    // 2b. Gouraud vs Phong over the scene (key light only, no shadows)
    const float lightLum = std::max(0.2126f * m.lightColor.x + 0.7152f * m.lightColor.y + 0.0722f * m.lightColor.z, 0.02f);
    const auto shadeSetup = [&](const Mat4& vp, const Vec3& eye, float expo) {
        m.shade->use();
        m.shade->set("uViewProj", vp);
        m.shade->set("uCamPos", eye);
        m.shade->set("uLightDir", m.lightDir);
        m.shade->set("uLightColor", m.lightColor);
        m.shade->set("uAmbient", m.ambient);
        m.shade->set("uExposure", expo);
    };
    Mrt shadeT;
    shadeT.create(w, h, {kRgba8, kRgba8});
    shadeT.bind();
    {
        const float sky[4] = {kSky.x, kSky.y, kSky.z, 1};
        glClearBufferfv(GL_COLOR, 0, sky);
        glClearBufferfv(GL_COLOR, 1, sky);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    shadeSetup(snap_.viewProj, snap_.camPos, 1.4f / lightLum);
    m.drawLeaves(*m.shade, list, true);
    for (GLuint t : shadeT.tex) m.keep(t);
    shadeT.releaseFbo();
    // A coarse sphere (12 x 8) close up: the classic Gouraud vs Phong comparison.
    {
        Mesh coarse;
        coarse.upload(createSphere(10, 6));
        Mrt sph;
        sph.create(360, 360, {kRgba8, kRgba8});
        sph.bind();
        const float bg[4] = {0.03f, 0.035f, 0.05f, 1};
        glClearBufferfv(GL_COLOR, 0, bg);
        glClearBufferfv(GL_COLOR, 1, bg);
        glClear(GL_DEPTH_BUFFER_BIT);
        const Vec3 eye{0.0f, 0.4f, 2.6f};
        const Mat4 vp = perspective(radians(30.0f), 1.0f, 0.1f, 10.0f) * lookAt(eye, {0, 0, 0}, {0, 1, 0});
        shadeSetup(vp, eye, 1.0f);
        m.shade->set("uLightDir", normalize(Vec3{0.6f, -0.5f, -0.6f}));
        m.shade->set("uLightColor", Vec3{1.0f, 0.95f, 0.85f});
        m.shade->set("uAmbient", Vec3{0.06f, 0.07f, 0.09f});
        m.shade->set("uAlbedo", Vec3{0.75f, 0.42f, 0.22f});
        m.shade->set("uSpecular", 0.9f);
        m.shade->set("uShininess", 60.0f);
        m.shade->set("uModel", Mat4::identity());
        m.shade->set("uNormalMat", normalMatrix(Mat4::identity()));
        coarse.bind(true);
        coarse.draw(true);
        glBindVertexArray(0);
        m.sphereGouraud = m.keep(sph.tex[0]);
        m.spherePhong = m.keep(sph.tex[1]);
        sph.releaseFbo();
    }

    mark("shade passes");
    // 2c. overdraw: every fragment counts (depth test off, back faces still culled as in the real pass)
    Mrt od;
    od.create(w, h, {kR16f});
    od.bind();
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    m.overdraw->use();
    m.overdraw->set("uViewProj", snap_.viewProj);
    m.drawLeaves(*m.overdraw, list, false);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    snap_.overdraw = readPlane(od.tex[0], w, h, GL_RED, 1);
    od.release();

    mark("overdraw");
    // 2d. primitive assembly: front faces with edges (depth tested), back faces as an additive x-ray
    Mrt asmT;
    asmT.create(w, h, {kRgba8, kRgba8, kRgba8});
    const auto wirePass = [&](int target, bool front, bool back) {
        asmT.bind();
        const GLenum buf = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(target);
        glDrawBuffers(1, &buf);
        glClearColor(0.012f, 0.016f, 0.024f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m.wire->use();
        m.wire->set("uViewProj", snap_.viewProj);
        m.wire->set("uCamPos", snap_.camPos);
        if (front) {
            glEnable(GL_DEPTH_TEST);
            glCullFace(GL_BACK);
            m.wire->set("uBack", 0);
            m.drawLeaves(*m.wire, list, false);
        }
        if (back) {
            glDisable(GL_DEPTH_TEST);
            glCullFace(GL_FRONT);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            m.wire->set("uBack", 1);
            m.drawLeaves(*m.wire, list, false);
            glDisable(GL_BLEND);
            glCullFace(GL_BACK);
            glEnable(GL_DEPTH_TEST);
        }
    };
    wirePass(0, true, false);
    wirePass(1, false, true);
    wirePass(2, true, true);
    for (GLuint t : asmT.tex) m.keep(t);
    asmT.releaseFbo();

    mark("assembly");
    // 2e. bird's-eye view: the frozen camera's frustum seen from outside
    Mrt birdT;
    birdT.create(w, h, {kRgba8});
    {
        const Mat4 invView = inverse(snap_.view);
        const Vec3 fwd = normalize(transformDirection(invView, {0, 0, -1}));
        const Vec3 right = normalize(transformDirection(invView, {1, 0, 0}));
        Vec3 flatFwd = normalize(Vec3{fwd.x, 0.0f, fwd.z});
        if (length(flatFwd) < 0.1f) flatFwd = {0, 0, -1};
        const Vec3 focus = snap_.camPos + flatFwd * 12.0f;
        m.birdEye = snap_.camPos - flatFwd * 26.0f + normalize(Vec3{right.x, 0.0f, right.z}) * 30.0f + Vec3{0, 34.0f, 0};
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m.birdVP = perspective(radians(50.0f), aspect, 0.5f, 900.0f) * lookAt(m.birdEye, focus, {0, 1, 0});
        birdT.bind();
        glClearColor(0.035f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m.bird->use();
        m.bird->set("uViewProj", m.birdVP);
        m.bird->set("uCamViewProj", snap_.viewProj);
        m.bird->set("uEye", m.birdEye);
        const GLint albedoLoc = glGetUniformLocation(m.bird->id(), "uAlbedo");
        const auto birdList = m.drawList(m.birdVP);
        for (const auto& [n, leaf] : birdList) {
            (void)leaf;
            m.bird->set("uModel", n->world);
            m.bird->set("uNormalMat", normalMatrix(n->world));
            glUniform3f(albedoLoc, n->material.baseColor.x, n->material.baseColor.y, n->material.baseColor.z);
            const Mesh& mesh = *m.meshes.at(n->primitive);
            mesh.bind(true);
            mesh.draw(true);
        }
        glBindVertexArray(0);
        // Frustum: near rectangle, the edges out to a display distance, the far rectangle there.
        const Mat4 invVP = inverse(snap_.viewProj);
        const float showFar = 30.0f;
        const float dFar = fx::depthFromDistance(showFar, snap_.nearZ, snap_.farZ) * 2.0f - 1.0f;
        Vec3 nearC[4], farC[4];
        const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        for (int i = 0; i < 4; ++i) {
            const Vec4 a = invVP * Vec4{cx[i], cy[i], -1.0f, 1.0f}, b = invVP * Vec4{cx[i], cy[i], dFar, 1.0f};
            nearC[i] = Vec3{a.x, a.y, a.z} / a.w;
            farC[i] = Vec3{b.x, b.y, b.z} / b.w;
        }
        std::vector<LineVertex> lines;
        const Vec3 amber{1.0f, 0.72f, 0.25f}, cyan{0.35f, 0.9f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            lines.push_back({snap_.camPos, amber});
            lines.push_back({farC[i], amber});
            lines.push_back({farC[i], cyan});
            lines.push_back({farC[(i + 1) % 4], cyan});
            lines.push_back({nearC[i], amber});
            lines.push_back({nearC[(i + 1) % 4], amber});
        }
        r.drawLines(lines, GL_LINES, m.birdVP, 1.0f, false);
        glBindFramebuffer(GL_FRAMEBUFFER, birdT.fbo);
    }
    const GLuint texBird = m.keep(birdT.tex[0]);
    birdT.releaseFbo();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    mark("bird");
    // Triangle statistics of the drawn leaves: signed screen area -> front / back (CPU, same matrices).
    m.trisInView = m.trisFront = m.trisBack = 0;
    for (const auto& [n, leaf] : list) {
        (void)leaf;
        const MeshData& md = m.meshData.at(n->primitive);
        const Mat4 mvp = snap_.viewProj * n->world;
        std::vector<Vec4> clip(md.vertices.size());
        for (size_t i = 0; i < md.vertices.size(); ++i) {
            const Vec3& p = md.vertices[i].position;
            clip[i] = mvp * Vec4{p.x, p.y, p.z, 1.0f};
        }
        for (size_t t = 0; t + 2 < md.indices.size(); t += 3) {
            const Vec4 &a = clip[md.indices[t]], &b = clip[md.indices[t + 1]], &c = clip[md.indices[t + 2]];
            if (a.w <= 0.0f || b.w <= 0.0f || c.w <= 0.0f) continue;
            ++m.trisInView;
            const float area = fx::signedArea2({a.x / a.w, a.y / a.w}, {b.x / b.w, b.y / b.w}, {c.x / c.w, c.y / c.w});
            if (area > 0.0f) ++m.trisFront;
            else ++m.trisBack;
        }
    }

    mark("triangle stats");
    // ---- 3. variants through the real renderer: single frames without TAA (no jitter, so the difference images
    //         show only the switched feature); the thermal sensor gets two frames for its gain control.
    RenderSettings base = s;
    base.taaOn = false;
    struct Variant {
        const char* label;
        RenderSettings settings;
        int frames;
        Img* out;
    };
    std::vector<Variant> variants;
    const auto variant = [&](bool on, const char* label, const std::function<void(RenderSettings&)>& change, int frames, Img* out) {
        if (!on) return;
        RenderSettings v = base;
        change(v);
        variants.push_back({label, v, frames, out});
    };
    variant(snap_.taaOn, "one frame without TAA", [](RenderSettings&) {}, 1, &m.noTaaImg);
    variant(snap_.ssrOn, "SSR off", [](RenderSettings& v) { v.ssrOn = false; }, 1, &m.ssrOffImg);
    variant(snap_.aoOn, "SSAO off", [](RenderSettings& v) { v.ssaoOn = false; }, 1, &m.aoOffImg);
    variant(snap_.fogOn, "fog off", [](RenderSettings& v) { v.fogOn = false; }, 1, &m.fogOffImg);
    variant(true, s.pbr ? "Blinn-Phong" : "PBR", [&s](RenderSettings& v) { v.pbr = !s.pbr; }, 1, &m.blinnImg);
    // WP-H: the same frame with BVH reflection rays instead of SSR (hybrid mode; its soft RT shadows differ slightly too)
    // TAA on like the frozen frame: the soft RT shadow rays are stochastic, a single frame would compare noise.
    if (r.rayTracer() != nullptr && snap_.hdrPipeline) {
        RenderSettings v = s;
        v.rtMode = rt::RtMode::Hybrid;
        v.rtOverlay = false;  // no status line in the image
        variants.push_back({"ray-traced reflections (BVH)", v, std::max(4, convergeFrames_), &m.rtImg});
    }
    variant(true, "thermal sensor", [](RenderSettings& v) { v.view = ViewMode::Thermal; }, 1, &m.thermalImg);
    variant(true, "raw heat for the CV", [](RenderSettings& v) { v.view = ViewMode::HeatRaw; }, 1, &m.heatImg);
    for (size_t i = 0; i < variants.size(); ++i) {
        const float f = 0.5f + 0.38f * static_cast<float>(i) / variants.size();
        char buf[160];
        std::snprintf(buf, sizeof(buf), "capturing %zu variants %zu/%zu: %s", variants.size(), i + 1, variants.size(), variants[i].label);
        report(f, buf);
        pf0 = pf1 = f;
        *variants[i].out = render(variants[i].settings, variants[i].frames);
    }
    r.resetHistory();
    rt = RenderTarget{};
    mark("variants");
    report(0.9f, "building the 16 stage images");
    // ---- 4. stage images
    const auto& S = snap_;
    const GLuint texFinal = m.finalTex;
    // 1 scene graph: colour per composite object / per leaf, shaded by the final frame's brightness
    {
        const auto& leaves = scene.leaves();
        std::vector<Vec3> objCol(leaves.size()), leafCol(leaves.size());
        for (size_t i = 0; i < leaves.size(); ++i) {
            objCol[i] = hashColour(leaves[i]->object);
            leafCol[i] = hashColour(leaves[i]->object + "#" + leaves[i]->name + std::to_string(i));
        }
        const auto tint = [&](const std::vector<Vec3>& cols) {
            Img im(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = static_cast<size_t>(h - 1 - y) * w + x;
                    const float* id = &S.ids.v[i * 3];
                    const float* lt = &S.light.v[i * 4];
                    const uint8_t* f = m.finalImg.px(x, y);
                    const float l = (0.2126f * f[0] + 0.7152f * f[1] + 0.0722f * f[2]) / 255.0f;
                    const int leaf = static_cast<int>(std::lround(id[0])) - 1;
                    const float shade = 0.34f + 0.48f * lt[1] * (0.4f + 0.6f * lt[0]) + 0.3f * std::sqrt(l);
                    Vec3 c = leaf < 0 ? kSky : cols[static_cast<size_t>(leaf)] * shade;
                    uint8_t* o = im.px(x, y);
                    o[0] = to8(c.x);
                    o[1] = to8(c.y);
                    o[2] = to8(c.z);
                }
            return im;
        };
        m.add(0, "objects", m.upload(tint(objCol)));
        m.add(0, "parts (leaves)", m.upload(tint(leafCol)));
        m.add(0, "final frame", texFinal);
    }
    // 2 vertex transform
    m.add(1, "outside view + frustum", texBird);
    m.add(1, "camera view", texFinal);
    // 3 assembly
    m.add(2, "front faces kept", asmT.tex[0]);
    m.add(2, "back faces culled (x-ray)", asmT.tex[1]);
    m.add(2, "front + back", asmT.tex[2]);
    // 4 raster
    {
        Layer l;
        l.name = "triangles (scanline reveal)";
        l.a = texRaster;
        l.b = texFinal;
        l.reveal = true;
        m.layers[3].push_back(l);
    }
    m.add(3, "triangles", texRaster);
    m.add(3, "barycentric weights", texBary);
    // 5 interpolation
    m.add(4, "affine | perspective-correct", texChkA, texChkP, "AFFINE (screen-linear)", "PERSPECTIVE-CORRECT (divide by w)");
    m.add(4, "Gouraud | Phong", shadeT.tex[0], shadeT.tex[1], "GOURAUD (lit per vertex)", "PHONG (lit per pixel)");
    m.add(4, "barycentric RGB", texBary);
    m.add(4, "affine only", texChkA);
    m.add(4, "perspective-correct only", texChkP);
    // 6 depth
    {
        std::vector<float> lin;
        lin.reserve(S.depth.v.size());
        for (float d : S.depth.v)
            if (d < 1.0f) lin.push_back(fx::linearizeDepth(d, S.nearZ, S.farZ));
        m.depthNear = std::max(percentile(lin, 0.002f), S.nearZ);
        m.depthFar = std::max(percentile(lin, 0.995f), m.depthNear * 1.5f);
        const float ln = std::log(m.depthNear), lf = std::log(m.depthFar);
        m.add(5, "linear depth (log scale)", m.upload(fromPlane(S.depth, [&](const float* p) {
                  if (p[0] >= 1.0f) return kSky;
                  const float z = fx::linearizeDepth(p[0], S.nearZ, S.farZ);
                  return ramp(1.0f - (std::log(std::max(z, 1e-3f)) - ln) / (lf - ln)) * 0.95f;
              })));
        m.add(5, "stored depth d (0.9 .. 1)", m.upload(fromPlane(S.depth, [&](const float* p) {
                  const float g = clampf((p[0] - 0.9f) / 0.1f, 0.0f, 1.0f);
                  return Vec3{g, g, g};
              })));
        float sum = 0.0f, mx = 0.0f;
        int n = 0;
        for (float c : S.overdraw.v) {
            if (c <= 0.0f) continue;
            sum += c;
            mx = std::max(mx, c);
            ++n;
        }
        m.overdrawMean = n ? sum / static_cast<float>(n) : 0.0f;
        m.overdrawMax = mx;
        m.add(5, "overdraw heat map", m.upload(fromPlane(S.overdraw, [&](const float* p) {
                  return p[0] <= 0.0f ? kSky : ramp((p[0] - 1.0f) / 9.0f);
              })));
    }
    // 7 G-buffer
    {
        const Img albedo = fromPlane(S.albedo, [&](const float* p) {
            return Vec3{srgb(p[0]), srgb(p[1]), srgb(p[2])};
        });
        const Img normal = fromPlane(S.surface, [&](const float* p) {
            return p[0] + p[1] + p[2] <= 0.0f ? kSky : Vec3{p[0], p[1], p[2]};
        });
        const Img rm = fromPlane(S.material, [&](const float* p) { return Vec3{p[0], p[1], p[2] * 0.6f}; });
        const Img dep = fromPlane(S.depth, [&](const float* p) {
            if (p[0] >= 1.0f) return Vec3{0, 0, 0};
            const float z = fx::linearizeDepth(p[0], S.nearZ, S.farZ);
            const float g = 1.0f - clampf((std::log(z) - std::log(m.depthNear)) / (std::log(m.depthFar) - std::log(m.depthNear)), 0.0f, 1.0f);
            return Vec3{g, g, g};
        });
        m.add(6, "all four", m.upload(mosaic(albedo, normal, rm, dep, w, h)));
        m.add(6, "albedo", m.upload(albedo));
        m.add(6, "world normal", m.upload(normal));
        m.add(6, "roughness (R) / metalness (G)", m.upload(rm));
        m.add(6, "depth", m.upload(dep));
    }
    // 8 shadow map
    {
        m.add(7, "shadow visibility", m.upload(fromPlane(S.light, [&](const float* p) {
                  if (p[3] <= 0.0f) return kSky;
                  const float v = p[0] * (p[1] > 0.0f ? 1.0f : 0.25f);
                  return lerp(Vec3{0.04f, 0.06f, 0.12f}, Vec3{1.0f, 0.93f, 0.80f}, v);
              })));
        if (m.shadowMapImg.valid()) m.add(7, "light's depth map", m.upload(m.shadowMapImg));
        m.add(7, "final frame", texFinal);
    }
    // 9 lighting: the same scale for every term so they compare
    {
        std::vector<float> sums;
        sums.reserve(S.light.v.size() / 4);
        const auto term = [&](const float* lt, const float* al, int which) {
            const Vec3 a{al[0], al[1], al[2]};
            const Vec3 amb = mul(a, m.ambient);
            const Vec3 dif = mul(a, m.lightColor) * (lt[1] * lt[0]);
            const Vec3 spe = m.lightColor * (lt[2] * lt[0]);
            return which == 0 ? amb : which == 1 ? dif : which == 2 ? spe : amb + dif + spe;
        };
        for (size_t i = 0; i + 3 < S.light.v.size(); i += 4) {
            if (S.light.v[i + 3] <= 0.0f) continue;
            const Vec3 c = term(&S.light.v[i], &S.albedo.v[i / 4 * 3], 3);
            sums.push_back(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
        }
        const float scale = 0.85f / std::max(percentile(sums, 0.985f), 1e-5f);
        Img img[4];
        for (int k = 0; k < 4; ++k) {
            img[k] = Img(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = static_cast<size_t>(h - 1 - y) * w + x;
                    Vec3 c = kSky;
                    if (S.light.v[i * 4 + 3] > 0.0f) {
                        c = term(&S.light.v[i * 4], &S.albedo.v[i * 3], k) * scale;
                        c = {srgb(c.x), srgb(c.y), srgb(c.z)};
                    }
                    uint8_t* o = img[k].px(x, y);
                    o[0] = to8(c.x);
                    o[1] = to8(c.y);
                    o[2] = to8(c.z);
                }
        }
        m.add(8, "ambient | diffuse / specular | sum", m.upload(mosaic(img[0], img[1], img[2], img[3], w, h)));
        m.add(8, "ambient", m.upload(img[0]));
        m.add(8, "diffuse (Lambert)", m.upload(img[1]));
        m.add(8, "specular (Blinn-Phong)", m.upload(img[2]));
        m.add(8, "sum", m.upload(img[3]));
        if (m.blinnImg.valid())
            m.add(8, s.pbr ? "PBR | Blinn-Phong (renderer)" : "Blinn-Phong | PBR (renderer)", texFinal, m.upload(m.blinnImg),
                  s.pbr ? "PBR (GGX / Cook-Torrance)" : "BLINN-PHONG", s.pbr ? "BLINN-PHONG" : "PBR (GGX / Cook-Torrance)");
    }
    // 10 reflection
    {
        m.add(9, "final + SSR ray march", texFinal);
        m.add(9, "reflectivity (SSR weight)", m.upload(fromPlane(S.surface, [&](const float* p) {
                  const float g = std::sqrt(clampf(p[3], 0.0f, 1.0f));
                  return Vec3{g * 0.75f, g * 0.95f, g};
              })));
        if (m.ssrOffImg.valid()) {
            const Img& onRef = m.noTaaImg.valid() ? m.noTaaImg : m.finalImg;
            Img diff(w, h);
            for (size_t i = 0; i < diff.rgb.size(); ++i)
                diff.rgb[i] = static_cast<uint8_t>(std::min(255, 5 * std::abs(int(onRef.rgb[i]) - int(m.ssrOffImg.rgb[i]))));
            m.add(9, "what SSR added (x5)", m.upload(diff));
            m.add(9, "SSR on | off", texFinal, m.upload(m.ssrOffImg), "SSR ON", "SSR OFF");
        }
        if (m.rtImg.valid()) {  // WP-H: ray-traced reflections (one un-jittered frame each, so they compare 1:1)
            const GLuint rtTex = m.upload(m.rtImg);
            m.add(9, "ray-traced reflections (BVH)", rtTex);
            m.add(9, "SSR | ray traced", texFinal, rtTex, "SSR (screen space)", "RAY TRACED (BVH)");
            // Difference where the surface reflects (weighted by the reflectivity buffer): the reflections, not the
            // (also ray-traced) soft shadows.
            Img diff(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float* sf = &S.surface.v[(static_cast<size_t>(h - 1 - y) * w + x) * 4];
                    const float wgt = std::sqrt(clampf(sf[3] * 4.0f, 0.0f, 1.0f));
                    for (int k = 0; k < 3; ++k) {
                        const size_t i = (static_cast<size_t>(y) * w + x) * 3 + static_cast<size_t>(k);
                        diff.rgb[i] = static_cast<uint8_t>(std::min(255.0f, 5.0f * wgt * std::abs(int(m.rtImg.rgb[i]) - int(m.finalImg.rgb[i]))));
                    }
                }
            m.add(9, "RT - SSR on reflective pixels (x5)", m.upload(diff));
        }
    }
    // 11 SSAO
    if (S.ao.valid()) {
        m.add(10, "ambient visibility", m.upload(fromPlane(S.ao, [](const float* p) { return Vec3{p[0], p[0], p[0]}; })));
        if (m.aoOffImg.valid()) m.add(10, "with | without SSAO", texFinal, m.upload(m.aoOffImg), "WITH SSAO", "WITHOUT");
    } else {
        m.add(10, "final frame (SSAO off at this quality)", texFinal);
    }
    // 12 fog
    if (S.fog.valid()) {
        std::vector<float> l;
        for (size_t i = 0; i + 3 < S.fog.v.size(); i += 4) l.push_back(lum3(&S.fog.v[i]));
        const float peak = std::max(percentile(l, 0.995f), 1e-6f);
        m.add(11, "in-scattered light", m.upload(fromPlane(S.fog, [&](const float* p) {
                  return Vec3{std::sqrt(p[0] / peak), std::sqrt(p[1] / peak), std::sqrt(p[2] / peak)};
              })));
        m.add(11, "transmittance T", m.upload(fromPlane(S.fog, [](const float* p) { return Vec3{p[3], p[3], p[3] * 1.05f}; })));
        if (m.fogOffImg.valid()) m.add(11, "with | without fog", texFinal, m.upload(m.fogOffImg), "WITH FOG", "WITHOUT");
    } else {
        m.add(11, "final frame (volumetric fog off at this quality)", texFinal);
    }
    mark("stage images 1-12");
    // 13 bloom: our own replay of the chain (threshold + 13-tap downsamples) and the renderer's result
    {
        m.bloomChain.clear();
        m.bloomRects.clear();
        Plane level = bloomDown(S.taa, true, m.bloomThreshold);
        std::vector<Plane> chain{level};
        const int mips = std::max(4, std::min(S.bloomMips > 0 ? S.bloomMips : 6, 7));
        for (int k = 1; k < mips; ++k) chain.push_back(level = bloomDown(level, false, 0.0f));
        std::vector<float> l;
        for (size_t i = 0; i + 2 < chain[0].v.size(); i += 3) l.push_back(lum3(&chain[0].v[i]));
        const float peak = std::max(percentile(l, 0.999f), 1e-4f);
        const auto show = [&](const Plane& p) {
            return fromPlane(p, [&](const float* c) {
                const float k = 1.0f / peak;
                return Vec3{std::sqrt(clampf(c[0] * k, 0.0f, 1.0f)), std::sqrt(clampf(c[1] * k, 0.0f, 1.0f)),
                            std::sqrt(clampf(c[2] * k, 0.0f, 1.0f))};
            });
        };
        // Shown over a dimmed copy of the frame so the bright parts keep their context.
        const auto over = [&](const Img& add, const Img& bg) {
            Img o = add;
            for (size_t i = 0; i < o.rgb.size(); ++i) o.rgb[i] = static_cast<uint8_t>(std::min(255, add.rgb[i] + bg.rgb[i] / 5));
            return o;
        };
        const Img bright = over(resized(show(chain[0]), w, h), m.finalImg);
        m.add(12, "bright pass (threshold)", m.upload(bright));
        // Mip pyramid mosaic: level 0 on the left half, each next level half the size to its right.
        Img mos(w, h);
        fill(mos, {0.012f, 0.016f, 0.024f});
        float x = 0.0f;
        const float y0 = h * 0.22f;
        for (size_t k = 0; k < chain.size(); ++k) {
            const int tw = std::max(2, static_cast<int>(w / std::pow(2.0f, static_cast<float>(k + 1))) - 4);
            const int th = std::max(2, tw * h / w);
            const Img level8 = over(resized(show(chain[k]), tw, th), resized(m.finalImg, tw, th));
            blit(mos, level8, static_cast<int>(x) + 2, static_cast<int>(y0));
            m.bloomRects.push_back({(x + 2) / w, y0 / h, static_cast<float>(tw) / w, static_cast<float>(th) / h});
            x += tw + 4;
        }
        m.add(12, "mip chain (13-tap downsample)", m.upload(mos));
        if (S.bloom.valid()) {
            std::vector<float> bl;
            for (size_t i = 0; i + 2 < S.bloom.v.size(); i += 3) bl.push_back(lum3(&S.bloom.v[i]));
            const float bp = std::max(percentile(bl, 0.999f), 1e-5f);
            m.add(12, "bloom after upsampling", m.upload(over(resized(fromPlane(S.bloom, [&](const float* p) {
                      return Vec3{std::sqrt(p[0] / bp), std::sqrt(p[1] / bp), std::sqrt(p[2] / bp)};
                  }), w, h), m.finalImg)));
        }
        m.add(12, "final frame", texFinal);
    }
    // 14 tone mapping
    {
        const float e = m.exposure * 1.6f;
        const Img linear = fromPlane(S.taa, [&](const float* p) { return Vec3{srgb(p[0] * e), srgb(p[1] * e), srgb(p[2] * e)}; });
        const Img aces = fromPlane(S.taa, [&](const float* p) {
            return Vec3{srgb(fx::acesFitted(p[0] * e)), srgb(fx::acesFitted(p[1] * e)), srgb(fx::acesFitted(p[2] * e))};
        });
        m.add(13, "linear clip | ACES curve", m.upload(linear), m.upload(aces), "LINEAR x EXPOSURE, CLIPPED", "ACES FILMIC");
        if (logLum.valid()) m.add(13, "HDR log2 luminance (false colour)", m.upload(logLum));
        m.add(13, "final frame (+ bloom, grading, grain)", texFinal);
        m.logHist.assign(56, 0);
        for (size_t i = 0; i + 2 < S.taa.v.size(); i += 3) {
            const float l = lum3(&S.taa.v[i]);
            if (!(l > 0.0f)) continue;
            const float t = (std::log2(l) - m.logHistLo) / (m.logHistHi - m.logHistLo);
            ++m.logHist[static_cast<size_t>(std::max(0, std::min(55, static_cast<int>(t * 56.0f))))];
        }
    }
    // 15 TAA
    if (m.noTaaImg.valid()) {
        const GLuint noTaa = m.upload(m.noTaaImg);
        m.add(14, "TAA off | on", noTaa, texFinal, "ONE JITTERED FRAME", "TAA (8+ FRAMES)");
        m.add(14, "one jittered frame (no TAA)", noTaa);
        m.add(14, "TAA result", texFinal);
        Img diff(w, h);
        for (size_t i = 0; i < diff.rgb.size(); ++i)
            diff.rgb[i] = static_cast<uint8_t>(std::min(255, 6 * std::abs(int(m.finalImg.rgb[i]) - int(m.noTaaImg.rgb[i]))));
        m.add(14, "difference (x6)", m.upload(diff));
    } else {
        m.add(14, "final frame (TAA off)", texFinal);
    }
    // 16 final + thermal + CV
    {
        m.add(15, "final frame", texFinal);
        if (m.thermalImg.valid()) {
            const GLuint th = m.upload(m.thermalImg);
            m.add(15, "thermal sensor view", th);
            // CV on the raw heat image, downsampled like the UAV camera (320 px wide)
            if (m.heatImg.valid()) {
                m.cvW = 320;
                m.cvH = std::max(1, 320 * h / w);
                const Img small = resized(m.heatImg, m.cvW, m.cvH);
                std::vector<uint8_t> heat(static_cast<size_t>(m.cvW) * m.cvH);
                for (size_t i = 0; i < heat.size(); ++i) heat[i] = small.rgb[i * 3];
                m.cvStats = {};
                m.blobs = cv::detectAll(heat, m.cvW, m.cvH, CvConfig{}, &m.cvStats);
                // Threshold mask over the thermal image
                // Otsu's split of the histogram (amber) and the detection threshold max(floor, Otsu + 1) (red)
                Img mask(w, h);
                const int t = m.cvStats.threshold, otsu = m.cvStats.otsu;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const int cx = std::min(m.cvW - 1, x * m.cvW / w), cy = std::min(m.cvH - 1, y * m.cvH / h);
                        const int hv = heat[static_cast<size_t>(cy) * m.cvW + cx];
                        uint8_t* p = mask.px(x, y);
                        const float g = hv / 255.0f * 0.45f;
                        const Vec3 c = hv >= t ? Vec3{1.0f, 0.22f, 0.18f} : hv > otsu ? Vec3{0.95f, 0.66f, 0.22f} * (0.55f + 0.45f * hv / 255.0f)
                                                                                        : Vec3{g * 0.6f, g * 0.7f, g};
                        p[0] = to8(c.x);
                        p[1] = to8(c.y);
                        p[2] = to8(c.z);
                    }
                m.add(15, "CV: Otsu split + blobs", m.upload(mask));
            }
            m.add(15, "visible | thermal", texFinal, th, "VISIBLE LIGHT", "THERMAL (LWIR)");
        }
    }

    mark("stage images 13-16");
    report(0.97f, "choosing the probe pixels");
    m.autoProbes(snap_);
    m.buildInterpInsets();
    m.traceRt(snap_);
    // The interpolation split runs through the middle of its probe triangle: the checker lines break at the seam.
    if (m.interpTri.valid) {
        const float cx = (m.interpTri.tr[0].pixel.x + m.interpTri.tr[1].pixel.x + m.interpTri.tr[2].pixel.x) / 3.0f;
        m.layers[4][0].split = clampf(cx / w, 0.22f, 0.45f);
    }
    char info[160];
    std::snprintf(info, sizeof(info), "frozen frame %ux%u  %s  t = %.1f s  %d leaves  %d triangles", static_cast<unsigned>(w),
                  static_cast<unsigned>(h), qualityName(s.quality), scene.time(), static_cast<int>(list.size()), m.trisInView);
    m.info = info;
    glBindFramebuffer(GL_FRAMEBUFFER, outerFbo);
    glViewport(outerViewport[0], outerViewport[1], outerViewport[2], outerViewport[3]);
    open_ = true;
    trans_ = 1.0f;
    anim_ = 0.0f;
    view_.layer = std::min(view_.layer, std::max(0, layerCount(view_.stage) - 1));
    openSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

}  // namespace ts

#include "rendering/FrameExplorerUi.inc"
