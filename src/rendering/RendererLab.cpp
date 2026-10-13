// Lab-figure helpers of the renderer: read the internal buffers of the last frame back as displayable images
// (shadow map, SSAO, fog, HDR scene before tone mapping, bloom, reflectivity, depth) and a few switches the
// lab figures need (static shadow cache, coarse sphere). Used only by app/CaptureLab.cpp; nothing here runs in
// the normal frame.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "math/ExplorerMath.h"
#include "primitives/Sphere.h"
#include "rendering/Renderer.h"

#include "primitives/Library.h"

namespace ts {

namespace {

uint8_t to8(float v) { return static_cast<uint8_t>(std::lround(clampf(v, 0.0f, 1.0f) * 255.0f)); }

// Piecewise "turbo-like" colour ramp: dark blue -> blue -> cyan -> green -> yellow -> orange -> red -> white.
Vec3 ramp(float t) {
    static const Vec3 stops[] = {{0.02f, 0.02f, 0.20f}, {0.10f, 0.20f, 0.85f}, {0.05f, 0.80f, 0.90f}, {0.15f, 0.85f, 0.25f},
                                 {0.95f, 0.90f, 0.10f}, {0.98f, 0.45f, 0.08f}, {0.85f, 0.05f, 0.05f}, {1.0f, 1.0f, 1.0f}};
    const int n = static_cast<int>(sizeof(stops) / sizeof(stops[0]));
    if (!(t == t)) t = 0.0f;  // NaN pixels
    const float x = clampf(t, 0.0f, 1.0f) * (n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    return lerp(stops[i], stops[i + 1], x - static_cast<float>(i));
}

// Flips rows (GL reads bottom-up) and packs a float RGB image.
void pack(LabImage& out, int w, int h, const std::vector<Vec3>& px /* bottom-up */) {
    out.w = w;
    out.h = h;
    out.rgb.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Vec3& c = px[static_cast<size_t>(h - 1 - y) * w + x];
            uint8_t* o = &out.rgb[(static_cast<size_t>(y) * w + x) * 3];
            o[0] = to8(c.x);
            o[1] = to8(c.y);
            o[2] = to8(c.z);
        }
}

std::vector<float> readFloat(GLuint tex, int w, int h, GLenum format, int comps) {
    std::vector<float> v(static_cast<size_t>(w) * h * comps);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, format, GL_FLOAT, v.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return v;
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<float>(v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

}  // namespace

void Renderer::labInvalidateCaches() {
    shadows_.staticValid = false;
    historyValid_ = false;
    adaptValid_ = false;
}

void Renderer::labSphereResolution(int slices, int stacks) {
    auto fresh = std::make_unique<Mesh>();  // the old mesh frees its buffers in its destructor
    if (slices <= 0) fresh->upload(createSphere(36, 18));
    else fresh->upload(createSphere(slices, stacks));
    meshes_[PrimitiveType::Sphere] = std::move(fresh);
}

void Renderer::labOverrideMesh(PrimitiveType key, const MeshData& mesh) {
    auto fresh = std::make_unique<Mesh>();
    fresh->upload(mesh.vertices.empty() ? sceneMesh(key) : mesh);
    meshes_[key] = std::move(fresh);
}

bool Renderer::labReadBuffer(LabBuffer which, LabImage& out) {
    char note[200] = "";
    if (std::getenv("TS_LAB_TRACE")) std::fprintf(stderr, "labReadBuffer %d\n", static_cast<int>(which));
    const int w = fb_.width(), h = fb_.height();
    if (w <= 0 || h <= 0) return false;
    std::vector<Vec3> px;
    switch (which) {
        case LabBuffer::ShadowMoonNear:
        case LabBuffer::ShadowMoonWorld: {
            if (!shadows_.moonTex || shadows_.moonRes <= 0) return false;
            const int n = shadows_.moonRes;
            const int layer = which == LabBuffer::ShadowMoonNear ? 0 : 1;
            std::vector<float> all(static_cast<size_t>(n) * n * 2);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, shadows_.moonTex);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);  // raw depths, not comparisons
            glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT, GL_FLOAT, all.data());
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            const float* d = all.data() + static_cast<size_t>(layer) * n * n;
            // Depth 1.0 = nothing drawn there. Stretch the occupied range so near casters are bright.
            float lo = 1.0f, hi = 0.0f;
            for (size_t i = 0; i < static_cast<size_t>(n) * n; ++i)
                if (d[i] < 0.9999f) { lo = std::min(lo, d[i]); hi = std::max(hi, d[i]); }
            if (hi <= lo) hi = lo + 1e-4f;
            px.resize(static_cast<size_t>(n) * n);
            for (size_t i = 0; i < px.size(); ++i) {
                const float v = d[i] >= 0.9999f ? 0.0f : 1.0f - 0.85f * (d[i] - lo) / (hi - lo);
                px[i] = {v, v, v};
            }
            pack(out, n, n, px);
            std::snprintf(note, sizeof(note), "%dx%d depth, cascade %s, stored range %.4f..%.4f stretched to white (near) .. dark (far), black = empty",
                          n, n, layer == 0 ? "0 (near, follows the camera)" : "1 (world)", lo, hi);
            break;
        }
        case LabBuffer::Ssao: {
            if (fb_.ao[1].tex == 0 || fb_.halfW <= 0) return false;
            const int aw = fb_.ao[1].w, ah = fb_.ao[1].h;
            const std::vector<float> v = readFloat(fb_.ao[1].tex, aw, ah, GL_RED, 1);
            px.resize(v.size());
            for (size_t i = 0; i < v.size(); ++i) px[i] = {v[i], v[i], v[i]};
            pack(out, aw, ah, px);
            std::snprintf(note, sizeof(note), "%dx%d, ambient visibility (white = open sky, dark = occluded)", aw, ah);
            break;
        }
        case LabBuffer::FogInscatter:
        case LabBuffer::FogTransmittance: {
            if (fb_.fog.tex == 0) return false;
            const int fw = fb_.fog.w, fh = fb_.fog.h;
            const std::vector<float> v = readFloat(fb_.fog.tex, fw, fh, GL_RGBA, 4);
            px.resize(static_cast<size_t>(fw) * fh);
            if (which == LabBuffer::FogInscatter) {
                std::vector<float> lum(px.size());
                for (size_t i = 0; i < px.size(); ++i) lum[i] = std::max({v[4 * i], v[4 * i + 1], v[4 * i + 2]});
                const float peak = std::max(percentile(lum, 0.995f), 1e-6f);
                for (size_t i = 0; i < px.size(); ++i) px[i] = {std::sqrt(v[4 * i] / peak), std::sqrt(v[4 * i + 1] / peak), std::sqrt(v[4 * i + 2] / peak)};
                std::snprintf(note, sizeof(note), "%dx%d in-scattered light (linear RGB), shown as sqrt(x / %.4f)", fw, fh, peak);
            } else {
                for (size_t i = 0; i < px.size(); ++i) px[i] = {v[4 * i + 3], v[4 * i + 3], v[4 * i + 3]};
                std::snprintf(note, sizeof(note), "%dx%d transmittance (white = clear, dark = fully fogged)", fw, fh);
            }
            pack(out, fw, fh, px);
            break;
        }
        case LabBuffer::HdrColour:
        case LabBuffer::HdrLogLuminance:
        case LabBuffer::Lit:
        case LabBuffer::Bloom: {
            ColorTarget* t = which == LabBuffer::Lit ? &fb_.lit : (which == LabBuffer::Bloom ? &fb_.bloom[0] : &fb_.resolved);
            if (which == LabBuffer::Bloom && fb_.bloom.empty()) return false;
            if (t->tex == 0) return false;
            const int tw = t->w, th = t->h;
            const std::vector<float> v = readFloat(t->tex, tw, th, GL_RGBA, 4);
            px.resize(static_cast<size_t>(tw) * th);
            std::vector<float> lum(px.size());
            for (size_t i = 0; i < px.size(); ++i)
                lum[i] = 0.2126f * v[4 * i] + 0.7152f * v[4 * i + 1] + 0.0722f * v[4 * i + 2];
            for (float& l : lum)
                if (!std::isfinite(l)) l = 0.0f;
            if (which == LabBuffer::HdrLogLuminance) {
                // False colour of log2(luminance): 14 stops from 2^-10 to 2^4 (cd/m2 in scene units).
                const float lo = -10.0f, hi = 4.0f;
                for (size_t i = 0; i < px.size(); ++i) px[i] = ramp((std::log2(std::max(lum[i], 1e-6f)) - lo) / (hi - lo));
                float mx = 0.0f;
                for (float l : lum) mx = std::max(mx, l);
                std::snprintf(note, sizeof(note), "%dx%d RGBA16F before tone mapping: log2(luminance) from %.0f to %.0f stops (blue = dark, red = bright), peak %.2f",
                              tw, th, lo, hi, mx);
            } else {
                const float exposure = which == LabBuffer::Bloom ? 1.0f / std::max(percentile(lum, 0.999f), 1e-4f) : 4.0f;
                for (size_t i = 0; i < px.size(); ++i) {
                    const Vec3 c{v[4 * i] * exposure, v[4 * i + 1] * exposure, v[4 * i + 2] * exposure};
                    px[i] = {std::pow(1.0f - std::exp(-c.x), 1.0f / 2.2f), std::pow(1.0f - std::exp(-c.y), 1.0f / 2.2f),
                             std::pow(1.0f - std::exp(-c.z), 1.0f / 2.2f)};
                }
                std::snprintf(note, sizeof(note), "%dx%d linear HDR x %.2f through 1-exp(-x), gamma 2.2 (a plain exposure, not the film curve)", tw, th,
                              exposure);
            }
            pack(out, tw, th, px);
            break;
        }
        case LabBuffer::Reflectivity:
        case LabBuffer::SurfaceNormal: {
            if (fb_.surface.tex == 0) return false;
            const std::vector<float> v = readFloat(fb_.surface.tex, w, h, GL_RGBA, 4);
            px.resize(static_cast<size_t>(w) * h);
            for (size_t i = 0; i < px.size(); ++i) {
                if (which == LabBuffer::Reflectivity) px[i] = {v[4 * i + 3], v[4 * i + 3], v[4 * i + 3]};
                else px[i] = {v[4 * i], v[4 * i + 1], v[4 * i + 2]};
            }
            pack(out, w, h, px);
            std::snprintf(note, sizeof(note), "%dx%d surface target %s", w, h,
                          which == LabBuffer::Reflectivity ? "alpha = reflectivity (SSR weight), white = mirror" : "rgb = world normal * 0.5 + 0.5");
            break;
        }
        case LabBuffer::Depth: {
            if (fb_.depthTex == 0) return false;
            glBindTexture(GL_TEXTURE_2D, fb_.depthTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glBindTexture(GL_TEXTURE_2D, 0);
            const std::vector<float> v = readFloat(fb_.depthTex, w, h, GL_DEPTH_COMPONENT, 1);
            const float n = frame_.cam.nearZ, f = frame_.cam.farZ;
            px.resize(v.size());
            float lo = 1e9f, hi = 0.0f;
            std::vector<float> lin(v.size());
            for (size_t i = 0; i < v.size(); ++i) {
                const float z = v[i] * 2.0f - 1.0f;
                lin[i] = v[i] >= 0.99999f ? -1.0f : 2.0f * n * f / (f + n - z * (f - n));
                if (lin[i] > 0.0f) { lo = std::min(lo, lin[i]); hi = std::max(hi, lin[i]); }
            }
            for (size_t i = 0; i < v.size(); ++i) {
                const float g = lin[i] < 0.0f ? 0.0f : 1.0f - (lin[i] - lo) / std::max(hi - lo, 1e-3f);
                px[i] = {g, g, g};
            }
            pack(out, w, h, px);
            std::snprintf(note, sizeof(note), "%dx%d depth buffer linearised: white = %.1f m, dark = %.1f m, black = sky", w, h, lo, hi);
            break;
        }
    }
    out.note = note;
    return true;
}

// v7 WP-I: float copies of the last frame's buffers for the Frame Explorer (pixel inspector, stage images).
bool Renderer::labSnapshot(fx::FrameSnapshot& out) {
    const int w = fb_.width(), h = fb_.height();
    if (w <= 0 || h <= 0 || fb_.resolved.tex == 0) return false;
    const auto grab = [](fx::Plane& p, GLuint tex, int tw, int th, GLenum format, int comps) {
        p.w = tw;
        p.h = th;
        p.c = comps;
        p.v = readFloat(tex, tw, th, format, comps);
    };
    out.w = w;
    out.h = h;
    grab(out.hdr, fb_.resolved.tex, w, h, GL_RGB, 3);
    grab(out.lit, fb_.lit.tex, fb_.lit.w, fb_.lit.h, GL_RGB, 3);
    if (frame_.taa) grab(out.taa, fb_.history[historyIndex_].tex, w, h, GL_RGB, 3);
    else out.taa = out.lit;
    grab(out.surface, fb_.surface.tex, w, h, GL_RGBA, 4);
    glBindTexture(GL_TEXTURE_2D, fb_.depthTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glBindTexture(GL_TEXTURE_2D, 0);
    grab(out.depth, fb_.depthTex, w, h, GL_DEPTH_COMPONENT, 1);
    out.ao = {};
    out.fog = {};
    out.bloom = {};
    if (frame_.ao && fb_.ao[1].tex) grab(out.ao, fb_.ao[1].tex, fb_.ao[1].w, fb_.ao[1].h, GL_RED, 1);
    if (frame_.fog && fb_.fog.tex) grab(out.fog, fb_.fog.tex, fb_.fog.w, fb_.fog.h, GL_RGBA, 4);
    const bool bloom = frame_.hdr && frame_.settings.bloomOn && !fb_.bloom.empty();
    if (bloom) grab(out.bloom, fb_.bloom[0].tex, fb_.bloom[0].w, fb_.bloom[0].h, GL_RGB, 3);
    out.bloomMips = bloom ? static_cast<int>(fb_.bloom.size()) : 0;
    // Matrices: the depth buffer holds the jittered projection; the unjittered one is what TAA converges to.
    out.view = frame_.cam.view;
    out.proj = frame_.cam.proj;
    jitterProjection(out.proj, 2.0f * frame_.jitterUV[0], 2.0f * frame_.jitterUV[1]);
    out.viewProj = frame_.viewProj;
    out.viewProjJ = frame_.viewProjJ;
    out.camPos = frame_.cam.position;
    out.nearZ = frame_.cam.nearZ;
    out.farZ = frame_.cam.farZ;
    out.jitterUV[0] = frame_.jitterUV[0];
    out.jitterUV[1] = frame_.jitterUV[1];
    out.frameIndex = frameIndex_;
    out.hdrPipeline = frame_.hdr;
    out.taaOn = frame_.taa;
    out.ssrOn = frame_.ssr;
    out.aoOn = frame_.ao;
    out.fogOn = frame_.fog;
    out.ssrSteps = frame_.ssrSteps;
    out.shadowMode = frame_.shadowMode;
    out.pcfTaps = frame_.pcfTaps;
    out.moonShadowRes = shadows_.moonRes;
    out.moonShadowTex = shadows_.moonTex;
    out.moonViewProj[0] = frame_.moon[0].viewProj;
    out.moonViewProj[1] = frame_.moon[1].viewProj;
    out.moonTexelWorld[0] = frame_.moon[0].texelWorld;
    out.moonTexelWorld[1] = frame_.moon[1].texelWorld;
    out.cascadeSplit = frame_.split;
    if (adaptValid_) adaptedStats(out.stats);
    return true;
}

}  // namespace ts
