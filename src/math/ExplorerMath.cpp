// v7 WP-I Frame Explorer: stage table, raster / interpolation maths, kernels, tone curve, SSR march, inspector.
#include "math/ExplorerMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace ts::fx {

// ================================================================================================ stages
namespace {

const StageInfo kStages[kStageCount] = {
    {"GRAPH", "Scene graph -> world matrices",
     "The camp is a tree of nodes. Each leaf draws one unit primitive (cube, sphere, cylinder, cone); its world matrix is "
     "the product of the local translate-rotate-scale matrices from the root down to it. Colours = composite objects.",
     "M_world = M_parent * T * R * S          p_world = M_world * p_model",
     "hierarchical modelling, affine transforms, homogeneous coordinates"},
    {"VERTEX", "Vertex transform: model -> world -> view -> clip -> NDC -> pixels",
     "Every vertex is moved into the camera's frame (view matrix), projected (clip space), clipped against the view frustum "
     "(-w <= x, y, z <= w), divided by w (perspective divide) into normalised device coordinates and mapped to pixels.",
     "p_clip = P * V * M * p     ndc = p_clip.xyz / p_clip.w     x_px = (ndc.x + 1) / 2 * W",
     "view frustum, perspective projection, clipping, viewport transform"},
    {"ASSEMBLY", "Primitive assembly + back-face culling",
     "The index buffer groups vertices into triangles. A triangle whose projected corners wind clockwise shows its back to the "
     "camera and is dropped before rasterisation: about half of every closed mesh is never shaded.",
     "2A = sum_i (x_i * y_i+1 - x_i+1 * y_i)      A > 0: counter-clockwise = front      A < 0: culled",
     "indexed meshes, winding order, signed area"},
    {"RASTER", "Rasterisation: coverage, edge functions, barycentrics",
     "For each pixel centre p the three edge functions are evaluated. p is covered when all three agree in sign (the top-left "
     "rule settles shared edges). Divided by the triangle's area they are the barycentric weights w0, w1, w2.",
     "E_01(p) = (x1 - x0)(p.y - y0) - (y1 - y0)(p.x - x0)       w_i = E_jk(p) / 2A,  w0 + w1 + w2 = 1",
     "scan conversion, edge functions, barycentric coordinates"},
    {"INTERP", "Colour interpolation: affine vs perspective-correct, Gouraud vs Phong",
     "Vertex attributes are blended with the barycentric weights. Blending in screen space (affine) bends patterns on surfaces "
     "seen at an angle; dividing by w first keeps them straight. Gouraud lights the vertices and blends colours; Phong blends "
     "normals and lights every pixel.",
     "affine: a = sum w_i a_i        perspective-correct: a = sum(w_i a_i / w_clip_i) / sum(w_i / w_clip_i)",
     "attribute interpolation, perspective-correct mapping, shading models"},
    {"DEPTH", "Depth test: the Z-buffer and overdraw",
     "Every fragment's depth is compared with the depth already stored for its pixel: nearer wins, the rest is discarded. "
     "Depth is stored non-linearly (precision near the camera). The heat map counts fragments per pixel: all but one were wasted.",
     "pass if z_frag < z_buffer        z_ndc = (f + n) / (f - n) - 2 f n / ((f - n) d)",
     "hidden-surface removal, depth precision, overdraw"},
    {"G-BUF", "Surface buffers (G-buffer)",
     "The attributes lighting needs, stored per pixel: albedo (base colour), world normal, roughness / metalness and depth. "
     "The screen-space passes (SSAO, reflections, fog) read these images instead of the geometry.",
     "normal stored as n * 0.5 + 0.5        P_world = (V P)^-1 * (2 uv - 1, 2 d - 1, 1), then / w",
     "deferred data, encoding vectors in textures, reconstruction from depth"},
    {"SHADOW", "Shadow map: the scene seen from the light",
     "The scene is drawn once more from the moon / sun into a depth texture. A pixel is in shadow when its light-space depth "
     "is farther than what the light saw there; percentage-closer filtering (PCF) averages many such tests into a soft edge.",
     "lit(p) = 1/N * sum_k [ z_light(p) - bias <= shadowMap(uv_light(p) + o_k) ]",
     "two-pass rendering, light space, depth comparison, filtering"},
    {"LIGHT", "Lighting: ambient + diffuse + specular",
     "The key light split into its terms: ambient (sky fill), diffuse (Lambert, N.L) and specular (Blinn-Phong highlight around "
     "the half vector H). The renderer itself uses the physically based version: GGX, Smith geometry, Schlick Fresnel.",
     "I = k_a A + k_d (N.L) C s + k_s (N.H)^n C s       H = normalize(L + V),  s = shadow",
     "local illumination, Lambert's cosine law, Blinn-Phong, PBR"},
    {"REFLECT", "Reflection: mirror law and screen-space ray marching",
     "The reflected ray leaves at the angle it arrived (mirror law). Screen-space reflections march that ray through the depth "
     "buffer until it slips behind a surface, then reuse the colour already on screen there, weighted by Fresnel.",
     "R = D - 2 (D.N) N        F = F0 + (1 - F0)(1 - cos t)^5",
     "reflection vector, ray marching, Fresnel"},
    {"SSAO", "Screen-space ambient occlusion",
     "Points are sampled in a hemisphere around each pixel's normal; those that end up behind the depth buffer are occluded. "
     "Creases, corners and contact areas receive less sky light.",
     "AO = 1 - 1/N * sum_i [ depth(s_i) closer than s_i ] * range(s_i)",
     "Monte Carlo sampling, hemisphere integration, screen-space approximation"},
    {"FOG", "Fog and volumetric light",
     "Light scattered by the air is integrated along each view ray (marching through the shadow map gives light shafts). "
     "The surface colour is dimmed by the transmittance and the in-scattered light is added.",
     "T = exp(-sigma d)        L = L_surface * T + L_inscatter",
     "participating media, Beer-Lambert law, ray marching"},
    {"BLOOM", "Bloom: threshold, mip chain, blur",
     "Pixels brighter than a threshold leak light around them, like a real lens. The bright part is extracted, halved again and "
     "again with a 13-tap filter (a cheap wide blur), then the chain is upsampled with a 3x3 tent and added back.",
     "b = max(0, max(r,g,b) - T) (soft knee)     mip_k+1 = down13(mip_k)     bloom = sum_k tent(mip_k)",
     "image pyramids, separable / multi-scale blur, thresholding"},
    {"TONEMAP", "Tone mapping + exposure",
     "The HDR image spans many stops of brightness. Exposure (auto: from the mean log luminance) scales it, the ACES filmic "
     "curve compresses the highlights smoothly and the sRGB curve encodes it for the display.",
     "e = e0 * 2^(key - mean log2 L)       c = ACES(1.6 e L)        out = sRGB(c)",
     "dynamic range, exposure, tone curves, gamma encoding"},
    {"TAA", "Temporal anti-aliasing",
     "Every frame the projection moves by a sub-pixel Halton (2,3) offset. The history (reprojected, clipped to the current "
     "neighbourhood) is blended with the new frame: edges converge to a supersampled result over ~8 frames.",
     "h_t = lerp(c_t, clip(h_t-1), 0.9)        jitter_k = (H2(k) - 1/2, H3(k) - 1/2) pixel",
     "sampling, aliasing, supersampling over time, low-discrepancy sequences"},
    {"FINAL", "Final frame + thermal / computer vision",
     "The finished frame, and the same world through the UAV's thermal sensor: heat instead of light, sensor noise and the "
     "computer-vision stage on top: Otsu threshold, connected components, blob classes.",
     "t = max(floor, Otsu + 1)       blob = 8-connected component of (heat >= t)",
     "image formation, thresholding, segmentation"},
};

}  // namespace

