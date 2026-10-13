// v7 WP-I Frame Explorer maths (T280-T299). CPU only: math/ExplorerMath (stage table, rasterisation, interpolation,
// vertex pipeline, kernels, tone curve, SSR march, pixel inspector) against hand-made references.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "math/ExplorerMath.h"
#include "primitives/Cube.h"
#include "raytrace/RayTracer.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

uint32_t g_rng = 12345u;
float rnd(float lo, float hi) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * static_cast<float>((g_rng >> 8) & 0xFFFFFF) / 16777215.0f;
}

bool printableAscii(const char* s) {
    for (; *s; ++s)
        if (*s < 32 || *s > 126) return false;
    return true;
}

// The full ACES fit of kCompositeFrag (input / output matrices, rational RRT + ODT) applied to an RGB triple.
Vec3 acesMatrix(const Vec3& c) {
    const float in[9] = {0.59719f, 0.07600f, 0.02840f, 0.35458f, 0.90834f, 0.13383f, 0.04823f, 0.01566f, 0.83777f};
    const float out[9] = {1.60475f, -0.10208f, -0.00327f, -0.53108f, 1.10813f, -0.07276f, -0.07367f, -0.00605f, 1.07602f};
    const auto mul3 = [](const float* m, const Vec3& v) {  // column-major mat3 * v
        return Vec3{m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z, m[2] * v.x + m[5] * v.y + m[8] * v.z};
    };
    Vec3 v = mul3(in, c);
    const auto rrt = [](float x) { return (x * (x + 0.0245786f) - 0.000090537f) / (x * (0.983729f * x + 0.4329510f) + 0.238081f); };
    v = {rrt(v.x), rrt(v.y), rrt(v.z)};
    v = mul3(out, v);
    return {clampf(v.x, 0, 1), clampf(v.y, 0, 1), clampf(v.z, 0, 1)};
}

}  // namespace

