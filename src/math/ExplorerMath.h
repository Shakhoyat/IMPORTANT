// v7 WP-I Frame Explorer: the pure (GPU-free, unit-tested) part.
//   - the 16 stages of a frame (title, caption, key formula) in pipeline order
//   - rasterisation maths: edge functions, coverage (top-left rule), barycentric weights, affine vs
//     perspective-correct interpolation, signed screen area (back-face culling), the vertex pipeline
//     model -> world -> view -> clip -> NDC -> viewport
//   - image processing: convolution kernels, CPU convolution, histogram, gamma, Halton jitter
//   - the tone curve of the composite pass (ACES fit + sRGB) and the SSR ray march of the combine pass,
//     replayed on the CPU so the explorer can draw every step
//   - FrameSnapshot: float copies of the frozen frame's buffers + the pixel inspector that reads them
// The GPU side (render passes, overlay) is rendering/FrameExplorer.{h,cpp}.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/Math3D.h"

namespace ts::fx {

// ------------------------------------------------------------------------------------------------ stages
enum class Stage {
    SceneGraph = 0, Transform, Assembly, Raster, Interpolation, Depth, GBuffer, Shadow,
    Lighting, Reflection, Ssao, Fog, Bloom, ToneMap, Taa, Final, Count
};
constexpr int kStageCount = static_cast<int>(Stage::Count);

struct StageInfo {
    const char* shortName;  // timeline label (<= 8 characters)
    const char* title;
    const char* caption;    // what happens in this stage, plain words
    const char* formula;    // the key formula (ASCII: the HUD font is printable ASCII)
    const char* concepts;   // the core CG / image-processing concept it teaches
};
const StageInfo& stageInfo(Stage s);
inline const StageInfo& stageInfo(int i) { return stageInfo(static_cast<Stage>(i)); }

// ------------------------------------------------------------------------------------------------ raster maths
struct Vec2f {
    float x = 0.0f, y = 0.0f;
};

// Edge function of the directed edge a -> b at p: twice the signed area of (a, b, p). With y pointing up,
// > 0 = p is left of the edge (counter-clockwise side).
inline float edgeFunction(const Vec2f& a, const Vec2f& b, const Vec2f& p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
// Twice the signed area of the triangle (> 0 = counter-clockwise = front face in OpenGL's default).
inline float signedArea2(const Vec2f& a, const Vec2f& b, const Vec2f& c) { return edgeFunction(a, b, c); }

struct Barycentric {
    float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f;  // weights of v0, v1, v2 (sum 1)
    float e0 = 0.0f, e1 = 0.0f, e2 = 0.0f;  // raw edge functions E12(p), E20(p), E01(p)
    bool inside = false;                    // covered (top-left fill rule on shared edges)
};
// Coverage + barycentric weights of pixel point p for the screen triangle (v0, v1, v2) of either winding.
Barycentric rasterize(const Vec2f& v0, const Vec2f& v1, const Vec2f& v2, const Vec2f& p);

// Attribute interpolation from SCREEN-space barycentrics b. affine: sum b_i a_i. Perspective-correct: each
// attribute is divided by its clip w, interpolated, and divided by the interpolated 1/w.
float interpolateAffine(const Barycentric& b, float a0, float a1, float a2);
float interpolatePerspective(const Barycentric& b, float w0, float w1, float w2, float a0, float a1, float a2);

// One vertex through the pipeline (for the transform stage panel).
struct VertexTrace {
    Vec3 model, world, view;
    Vec4 clip;
    Vec3 ndc;
    Vec2f pixel;            // origin top-left, like the HUD
    float depth01 = 0.0f;   // window depth (glDepthRange 0..1)
    bool clipped = false;   // outside -w <= x, y, z <= w
};
VertexTrace traceVertex(const Vec3& model, const Mat4& world, const Mat4& view, const Mat4& proj, int width, int height);

// Window depth d (0..1) -> eye distance along -z, and back (OpenGL perspective projection).
float linearizeDepth(float d01, float nearZ, float farZ);
float depthFromDistance(float dist, float nearZ, float farZ);

// ------------------------------------------------------------------------------------------------ image processing
enum class KernelType { Identity = 0, Box, Gaussian, SobelX, SobelY, Sobel, Laplacian, Sharpen, Emboss, Count };
constexpr int kKernelCount = static_cast<int>(KernelType::Count);
struct Kernel {
    KernelType type = KernelType::Identity;
    const char* name = "";
    const char* note = "";
    int size = 3;            // 3 or 5 (odd, square)
    float w[25] = {};        // row-major, row 0 = top
    float bias = 0.0f;       // added after the sum (0.5 for signed edge kernels so 0 shows as mid-grey)
    bool magnitude = false;  // Sobel: |(Gx, Gy)| from the two Sobel kernels instead of one sum
};
Kernel kernel(KernelType t);
float kernelSum(const Kernel& k);

// Single-channel float image, rows top to bottom.
struct GrayImage {
    int w = 0, h = 0;
    std::vector<float> v;
    float at(int x, int y) const;  // clamp-to-edge
};
GrayImage convolve(const GrayImage& src, const Kernel& k);

// 'bins' luminance (Rec. 709) histogram of an RGB8 image (or channel 0..2 alone).
std::vector<int> histogram(const std::vector<uint8_t>& rgb, int bins, int channel = -1);
inline float applyGamma(float v, float gamma) { return std::pow(clampf(v, 0.0f, 1.0f), 1.0f / gamma); }

// Halton low-discrepancy sequence (the TAA jitter: bases 2 and 3, index k = 1..8).
float halton(unsigned index, unsigned base);

// ------------------------------------------------------------------------------------------------ tone curve
float acesFitted(float x);          // grey axis of the RRT + ODT fit used by the composite shader
float srgbEncode(float linear);     // IEC 61966-2-1
// Display value of linear HDR radiance x with the composite's exposure (x * exposure * 1.6 -> ACES -> sRGB).
float toneCurve(float x, float exposure);
// Exposure the composite pass applies: base * 2^clamp(key - meanLog2, -range, range).
float autoExposure(float base, float keyLog2, float meanLog2, float evRange);

// ------------------------------------------------------------------------------------------------ reflection
inline Vec3 reflectDir(const Vec3& d, const Vec3& n) { return d - n * (2.0f * dot(d, n)); }
inline float schlickFresnel(float f0, float cosTheta) {
    const float m = 1.0f - clampf(cosTheta, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * m * m * m * m * m;
}

// Read-only depth buffer view for the CPU ray march (raw window depth 0..1, rows bottom-up like GL).
struct DepthView {
    int w = 0, h = 0;
    const float* d = nullptr;
    float sample(float u, float v) const;  // nearest texel, uv in 0..1 (v = 0 at the bottom)
};
struct SsrStep {
    Vec3 world;
    Vec2f uv;           // projected position (0..1, v up)
    float rayDepth = 0.0f, sceneDepth = 0.0f;  // eye distances
    bool behind = false;  // ray below the depth buffer surface
};
struct SsrMarch {
    Vec3 origin, normal, view, reflected;
    std::vector<SsrStep> steps;
    bool hit = false;
    Vec2f hitUV;
    int hitStep = -1;
    const char* stop = "";  // why the march ended
};
// Same march as kCombineFrag's reflection(): world-space steps t = 0.04, t = 1.09 t + 0.03, a crossing within the
// thickness is refined by 6 bisections. uv = pixel centre (0..1, v up), normal = world normal of that pixel.
SsrMarch marchSsr(const DepthView& depth, const Mat4& viewProj, const Vec3& camPos, float nearZ, float farZ,
                  const Vec2f& uv, const Vec3& normal, int maxSteps);

// ------------------------------------------------------------------------------------------------ frame snapshot
// A float image of one internal buffer. Rows are bottom-up (OpenGL order), 'c' channels per texel.
struct Plane {
    int w = 0, h = 0, c = 0;
    std::vector<float> v;
    bool valid() const { return w > 0 && h > 0 && c > 0 && v.size() == static_cast<size_t>(w) * h * c; }
    // Nearest texel at normalised (u, v), v = 0 at the bottom; nullptr if the plane is empty.
    const float* at(float u, float v) const;
};

// The frozen frame: the renderer's buffers (Renderer::labSnapshot, after endFrame) plus the explorer's own passes.
struct FrameSnapshot {
    int w = 0, h = 0;
    // From the renderer
    Plane hdr;       // resolved HDR colour (linear radiance, before fog / reflections)
    Plane lit;       // + fog + SSR (TAA input)
    Plane taa;       // TAA output (the image the composite tone-maps)
    Plane surface;   // rgb world normal * 0.5 + 0.5, a reflectivity
    Plane depth;     // raw window depth 0..1
    Plane ao;        // ambient visibility (pre-pass resolution)
    Plane fog;       // rgb in-scatter, a transmittance (fog resolution)
    Plane bloom;     // bloom mip 0 after the upsample chain (quarter resolution)
    Mat4 view, proj, viewProj, viewProjJ;  // unjittered / jittered (depth buffer)
    Vec3 camPos;
    float nearZ = 0.05f, farZ = 600.0f;
    float jitterUV[2] = {0.0f, 0.0f};
    unsigned frameIndex = 0;
    bool hdrPipeline = false, taaOn = false, ssrOn = false, aoOn = false, fogOn = false;
    int ssrSteps = 48, shadowMode = 0, pcfTaps = 8, moonShadowRes = 0;
    Mat4 moonViewProj[2];
    float moonTexelWorld[2] = {0.0f, 0.0f};  // metres per shadow texel (normal-offset bias)
    float cascadeSplit = 12.0f;
    unsigned moonShadowTex = 0;   // GL handle of the renderer's moon shadow array (valid until its next frame)
    float stats[4] = {0, 0, 0, 0};  // adapted (min, max, mean, mean of squares) of log2 luminance
    int bloomMips = 0;
    // From the explorer's passes (FrameExplorer)
    Plane final;     // displayed sRGB 0..1
    Plane ids;       // r = leaf index + 1 (0 = sky), g = triangle index in its mesh, b = 1 front facing
    Plane albedo;    // material base colour
    Plane material;  // r roughness, g metalness, b emissive
    Plane light;     // r shadow (lit fraction), g N.L, b (N.H)^n, a ambient luminance
    Plane overdraw;  // r = fragments per pixel (depth test off)
    std::vector<std::string> leafNames;  // index -> "object / part (primitive)"
};

struct InspectRow {
    std::string label, value;
    Vec3 swatch;
    bool hasSwatch = false;
};
// Values of every buffer at pixel (px, py) (origin top-left, the snapshot's resolution).
std::vector<InspectRow> inspect(const FrameSnapshot& s, int px, int py);

}  // namespace ts::fx