const StageInfo& stageInfo(Stage s) {
    const int i = std::max(0, std::min(kStageCount - 1, static_cast<int>(s)));
    return kStages[i];
}

// ================================================================================================ raster maths
namespace {

// Top-left rule for a counter-clockwise triangle with y up: a "top" edge is horizontal and runs right to left,
// a "left" edge runs downwards.
bool topLeft(const Vec2f& a, const Vec2f& b) { return (a.y == b.y && b.x < a.x) || b.y < a.y; }

bool coveredCcw(const Vec2f& v0, const Vec2f& v1, const Vec2f& v2, const Vec2f& p) {
    const float e0 = edgeFunction(v1, v2, p), e1 = edgeFunction(v2, v0, p), e2 = edgeFunction(v0, v1, p);
    const auto ok = [](float e, const Vec2f& a, const Vec2f& b) { return e > 0.0f || (e == 0.0f && topLeft(a, b)); };
    return ok(e0, v1, v2) && ok(e1, v2, v0) && ok(e2, v0, v1);
}

}  // namespace

Barycentric rasterize(const Vec2f& v0, const Vec2f& v1, const Vec2f& v2, const Vec2f& p) {
    Barycentric b;
    b.e0 = edgeFunction(v1, v2, p);
    b.e1 = edgeFunction(v2, v0, p);
    b.e2 = edgeFunction(v0, v1, p);
    const float area2 = signedArea2(v0, v1, v2);
    if (area2 == 0.0f) return b;  // degenerate: covers nothing
    b.w0 = b.e0 / area2;
    b.w1 = b.e1 / area2;
    b.w2 = b.e2 / area2;
    b.inside = area2 > 0.0f ? coveredCcw(v0, v1, v2, p) : coveredCcw(v0, v2, v1, p);
    return b;
}