TS_TEST_GROUP(explorer) {
    using namespace ts::fx;

    // ---- T280: the stage list -------------------------------------------------------------------------------------
    {
        std::set<std::string> shorts;
        bool ok = kStageCount == 16;
        for (int i = 0; i < kStageCount; ++i) {
            const StageInfo& s = stageInfo(i);
            ok = ok && s.shortName && s.title && s.caption && s.formula && s.concepts;
            ok = ok && std::string(s.shortName).size() <= 8 && std::string(s.caption).size() > 80 && std::string(s.formula).size() > 10;
            ok = ok && printableAscii(s.title) && printableAscii(s.caption) && printableAscii(s.formula) && printableAscii(s.concepts);
            shorts.insert(s.shortName);
        }
        const bool order = std::string(stageInfo(Stage::Raster).shortName) == "RASTER" &&
                           std::string(stageInfo(Stage::Interpolation).shortName) == "INTERP" &&
                           std::string(stageInfo(Stage::Reflection).shortName) == "REFLECT" &&
                           std::string(stageInfo(Stage::Final).shortName) == "FINAL";
        record("T280", "Frame Explorer stage list: 16 pipeline stages in order, each with title, caption, formula, concepts (HUD-printable ASCII)",
               "16 stages, unique short names, all texts present and printable", fmt("%d stages, %zu unique, order %s", kStageCount, shorts.size(), order ? "ok" : "wrong"),
               ok && shorts.size() == 16 && order);
    }

    // ---- T281: edge functions -> barycentric weights reconstruct the point -----------------------------------------
    {
        float maxSum = 0.0f, maxRec = 0.0f;
        int signMismatch = 0;
        for (int t = 0; t < 2000; ++t) {
            const Vec2f a{rnd(0, 100), rnd(0, 100)}, b{rnd(0, 100), rnd(0, 100)}, c{rnd(0, 100), rnd(0, 100)};
            if (std::fabs(signedArea2(a, b, c)) < 20.0f) continue;
            const Vec2f p{rnd(-20, 120), rnd(-20, 120)};
            const Barycentric w = rasterize(a, b, c, p);
            maxSum = std::max(maxSum, std::fabs(w.w0 + w.w1 + w.w2 - 1.0f));
            const float rx = w.w0 * a.x + w.w1 * b.x + w.w2 * c.x, ry = w.w0 * a.y + w.w1 * b.y + w.w2 * c.y;
            maxRec = std::max(maxRec, std::max(std::fabs(rx - p.x), std::fabs(ry - p.y)));
            const bool allPos = w.w0 > 1e-4f && w.w1 > 1e-4f && w.w2 > 1e-4f, anyNeg = w.w0 < -1e-4f || w.w1 < -1e-4f || w.w2 < -1e-4f;
            if ((allPos && !w.inside) || (anyNeg && w.inside)) ++signMismatch;
        }
        record("T281", "Edge functions / 2A are barycentric weights: sum 1, reconstruct p, inside <=> all weights >= 0 (both windings)",
               "|sum - 1| < 1e-4, reconstruction < 1e-2 px, 0 coverage mismatches",
               fmt("sum err %.2g, reconstruction err %.2g px, %d mismatches", maxSum, maxRec, signMismatch),
               maxSum < 1e-4f && maxRec < 1e-2f && signMismatch == 0);
    }

    // ---- T282: top-left rule: shared edges are rasterised exactly once ---------------------------------------------
    {
        // A 16 x 16 square split along its diagonal plus a fan around an interior point: pixel centres lie exactly on edges.
        const Vec2f q0{0, 0}, q1{16, 0}, q2{16, 16}, q3{0, 16}, m{8, 8};
        const Vec2f tris[6][3] = {{q0, q1, q2}, {q0, q2, q3}, {q0, q1, m}, {q1, q2, m}, {q2, q3, m}, {q3, q0, m}};
        int twice = 0, never = 0, twiceFan = 0, neverFan = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const Vec2f p{x + 0.5f, y + 0.5f}, e{static_cast<float>(x), static_cast<float>(y)};  // centre and corner (on edges)
                for (const Vec2f& pt : {p, e}) {
                    int n = 0, nf = 0;
                    for (int t = 0; t < 2; ++t) n += rasterize(tris[t][0], tris[t][1], tris[t][2], pt).inside;
                    for (int t = 2; t < 6; ++t) nf += rasterize(tris[t][0], tris[t][1], tris[t][2], pt).inside;
                    if (n > 1) ++twice;
                    if (n == 0 && pt.x > 0 && pt.y > 0 && pt.x < 16 && pt.y < 16) ++never;
                    if (nf > 1) ++twiceFan;
                    if (nf == 0 && pt.x > 0 && pt.y > 0 && pt.x < 16 && pt.y < 16) ++neverFan;
                }
            }
        record("T282", "Top-left fill rule: interior points on shared edges / vertices are covered by exactly one triangle (2-split and 4-fan)",
               "0 double, 0 missing", fmt("split: %d double %d missing; fan: %d double %d missing", twice, never, twiceFan, neverFan),
               twice == 0 && never == 0 && twiceFan == 0 && neverFan == 0);
    }

    // ---- T283: perspective-correct interpolation == ray / plane intersection (CPU reference); affine is not --------
    {
        const Mat4 view = lookAt({0, 1.6f, 0}, {0, 0.2f, -10}, {0, 1, 0});
        const Mat4 proj = perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 200.0f);
        const Vec3 A{-6, 0, -2}, B{6, 0, -2}, C{0, 0, -60};  // a long ground triangle, steeply foreshortened
        const VertexTrace ta = traceVertex(A, Mat4::identity(), view, proj, 1280, 720), tb = traceVertex(B, Mat4::identity(), view, proj, 1280, 720),
                          tc = traceVertex(C, Mat4::identity(), view, proj, 1280, 720);
        const Mat4 invVP = inverse(proj * view);
        float maxPersp = 0.0f, maxAffine = 0.0f;
        int samples = 0;
        for (int y = 0; y < 720; y += 24)
            for (int x = 0; x < 1280; x += 32) {
                const Vec2f p{x + 0.5f, y + 0.5f};
                const Barycentric b = rasterize(ta.pixel, tb.pixel, tc.pixel, p);
                if (!b.inside) continue;
                // reference: the pixel's view ray hits the plane y = 0
                const float nx = p.x / 1280 * 2 - 1, ny = 1 - p.y / 720 * 2;
                const Vec4 n4 = invVP * Vec4{nx, ny, -1, 1}, f4 = invVP * Vec4{nx, ny, 1, 1};
                const Vec3 o = Vec3{n4.x, n4.y, n4.z} / n4.w, f = Vec3{f4.x, f4.y, f4.z} / f4.w;
                const Vec3 d = f - o;
                const float t = -o.y / d.y;
                const Vec3 hit = o + d * t;
                const float pz = interpolatePerspective(b, ta.clip.w, tb.clip.w, tc.clip.w, A.z, B.z, C.z);
                const float az = interpolateAffine(b, A.z, B.z, C.z);
                maxPersp = std::max(maxPersp, std::fabs(pz - hit.z));
                maxAffine = std::max(maxAffine, std::fabs(az - hit.z));
                ++samples;
            }
        record("T283", "Perspective-correct interpolation (divide by clip w) matches the ray / plane intersection; affine does not",
               "perspective error < 0.02 m, affine error > 1 m", fmt("%d pixels: perspective %.4f m, affine %.2f m", samples, maxPersp, maxAffine),
               samples > 50 && maxPersp < 0.02f && maxAffine > 1.0f);
    }

    // ---- T284: both rules agree at the vertices and on constants ------------------------------------------------------
    {
        const Vec2f a{10, 10}, b{200, 30}, c{60, 180};
        float worst = 0.0f;
        const Vec2f vs[3] = {a, b, c};
        const float attr[3] = {3.0f, -2.0f, 7.5f}, w[3] = {1.5f, 9.0f, 40.0f};
        for (int i = 0; i < 3; ++i) {
            const Barycentric bb = rasterize(a, b, c, vs[i]);
            worst = std::max(worst, std::fabs(interpolateAffine(bb, attr[0], attr[1], attr[2]) - attr[i]));
            worst = std::max(worst, std::fabs(interpolatePerspective(bb, w[0], w[1], w[2], attr[0], attr[1], attr[2]) - attr[i]));
        }
        const Barycentric mid = rasterize(a, b, c, {80, 70});
        worst = std::max(worst, std::fabs(interpolatePerspective(mid, w[0], w[1], w[2], 4.2f, 4.2f, 4.2f) - 4.2f));
        worst = std::max(worst, std::fabs(interpolateAffine(mid, 4.2f, 4.2f, 4.2f) - 4.2f));
        record("T284", "Affine and perspective-correct interpolation reproduce vertex values and constant attributes",
               "error < 1e-4", fmt("worst %.2g", worst), worst < 1e-4f);
    }

    // ---- T285: vertex pipeline trace ----------------------------------------------------------------------------------
    {
        const Mat4 world = translate({2, 1, -3}) * rotateEulerYXZ({10, 35, 0}) * scale({2, 1, 3});
        const Mat4 view = lookAt({4, 5, 8}, {0, 0, 0}, {0, 1, 0});
        const Mat4 proj = perspective(radians(50.0f), 1.6f, 0.1f, 300.0f);
        const Vec3 m{0.5f, -0.5f, 0.5f};
        const VertexTrace t = traceVertex(m, world, view, proj, 1600, 1000);
        const Vec4 ref = (proj * view * world) * Vec4{m.x, m.y, m.z, 1};
        const float clipErr = std::max({std::fabs(ref.x - t.clip.x), std::fabs(ref.y - t.clip.y), std::fabs(ref.z - t.clip.z), std::fabs(ref.w - t.clip.w)});
        const float px = (ref.x / ref.w + 1) * 0.5f * 1600, py = (1 - (ref.y / ref.w + 1) * 0.5f) * 1000;
        const VertexTrace behind = traceVertex({0, 0, 0}, translate({0, 0, 20}), view, proj, 1600, 1000);
        const VertexTrace centre = traceVertex({0, 0, 0}, Mat4::identity(), view, proj, 1600, 1000);
        record("T285", "Vertex trace model -> world -> view -> clip -> NDC -> pixel equals P V M p; points behind the eye are clipped",
               "clip error < 1e-4, pixel error < 0.01, look-at target at the screen centre, behind = clipped",
               fmt("clip %.2g, pixel (%.2f, %.2f) vs (%.2f, %.2f), centre (%.1f, %.1f), behind clipped %d", clipErr, t.pixel.x, t.pixel.y, px, py,
                   centre.pixel.x, centre.pixel.y, behind.clipped),
               clipErr < 1e-4f && std::fabs(px - t.pixel.x) < 0.01f && std::fabs(py - t.pixel.y) < 0.01f && behind.clipped && !centre.clipped &&
                   std::fabs(centre.pixel.x - 800) < 0.01f && std::fabs(centre.pixel.y - 500) < 0.01f);
    }

    // ---- T286: depth linearisation ------------------------------------------------------------------------------------
    {
        const float n = 0.05f, f = 600.0f;
        float worst = 0.0f;
        for (float d : {0.06f, 0.5f, 3.0f, 12.0f, 77.0f, 420.0f}) worst = std::max(worst, std::fabs(linearizeDepth(depthFromDistance(d, n, f), n, f) - d) / d);
        const float dn = depthFromDistance(n, n, f), df = depthFromDistance(f, n, f);
        // window depth of a point 10 m away, straight from the projection matrix
        const Mat4 P = perspective(radians(60.0f), 1.0f, n, f);
        const Vec4 c = P * Vec4{0, 0, -10, 1};
        const float d10 = (c.z / c.w) * 0.5f + 0.5f;
        record("T286", "Depth: window depth <-> eye distance round trip; near -> 0, far -> 1; matches the projection matrix",
               "relative error < 1e-3", fmt("round trip %.2g, d(near) %.5f, d(far) %.5f, d(10 m) %.6f vs %.6f", worst, dn, df, depthFromDistance(10, n, f), d10),
               worst < 1e-3f && std::fabs(dn) < 1e-5f && std::fabs(df - 1) < 1e-5f && std::fabs(depthFromDistance(10, n, f) - d10) < 1e-5f);
    }

    // ---- T287: convolution kernels: sums / normalisation ----------------------------------------------------------------
    {
        std::string obs;
        bool ok = true;
        for (int i = 0; i < kKernelCount; ++i) {
            const Kernel k = kernel(static_cast<KernelType>(i));
            const float s = kernelSum(k);
            const bool zeroSum = k.type == KernelType::SobelX || k.type == KernelType::SobelY || k.type == KernelType::Sobel || k.type == KernelType::Laplacian;
            ok = ok && std::fabs(s - (zeroSum ? 0.0f : 1.0f)) < 1e-5f && k.name[0] && (k.size == 3 || k.size == 5);
            obs += fmt("%s=%.3g ", k.name, s);
        }
        const Kernel g = kernel(KernelType::Gaussian);
        bool sym = true;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) sym = sym && g.w[y * 5 + x] == g.w[x * 5 + y] && g.w[y * 5 + x] == g.w[(4 - y) * 5 + (4 - x)];
        record("T287", "Image kernels: blur / sharpen / emboss / identity sum to 1 (brightness kept), Sobel / Laplacian sum to 0; Gaussian symmetric",
               "sums 1 or 0, Gaussian symmetric", obs + (sym ? "| symmetric" : "| NOT symmetric"), ok && sym);
    }

    // ---- T288: CPU convolution behaviour --------------------------------------------------------------------------------
    {
        GrayImage step, ramp, flat;
        for (GrayImage* g : {&step, &ramp, &flat}) {
            g->w = 16;
            g->h = 12;
            g->v.resize(16 * 12);
        }
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 16; ++x) {
                step.v[y * 16 + x] = x < 8 ? 0.0f : 1.0f;
                ramp.v[y * 16 + x] = 0.05f * x + 0.02f * y;
                flat.v[y * 16 + x] = 0.37f;
            }
        const GrayImage id = convolve(step, kernel(KernelType::Identity));
        const GrayImage box = convolve(flat, kernel(KernelType::Box));
        const GrayImage sob = convolve(step, kernel(KernelType::Sobel));
        const GrayImage lap = convolve(ramp, kernel(KernelType::Laplacian));
        float idErr = 0, boxErr = 0, lapErr = 0, edge = 0, away = 0;
        for (size_t i = 0; i < step.v.size(); ++i) {
            idErr = std::max(idErr, std::fabs(id.v[i] - step.v[i]));
            boxErr = std::max(boxErr, std::fabs(box.v[i] - 0.37f));
        }
        for (int y = 1; y < 11; ++y)
            for (int x = 1; x < 15; ++x) lapErr = std::max(lapErr, std::fabs(lap.v[y * 16 + x] - kernel(KernelType::Laplacian).bias));
        edge = sob.v[5 * 16 + 8];
        away = std::max(sob.v[5 * 16 + 3], sob.v[5 * 16 + 12]);
        record("T288", "Convolution: identity keeps the image, box keeps a flat image, Sobel fires only at a step edge, Laplacian of a ramp is 0",
               "errors < 1e-5, Sobel |G| = 4 at the edge and 0 away", fmt("id %.2g, box %.2g, lap %.2g, sobel edge %.2f away %.2f", idErr, boxErr, lapErr, edge, away),
               idErr < 1e-5f && boxErr < 1e-5f && lapErr < 1e-5f && std::fabs(edge - 4.0f) < 1e-4f && away < 1e-5f);
    }

    // ---- T289: histogram -----------------------------------------------------------------------------------------------
    {
        std::vector<uint8_t> rgb;
        for (int i = 0; i < 256; ++i) rgb.insert(rgb.end(), {static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i)});
        rgb.insert(rgb.end(), {255, 0, 0});
        const std::vector<int> lum = histogram(rgb, 64), red = histogram(rgb, 64, 0);
        int sum = 0;
        bool even = true;
        for (int i = 0; i < 64; ++i) {
            sum += lum[static_cast<size_t>(i)];
            if (i != 13) even = even && lum[static_cast<size_t>(i)] == 4;  // pure red has luminance 0.2126 * 255 = 54 -> bin 13
        }
        record("T289", "Histogram: a grey ramp fills 64 bins evenly; counts sum to the pixel count; channel histograms",
               "257 pixels, 4 per bin (+1 in bin 13), red bin 63 = 5", fmt("sum %d, even %d, bin13 %d, red[63] %d", sum, even, lum[13], red[63]),
               sum == 257 && even && lum[13] == 5 && red[63] == 5);
    }

    // ---- T290: Halton jitter -------------------------------------------------------------------------------------------
    {
        const float h2[4] = {0.5f, 0.25f, 0.75f, 0.125f}, h3[3] = {1 / 3.0f, 2 / 3.0f, 1 / 9.0f};
        float err = 0.0f;
        for (int i = 0; i < 4; ++i) err = std::max(err, std::fabs(halton(i + 1, 2) - h2[i]));
        for (int i = 0; i < 3; ++i) err = std::max(err, std::fabs(halton(i + 1, 3) - h3[i]));
        // 8 TAA offsets: inside the pixel, mean near the centre
        float mx = 0, my = 0;
        bool inside = true;
        for (unsigned k = 1; k <= 8; ++k) {
            mx += halton(k, 2) - 0.5f;
            my += halton(k, 3) - 0.5f;
            inside = inside && halton(k, 2) > 0 && halton(k, 2) < 1 && halton(k, 3) > 0 && halton(k, 3) < 1;
        }
        // the offset moves a point by the same sub-pixel step at every depth, under perspective and parallel projection
        const float jx = 0.5f * 2.0f / 1920.0f, jy = -0.25f * 2.0f / 1080.0f;
        float shift = 0.0f;
        for (int k = 0; k < 2; ++k) {
            const Mat4 p0 = k == 0 ? perspective(radians(50.0f), 16.0f / 9.0f, 0.05f, 600.0f) : ortho(-53.3f, 53.3f, -30.0f, 30.0f, 110.0f, 190.0f);
            Mat4 p1 = p0;
            jitterProjection(p1, jx, jy);
            for (float z : {-111.0f, -150.0f, -189.0f}) {
                const Vec4 a = p0 * Vec4{7.0f, -3.0f, z, 1.0f}, b = p1 * Vec4{7.0f, -3.0f, z, 1.0f};
                shift = std::max(shift, std::fabs(b.x / b.w - a.x / a.w + jx));
                shift = std::max(shift, std::fabs(b.y / b.w - a.y / a.w + jy));
            }
        }
        record("T290", "Halton (2,3) sequence of the TAA jitter: textbook values, 8 offsets inside the pixel, centred; the projection offset is depth-free",
               "error < 1e-6, |mean| < 0.1, shift error < 1e-6", fmt("err %.2g, mean (%.3f, %.3f), shift err %.2g", err, mx / 8, my / 8, shift),
               err < 1e-6f && inside && std::fabs(mx / 8) < 0.1f && std::fabs(my / 8) < 0.1f && shift < 1e-6f);
    }

    // ---- T291: tone curve monotonic, bounded; exposure clamp -----------------------------------------------------------
    {
        bool mono = true, bounded = true;
        float prev = -1.0f;
        for (int i = 0; i <= 400; ++i) {
            const float L = std::exp2(-14.0f + 22.0f * i / 400.0f);
            const float v = toneCurve(L, 1.7f);
            mono = mono && v >= prev - 1e-6f;
            bounded = bounded && v >= 0.0f && v <= 1.0f;
            prev = v;
        }
        float prevS = -1.0f;
        bool srgbMono = true;
        for (int i = 0; i <= 1000; ++i) {
            const float s = srgbEncode(i / 1000.0f);
            srgbMono = srgbMono && s >= prevS;
            prevS = s;
        }
        const float jump = std::fabs(srgbEncode(0.0031309f) - srgbEncode(0.0031307f));
        const float eUp = autoExposure(1.0f, -5.2f, -12.0f, 1.5f), eDown = autoExposure(1.0f, -5.2f, 3.0f, 1.5f), eMid = autoExposure(2.0f, -5.2f, -5.7f, 1.5f);
        record("T291", "Tone curve (exposure x 1.6 -> ACES -> sRGB) is monotonic and in [0, 1]; sRGB continuous; auto-exposure clamped to +-1.5 EV",
               "monotonic, bounded, sRGB jump < 1e-4, exposure 2^1.5 / 2^-1.5 / 2 * 2^0.5",
               fmt("mono %d bounded %d top %.3f, sRGB mono %d jump %.2g, e %.3f %.3f %.3f", mono, bounded, prev, srgbMono, jump, eUp, eDown, eMid),
               mono && bounded && prev > 0.95f && srgbMono && jump < 1e-4f && std::fabs(eUp - std::exp2(1.5f)) < 1e-4f &&
                   std::fabs(eDown - std::exp2(-1.5f)) < 1e-4f && std::fabs(eMid - 2.0f * std::exp2(0.5f)) < 1e-4f);
    }

    // ---- T292: the grey-axis ACES of the plot == the shader's matrix version on grey -----------------------------------
    {
        float worst = 0.0f;
        for (float x : {0.001f, 0.01f, 0.05f, 0.18f, 0.5f, 1.0f, 2.0f, 8.0f, 30.0f}) {
            const Vec3 m = acesMatrix({x, x, x});
            worst = std::max({worst, std::fabs(m.x - acesFitted(x)), std::fabs(m.y - acesFitted(x)), std::fabs(m.z - acesFitted(x))});
        }
        record("T292", "Plotted tone curve = the composite shader's ACES fit (input / output matrices) on the grey axis",
               "difference < 1e-3", fmt("worst %.2g", worst), worst < 1e-3f);
    }

    // ---- T293: mirror law, Fresnel, SSR march on a synthetic depth buffer ------------------------------------------------
    {
        float lawErr = 0.0f;
        for (int i = 0; i < 200; ++i) {
            const Vec3 n = normalize(Vec3{rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)});
            Vec3 d = normalize(Vec3{rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)});
            if (dot(d, n) > 0) d = -d;
            const Vec3 r = reflectDir(d, n);
            // unit length, angle in = angle out, R in the plane of D and N (N x R = N x D)
            lawErr = std::max({lawErr, std::fabs(length(r) - 1.0f), std::fabs(dot(r, n) + dot(d, n)), length(cross(n, d) - cross(n, r))});
        }
        const float f0 = schlickFresnel(0.04f, 1.0f), f90 = schlickFresnel(0.04f, 0.0f);
        // Scene: camera at (0, 2, 0) looking down -z at a floor (y = 0) and a wall z = -12. Depth buffer from analytic hits.
        const int W = 1280, H = 720;
        const Vec3 eye{0, 2, 0};
        const Mat4 view = lookAt(eye, {0, 1.0f, -10}, {0, 1, 0});
        const Mat4 proj = perspective(radians(60.0f), static_cast<float>(W) / H, 0.1f, 200.0f);
        const Mat4 vp = proj * view, inv = inverse(vp);
        std::vector<float> depth(static_cast<size_t>(W) * H, 1.0f);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float nx = (x + 0.5f) / W * 2 - 1, ny = (y + 0.5f) / H * 2 - 1;  // rows bottom-up
                const Vec4 a = inv * Vec4{nx, ny, -1, 1}, b = inv * Vec4{nx, ny, 1, 1};
                const Vec3 o = Vec3{a.x, a.y, a.z} / a.w, dir = normalize(Vec3{b.x, b.y, b.z} / b.w - o);
                float t = 1e9f;
                if (dir.y < -1e-4f) t = std::min(t, -o.y / dir.y);
                if (dir.z < -1e-4f) {
                    const float tw = (-12.0f - o.z) / dir.z;
                    if ((o + dir * tw).y >= 0.0f) t = std::min(t, tw);
                }
                if (t < 1e8f) {
                    const Vec3 p = o + dir * t;
                    const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1};
                    depth[static_cast<size_t>(y) * W + x] = c.z / c.w * 0.5f + 0.5f;
                }
            }
        DepthView dv{W, H, depth.data()};
        const Vec4 fp = vp * Vec4{0, 0, -5, 1};  // a floor point 5 m ahead
        const Vec2f uv{fp.x / fp.w * 0.5f + 0.5f, fp.y / fp.w * 0.5f + 0.5f};
        const SsrMarch m = marchSsr(dv, vp, eye, 0.1f, 200.0f, uv, {0, 1, 0}, 64);
        // the mirror image of the eye in the floor sees the wall: expected hit height on the wall by similar triangles
        Vec3 hitWorld{0, 0, 0};
        if (m.hit) {
            const float d = dv.sample(m.hitUV.x, m.hitUV.y);
            const Vec4 h4 = inv * Vec4{m.hitUV.x * 2 - 1, m.hitUV.y * 2 - 1, d * 2 - 1, 1};
            hitWorld = Vec3{h4.x, h4.y, h4.z} / h4.w;
        }
        const float expectY = (12.0f - 5.0f) * (2.0f / 5.0f);  // R climbs 2 m per 5 m
        // v7 WP-H: the same reflection through the BVH ray tracer (what stage 10 shows next to the SSR march): its
        // mirror direction obeys the explorer's mirror law and its ray lands on the wall where the SSR march did.
        rt::RtScene rs;
        rt::RtMaterial wet;
        wet.roughness = 0.05f;
        rs.addPrim(rt::Shape::Box, translate({0, -0.5f, -20}) * scale({60, 1, 60}), wet, 0, -1, "floor");
        rs.addPrim(rt::Shape::Box, translate({0, 10, -12.5f}) * scale({60, 20, 1}), wet, 0, -1, "wall");
        rs.build();
        const rt::RtCamera rc = rt::RtCamera::fromView(view, proj);
        float ru = 0, rv = 0;
        rc.project({0, 0, -5}, ru, rv);
        const rt::ReflectionSample rr = rt::traceReflection(rs, rc, ru, rv);
        const Vec3 law = reflectDir(rr.view, rr.normal);
        const float rtLaw = length(law - rr.reflectDir);
        const bool rtOk = rr.hit && rr.reflectedHit && rr.reflectedObject == "wall" && rtLaw < 1e-4f && std::fabs(rr.reflectedPoint.y - expectY) < 0.1f;
        record("T293", "Reflection: mirror law, Schlick F(0) = F0 / F(90) = 1, CPU SSR march and the BVH mirror ray (rt::traceReflection) hit the wall where the mirror law says",
               "law err < 1e-4, F 0.04 / 1, SSR and BVH ray both hit the wall z = -12 at y ~ 2.8 m",
               fmt("law %.2g, F %.3f / %.3f, SSR hit %d after %zu steps at (%.2f, %.2f, %.2f); RT %s at y %.2f (law %.2g)", lawErr, f0, f90, m.hit,
                   m.steps.size(), hitWorld.x, hitWorld.y, hitWorld.z, rr.reflectedObject.c_str(), rr.reflectedPoint.y, rtLaw),
               lawErr < 1e-4f && std::fabs(f0 - 0.04f) < 1e-6f && std::fabs(f90 - 1.0f) < 1e-6f && m.hit && std::fabs(hitWorld.z + 12.0f) < 0.6f &&
                   std::fabs(hitWorld.y - expectY) < 0.6f && rtOk);
    }

    // ---- T294: the pixel inspector reads the right buffer values ----------------------------------------------------------
    {
        FrameSnapshot s;
        s.w = 8;
        s.h = 4;
        s.nearZ = 0.1f;
        s.farZ = 100.0f;
        const auto plane = [](int w, int h, int c) {
            Plane p;
            p.w = w;
            p.h = h;
            p.c = c;
            p.v.assign(static_cast<size_t>(w) * h * c, 0.0f);
            return p;
        };
        s.hdr = plane(8, 4, 3);
        s.depth = plane(8, 4, 1);
        s.surface = plane(8, 4, 4);
        s.ids = plane(8, 4, 3);
        s.final = plane(8, 4, 3);
        s.ao = plane(4, 2, 1);  // half resolution
        s.leafNames = {"Tank / Hull (Cube)", "Tank / Turret (Cylinder)"};
        // pixel (6, 1) top-left origin = row 4 - 1 - 1 = 2 bottom-up
        const size_t i = 2 * 8 + 6;
        s.hdr.v[i * 3] = 1.5f;
        s.hdr.v[i * 3 + 1] = 0.25f;
        s.hdr.v[i * 3 + 2] = 0.125f;
        s.depth.v[i] = depthFromDistance(12.5f, 0.1f, 100.0f);
        s.surface.v[i * 4 + 1] = 1.0f;     // normal (-1, +1, -1)
        s.surface.v[i * 4 + 3] = 0.75f;    // reflectivity
        s.ids.v[i * 3] = 2.0f;             // leaf 1 = turret
        s.ids.v[i * 3 + 1] = 17.0f;
        s.ids.v[i * 3 + 2] = 1.0f;
        s.ao.v[1 * 4 + 3] = 0.42f;         // half-res texel (3, 1) bottom-up covers pixel (6..7, 2..3) bottom-up
        const auto rows = inspect(s, 6, 1);
        const auto find = [&rows](const std::string& l) {
            for (const auto& r : rows)
                if (r.label == l) return r.value;
            return std::string("?");
        };
        const bool ok = find("object") == "Tank / Turret (Cylinder)" && find("triangle") == "#17, front face" &&
                        find("HDR radiance") == "1.5000 0.2500 0.1250" && find("depth").find("12.50 m") != std::string::npos &&
                        find("world normal") == "-1.00 +1.00 -1.00" && find("reflectivity") == "0.750" && find("SSAO").find("0.420") == 0;
        const auto other = inspect(s, 5, 1);
        std::string otherObj = "?";
        for (const auto& r : other)
            if (r.label == "object") otherObj = r.value;
        record("T294", "Pixel inspector: values at a pixel come from that texel of every buffer (top-left pixel coords -> bottom-up GL rows, half-res buffers)",
               "turret #17, HDR 1.5/0.25/0.125, 12.50 m, normal (-1,1,-1), refl 0.75, AO 0.42; neighbour = sky",
               fmt("%s | %s | %s | %s | %s | %s | AO %s | neighbour %s", find("object").c_str(), find("triangle").c_str(), find("HDR radiance").c_str(),
                   find("depth").c_str(), find("world normal").c_str(), find("reflectivity").c_str(), find("SSAO").c_str(), otherObj.c_str()),
               ok && otherObj == "sky (no geometry)");
    }

    // ---- T295: inspector without buffers / out of range ----------------------------------------------------------------
    {
        FrameSnapshot s;
        s.w = 4;
        s.h = 4;
        Plane d;
        d.w = d.h = 4;
        d.c = 1;
        d.v.assign(16, 1.0f);
        s.depth = d;
        const auto rows = inspect(s, -10, 99);  // clamped to (0, 3)
        FrameSnapshot empty;
        const bool ok = rows.size() == 2 && rows[0].value == "(0, 3)" && rows[1].label == "depth" && inspect(empty, 1, 1).empty();
        record("T295", "Inspector lists only the buffers that exist and clamps coordinates to the frame", "pixel + depth rows, (0, 3), empty snapshot -> no rows",
               fmt("%zu rows, first '%s'", rows.size(), rows.empty() ? "" : rows[0].value.c_str()), ok);
    }

    // ---- T296: back-face culling by signed area == by normal ----------------------------------------------------------
    {
        const MeshData cube = createCube();
        const Mat4 world = rotateEulerYXZ({20, 35, 0});
        int agree = 0, front = 0, total = 0;
        for (const Vec3 eye : {Vec3{3, 2, 4}, Vec3{-4, -1, 2}, Vec3{0.5f, 5, -3}}) {
            const Mat4 view = lookAt(eye, {0, 0, 0}, {0, 1, 0}), proj = perspective(radians(60.0f), 1.0f, 0.1f, 50.0f);
            for (size_t t = 0; t + 2 < cube.indices.size(); t += 3) {
                VertexTrace v[3];
                Vec3 w[3];
                for (int k = 0; k < 3; ++k) {
                    v[k] = traceVertex(cube.vertices[cube.indices[t + k]].position, world, view, proj, 512, 512);
                    w[k] = v[k].world;
                }
                // pixel y points down: counter-clockwise on screen = negative area in pixel coordinates
                const bool byArea = -signedArea2(v[0].pixel, v[1].pixel, v[2].pixel) > 0.0f;
                const Vec3 n = cross(w[1] - w[0], w[2] - w[0]);
                const bool byNormal = dot(n, eye - w[0]) > 0.0f;
                agree += byArea == byNormal;
                front += byArea;
                ++total;
            }
        }
        record("T296", "Back-face test by the sign of the projected area agrees with the normal test; a cube shows at most 3 faces (<= 6 front triangles)",
               "36 / 36 agree, front triangles <= 18 of 36", fmt("%d / %d agree, %d front", agree, total, front), agree == total && total == 36 && front <= 18 && front >= 6);
    }

    // ---- T297: coverage count ~ area ---------------------------------------------------------------------------------------
    {
        const Vec2f a{3.2f, 4.7f}, b{180.4f, 30.1f}, c{60.3f, 140.9f};
        int covered = 0;
        for (int y = 0; y < 160; ++y)
            for (int x = 0; x < 200; ++x) covered += rasterize(a, b, c, {x + 0.5f, y + 0.5f}).inside;
        const float area = std::fabs(signedArea2(a, b, c)) * 0.5f;
        const float perim = std::hypot(b.x - a.x, b.y - a.y) + std::hypot(c.x - b.x, c.y - b.y) + std::hypot(a.x - c.x, a.y - c.y);
        record("T297", "Rasterised pixel count matches the triangle area (within half the perimeter)", fmt("|count - %.0f| < %.0f", area, perim * 0.5f),
               fmt("%d pixels", covered), std::fabs(covered - area) < perim * 0.5f);
    }

    // ---- T298: plane / depth sampling orientation --------------------------------------------------------------------------
    {
        Plane p;
        p.w = 4;
        p.h = 2;
        p.c = 1;
        p.v = {0, 1, 2, 3, 4, 5, 6, 7};  // bottom row 0..3, top row 4..7
        const float* tl = p.at(0.01f, 0.99f);
        const float* br = p.at(0.99f, 0.01f);
        DepthView dv{4, 2, p.v.data()};
        record("T298", "Planes and depth views use GL order: v = 0 is the bottom row", "top-left 4, bottom-right 3, depth(0.6, 0.6) = 6",
               fmt("%.0f %.0f %.0f", tl ? *tl : -1.0f, br ? *br : -1.0f, dv.sample(0.6f, 0.6f)), tl && br && *tl == 4.0f && *br == 3.0f && dv.sample(0.6f, 0.6f) == 6.0f);
    }

    // ---- T299: gamma -------------------------------------------------------------------------------------------------------
    {
        bool mono = true;
        float prev = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            const float v = applyGamma(i / 100.0f, 2.2f);
            mono = mono && v >= prev;
            prev = v;
        }
        const float g = applyGamma(0.5f, 2.2f), id = applyGamma(0.3f, 1.0f);
        record("T299", "Gamma out = in^(1/gamma): identity at 1, brightens mid-tones for gamma > 1, monotonic, keeps 0 and 1",
               "0.5 -> 0.7297, 0.3 -> 0.3", fmt("%.4f %.4f, ends %.1f %.1f, mono %d", g, id, applyGamma(0, 2.2f), applyGamma(1, 2.2f), mono),
               std::fabs(g - std::pow(0.5f, 1 / 2.2f)) < 1e-6f && std::fabs(id - 0.3f) < 1e-6f && mono && applyGamma(0, 2.2f) == 0.0f && applyGamma(1, 2.2f) == 1.0f);
    }
}