float interpolateAffine(const Barycentric& b, float a0, float a1, float a2) { return b.w0 * a0 + b.w1 * a1 + b.w2 * a2; }

float interpolatePerspective(const Barycentric& b, float w0, float w1, float w2, float a0, float a1, float a2) {
    const float num = b.w0 * a0 / w0 + b.w1 * a1 / w1 + b.w2 * a2 / w2;
    const float den = b.w0 / w0 + b.w1 / w1 + b.w2 / w2;
    return den != 0.0f ? num / den : 0.0f;
}

VertexTrace traceVertex(const Vec3& model, const Mat4& world, const Mat4& view, const Mat4& proj, int width, int height) {
    VertexTrace t;
    t.model = model;
    t.world = transformPoint(world, model);
    t.view = transformPoint(view, t.world);
    t.clip = proj * Vec4{t.view.x, t.view.y, t.view.z, 1.0f};
    const float w = t.clip.w;
    t.clipped = !(std::fabs(t.clip.x) <= w && std::fabs(t.clip.y) <= w && std::fabs(t.clip.z) <= w);
    const float iw = std::fabs(w) > 1e-12f ? 1.0f / w : 0.0f;
    t.ndc = {t.clip.x * iw, t.clip.y * iw, t.clip.z * iw};
    t.pixel.x = (t.ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
    t.pixel.y = (1.0f - (t.ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
    t.depth01 = t.ndc.z * 0.5f + 0.5f;
    return t;
}

float linearizeDepth(float d01, float nearZ, float farZ) {
    const float z = d01 * 2.0f - 1.0f;
    return 2.0f * nearZ * farZ / (farZ + nearZ - z * (farZ - nearZ));
}

float depthFromDistance(float dist, float nearZ, float farZ) {
    const float zNdc = (farZ + nearZ) / (farZ - nearZ) - 2.0f * farZ * nearZ / ((farZ - nearZ) * dist);
    return zNdc * 0.5f + 0.5f;
}

// ================================================================================================ image processing
Kernel kernel(KernelType t) {
    Kernel k;
    k.type = t;
    k.size = 3;
    auto set3 = [&k](std::initializer_list<float> v) {
        int i = 0;
        for (float x : v) k.w[i++] = x;
    };
    switch (t) {
        case KernelType::Identity:
            k.name = "identity";
            k.note = "the frame unchanged";
            set3({0, 0, 0, 0, 1, 0, 0, 0, 0});
            break;
        case KernelType::Box:
            k.name = "box blur 3x3";
            k.note = "mean of the 9 neighbours: low-pass, blocky";
            for (int i = 0; i < 9; ++i) k.w[i] = 1.0f / 9.0f;
            break;
        case KernelType::Gaussian: {
            k.name = "Gaussian 5x5";
            k.note = "binomial weights [1 4 6 4 1] x [1 4 6 4 1] / 256: smooth low-pass";
            k.size = 5;
            const float b[5] = {1, 4, 6, 4, 1};
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x) k.w[y * 5 + x] = b[y] * b[x] / 256.0f;
            break;
        }
        case KernelType::SobelX:
            k.name = "Sobel Gx";
            k.note = "horizontal gradient (vertical edges); 0 shown as grey";
            set3({-1, 0, 1, -2, 0, 2, -1, 0, 1});
            k.bias = 0.5f;
            break;
        case KernelType::SobelY:
            k.name = "Sobel Gy";
            k.note = "vertical gradient (horizontal edges); 0 shown as grey";
            set3({-1, -2, -1, 0, 0, 0, 1, 2, 1});
            k.bias = 0.5f;
            break;
        case KernelType::Sobel:
            k.name = "Sobel |G|";
            k.note = "edge strength sqrt(Gx^2 + Gy^2)";
            set3({-1, 0, 1, -2, 0, 2, -1, 0, 1});
            k.magnitude = true;
            break;
        case KernelType::Laplacian:
            k.name = "Laplacian";
            k.note = "second derivative: zero on ramps, strong at edges and points";
            set3({0, 1, 0, 1, -4, 1, 0, 1, 0});
            k.bias = 0.5f;
            break;
        case KernelType::Sharpen:
            k.name = "sharpen";
            k.note = "identity - Laplacian: boosts local contrast";
            set3({0, -1, 0, -1, 5, -1, 0, -1, 0});
            break;
        case KernelType::Emboss:
            k.name = "emboss";
            k.note = "directional gradient + identity: relief look";
            set3({-2, -1, 0, -1, 1, 1, 0, 1, 2});
            break;
        case KernelType::Count: break;
    }
    return k;
}

float kernelSum(const Kernel& k) {
    float s = 0.0f;
    for (int i = 0; i < k.size * k.size; ++i) s += k.w[i];
    return s;
}

float GrayImage::at(int x, int y) const {
    x = std::max(0, std::min(w - 1, x));
    y = std::max(0, std::min(h - 1, y));
    return v[static_cast<size_t>(y) * w + x];
}

GrayImage convolve(const GrayImage& src, const Kernel& k) {
    GrayImage out;
    out.w = src.w;
    out.h = src.h;
    out.v.assign(src.v.size(), 0.0f);
    const int r = k.size / 2;
    const Kernel gy = kernel(KernelType::SobelY);
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            float s = 0.0f, s2 = 0.0f;
            for (int j = -r; j <= r; ++j)
                for (int i = -r; i <= r; ++i) {
                    const float px = src.at(x + i, y + j);  // kernel row 0 = top = the row above (y - 1)
                    s += k.w[(j + r) * k.size + (i + r)] * px;
                    if (k.magnitude) s2 += gy.w[(j + r) * 3 + (i + r)] * px;
                }
            out.v[static_cast<size_t>(y) * src.w + x] = k.magnitude ? std::sqrt(s * s + s2 * s2) : s + k.bias;
        }
    return out;
}

std::vector<int> histogram(const std::vector<uint8_t>& rgb, int bins, int channel) {
    std::vector<int> hist(static_cast<size_t>(std::max(bins, 1)), 0);
    for (size_t i = 0; i + 2 < rgb.size(); i += 3) {
        // Rec. 709 luminance in integer arithmetic (grey stays exactly grey)
        const int v = channel >= 0 && channel < 3 ? rgb[i + static_cast<size_t>(channel)]
                                                  : (2126 * rgb[i] + 7152 * rgb[i + 1] + 722 * rgb[i + 2]) / 10000;
        const int b = std::min(bins - 1, v * bins / 256);
        ++hist[static_cast<size_t>(b)];
    }
    return hist;
}

float halton(unsigned index, unsigned base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

// ================================================================================================ tone curve
float acesFitted(float v) {
    const float a = v * (v + 0.0245786f) - 0.000090537f;
    const float b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return clampf(a / b, 0.0f, 1.0f);
}

float srgbEncode(float c) {
    c = clampf(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float toneCurve(float x, float exposure) { return srgbEncode(acesFitted(std::max(x, 0.0f) * exposure * 1.6f)); }

float autoExposure(float base, float keyLog2, float meanLog2, float evRange) {
    return base * std::exp2(clampf(keyLog2 - meanLog2, -evRange, evRange));
}

// ================================================================================================ reflection
float DepthView::sample(float u, float v) const {
    if (!d || w <= 0 || h <= 0) return 1.0f;
    const int x = std::max(0, std::min(w - 1, static_cast<int>(u * static_cast<float>(w))));
    const int y = std::max(0, std::min(h - 1, static_cast<int>(v * static_cast<float>(h))));
    return d[static_cast<size_t>(y) * w + x];
}

SsrMarch marchSsr(const DepthView& depth, const Mat4& viewProj, const Vec3& camPos, float nearZ, float farZ,
                  const Vec2f& uv, const Vec3& normal, int maxSteps) {
    SsrMarch m;
    const Mat4 inv = inverse(viewProj);
    const float d = depth.sample(uv.x, uv.y);
    const Vec4 p4 = inv * Vec4{uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, d * 2.0f - 1.0f, 1.0f};
    m.origin = Vec3{p4.x, p4.y, p4.z} / p4.w;
    m.normal = normalize(normal);
    m.view = normalize(m.origin - camPos);
    m.reflected = reflectDir(m.view, m.normal);
    if (d >= 1.0f) {
        m.stop = "sky pixel: nothing to reflect from";
        return m;
    }
    if (m.reflected.y < -0.2f) {
        m.stop = "reflected ray points into the ground";
        return m;
    }
    const auto project = [&viewProj](const Vec3& q, Vec2f& out) {
        const Vec4 c = viewProj * Vec4{q.x, q.y, q.z, 1.0f};
        if (c.w <= 0.05f) return -1.0f;
        out = {c.x / c.w * 0.5f + 0.5f, c.y / c.w * 0.5f + 0.5f};
        return c.w;
    };
    float t = 0.04f, prevT = 0.0f;
    const float dist0 = length(m.origin - camPos);
    m.stop = "ran out of steps";
    for (int i = 0; i < std::min(maxSteps, 96); ++i) {
        SsrStep s;
        s.world = m.origin + m.reflected * t;
        s.rayDepth = project(s.world, s.uv);
        if (s.rayDepth < 0.0f) {
            m.stop = "passed behind the camera";
            break;
        }
        if (s.uv.x < 0.0f || s.uv.y < 0.0f || s.uv.x > 1.0f || s.uv.y > 1.0f) {
            m.stop = "left the screen (no data there)";
            break;
        }
        s.sceneDepth = linearizeDepth(depth.sample(s.uv.x, s.uv.y), nearZ, farZ);
        const float thickness = 0.15f + 0.04f * t + 0.01f * dist0;
        s.behind = s.rayDepth > s.sceneDepth + 0.01f;
        m.steps.push_back(s);
        if (s.behind && s.rayDepth - s.sceneDepth < thickness && s.sceneDepth < farZ * 0.9f) {
            float a = prevT, b = t;
            for (int k = 0; k < 6; ++k) {
                const float mid = 0.5f * (a + b);
                Vec2f q;
                const float rz = project(m.origin + m.reflected * mid, q);
                if (rz > linearizeDepth(depth.sample(q.x, q.y), nearZ, farZ)) b = mid;
                else a = mid;
            }
            project(m.origin + m.reflected * b, m.hitUV);
            m.hit = true;
            m.hitStep = i;
            m.stop = "hit: the ray crossed the depth buffer";
            break;
        }
        prevT = t;
        t = t * 1.09f + 0.03f;
    }
    return m;
}

// ================================================================================================ snapshot
const float* Plane::at(float u, float v) const {
    if (!valid()) return nullptr;
    const int x = std::max(0, std::min(w - 1, static_cast<int>(u * static_cast<float>(w))));
    const int y = std::max(0, std::min(h - 1, static_cast<int>(v * static_cast<float>(h))));
    return &this->v[(static_cast<size_t>(y) * w + x) * c];
}

namespace {

std::string f3(const char* fmt, float a, float b, float c) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c);
    return buf;
}
std::string f1(const char* fmt, float a) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), fmt, a);
    return buf;
}
float lum(const float* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

}  // namespace

std::vector<InspectRow> inspect(const FrameSnapshot& s, int px, int py) {
    std::vector<InspectRow> rows;
    if (s.w <= 0 || s.h <= 0) return rows;
    px = std::max(0, std::min(s.w - 1, px));
    py = std::max(0, std::min(s.h - 1, py));
    const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(s.w);
    const float v = 1.0f - (static_cast<float>(py) + 0.5f) / static_cast<float>(s.h);
    const auto add = [&rows](const std::string& l, const std::string& val, const Vec3* sw = nullptr) {
        InspectRow r;
        r.label = l;
        r.value = val;
        if (sw) {
            r.swatch = *sw;
            r.hasSwatch = true;
        }
        rows.push_back(r);
    };
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%d, %d)", px, py);
    add("pixel", buf);
    if (const float* p = s.ids.at(u, v)) {
        const int leaf = static_cast<int>(std::lround(p[0])) - 1;
        if (leaf < 0) add("object", "sky (no geometry)");
        else {
            add("object", leaf < static_cast<int>(s.leafNames.size()) ? s.leafNames[static_cast<size_t>(leaf)] : "leaf");
            std::snprintf(buf, sizeof(buf), "#%d, %s", static_cast<int>(std::lround(p[1])), p[2] > 0.5f ? "front face" : "back face");
            add("triangle", buf);
        }
    }
    if (const float* p = s.final.at(u, v)) {
        const Vec3 c{p[0], p[1], p[2]};
        add("final sRGB", f3("%3.0f %3.0f %3.0f", p[0] * 255.0f, p[1] * 255.0f, p[2] * 255.0f), &c);
    }
    if (const float* p = s.hdr.at(u, v)) {
        const float l = lum(p);
        add("HDR radiance", f3("%.4f %.4f %.4f", p[0], p[1], p[2]));
        add("luminance", f1("%.4f", l) + f1("  (%+.1f EV)", std::log2(std::max(l, 1e-6f))));
    }
    if (const float* p = s.taa.at(u, v)) add("after TAA", f1("L = %.4f", lum(p)));
    if (const float* p = s.depth.at(u, v)) {
        if (p[0] >= 1.0f) add("depth", "1.0 (far plane: sky)");
        else add("depth", f1("%.6f", p[0]) + f1("  = %.2f m", linearizeDepth(p[0], s.nearZ, s.farZ)));
    }
    if (const float* p = s.surface.at(u, v)) {
        const Vec3 n{p[0] * 2.0f - 1.0f, p[1] * 2.0f - 1.0f, p[2] * 2.0f - 1.0f};
        const Vec3 c{p[0], p[1], p[2]};
        add("world normal", f3("%+.2f %+.2f %+.2f", n.x, n.y, n.z), &c);
        add("reflectivity", f1("%.3f", p[3]));
    }
    if (const float* p = s.albedo.at(u, v)) {
        const Vec3 c{p[0], p[1], p[2]};
        add("albedo", f3("%.2f %.2f %.2f", p[0], p[1], p[2]), &c);
    }
    if (const float* p = s.material.at(u, v)) add("rough / metal", f3("%.2f / %.2f  emit %.2f", p[0], p[1], p[2]));
    if (const float* p = s.light.at(u, v)) add("shadow, N.L", f3("lit %.2f   N.L %.2f   spec %.2f", p[0], p[1], p[2]));
    if (const float* p = s.ao.at(u, v)) add("SSAO", f1("%.3f visible sky", p[0]));
    if (const float* p = s.fog.at(u, v)) add("fog", f1("T = %.3f", p[3]) + f1("   in-scatter L = %.4f", lum(p)));
    if (const float* p = s.bloom.at(u, v)) add("bloom", f1("L = %.4f", lum(p)));
    if (const float* p = s.overdraw.at(u, v)) add("overdraw", f1("%.0f fragments", p[0]));
    return rows;
}

}  // namespace ts::fx
