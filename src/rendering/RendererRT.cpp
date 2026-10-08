// v7 WP-H: the renderer's ray-tracing hooks. rtBegin() decides the mode of the frame and syncs the ray-traced scene;
// rtEnd() replaces the combine pass (fog + SSR) with the hybrid or the fully ray-traced image, so TAA / exposure /
// bloom / tone mapping run unchanged after it; drawRayOverlay() draws the ray debugger's tree and the status line.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rendering/RayTracerGL.h"
#include "rendering/Renderer.h"

namespace ts {

Renderer::Renderer() : rt_(std::make_unique<RayTracerGL>()) {}
Renderer::~Renderer() = default;

void Renderer::setRayDebug(bool on) {
    rayDebug_ = on;
    if (!on) rayTree_ = rt::RayTree{};
}

void Renderer::rtBegin(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s) {
    rtCam_ = cam;
    rtW_ = w;
    rtH_ = h;
    rtActive_ = frame_.hdr && s.isolate.empty() ? s.rtMode : rt::RtMode::Raster;
    if (rtActive_ == rt::RtMode::Hybrid && !rt_->gpuAvailable()) rtActive_ = rt::RtMode::Raster;   // needs compute
    skyOnly_ = rtActive_ == rt::RtMode::Whitted || rtActive_ == rt::RtMode::PathTrace || rtActive_ == rt::RtMode::LightTrace;
    if (rtActive_ == rt::RtMode::Raster) return;
    timer_.begin("rt scene sync");
    rt::LightOptions lo;
    lo.moonOn = s.moonOn;
    lo.beaconOn = s.beaconOn;
    lo.spotOn = s.spotOn;
    lo.practicalsOn = s.practicalsOn;
    lo.rain = s.rain;
    lo.camera = cam.position;
    rt_->setGround(ground_.texture(), GroundMap::extent());
    rt_->sync(scene, lo);
    timer_.end();
    frame_.ssr = false;                  // replaced by ray-traced reflections (hybrid) / the tracer itself
    if (skyOnly_) {                      // the tracer accumulates its own jittered samples
        frame_.taa = false;
        frame_.dof = false;
    }
}

GLuint Renderer::rtEnd() {
    const RenderSettings& s = frame_.settings;
    RtSettings rs;
    rs.mode = rtActive_;
    rs.scale = s.rtScale;
    rs.whittedScale = s.rtWhittedScale;
    rs.maxBounces = s.rtBounces;
    rs.denoise = s.rtDenoise;
    rs.lightPathsK = s.rtLightPathsK;
    rs.exposureKeyLog2 = atmo_.exposureKeyLog2;
    timer_.begin("ray tracing");
    GLuint image = 0;
    if (rtActive_ == rt::RtMode::Hybrid) {
        RtRasterInputs in;
        in.scene = fb_.resolved.tex;
        in.depth = fb_.depthTex;
        in.surface = fb_.surface.tex;
        in.fog = fb_.fog.tex;
        in.useFog = frame_.fog;
        in.viewProjJ = frame_.viewProjJ;
        image = rt_->hybrid(rs, frame_.cam, rtW_, rtH_, in, frame_.time);
    } else {
        image = rt_->render(rs, rtCam_, rtW_, rtH_, fb_.resolved.tex, frame_.time);
    }
    timer_.end();
    glBindVertexArray(emptyVao_);
    return image ? image : renderCombine();
}

bool Renderer::rayDebugPick(const Scene& scene, float u, float v) {
    if (rtW_ <= 0) return false;
    rt::LightOptions lo;
    const RenderSettings& s = frame_.settings;
    lo.moonOn = s.moonOn;
    lo.beaconOn = s.beaconOn;
    lo.spotOn = s.spotOn;
    lo.practicalsOn = s.practicalsOn;
    lo.rain = s.rain;
    lo.camera = rtCam_.position;
    rt_->sync(scene, lo);
    rayTree_ = rt_->pick(rtCam_, clampf(u, 0.0f, 0.999f), clampf(v, 0.0f, 0.999f));
    return !rayTree_.nodes.empty();
}

std::string Renderer::rtStatusLine() const {
    const rt::RtMode m = frame_.settings.rtMode;
    if (m == rt::RtMode::Raster && !rayDebug_) return "";
    std::string line = m == rt::RtMode::Raster ? std::string("RASTER") : rt_->status(rtActive_ == rt::RtMode::Raster ? m : rtActive_);
    if (m != rt::RtMode::Raster && rtActive_ == rt::RtMode::Raster) line += "  (visible graded view only)";
    for (const GpuTimer::Entry& e : timer_.entries())
        if (e.name == "ray tracing" && rtActive_ != rt::RtMode::Raster) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "  |  %.1f ms GPU", e.ms);
            line += buf;
        }
    if (rayDebug_) line += rayTree_.nodes.empty() ? "  |  RAY DEBUGGER: click a pixel" : "  |  RAY DEBUGGER";
    return line;
}

namespace {

Vec3 rayColor(const rt::RayNode& n) {
    switch (n.kind) {
        case rt::RayKind::Primary: return {1.0f, 1.0f, 1.0f};
        case rt::RayKind::Shadow: return n.hit ? Vec3{1.0f, 0.85f, 0.2f} : Vec3{0.75f, 0.15f, 0.1f};
        case rt::RayKind::Reflection: return {0.2f, 0.9f, 1.0f};
        case rt::RayKind::Refraction: return {1.0f, 0.35f, 0.95f};
        case rt::RayKind::Bounce: return {0.45f, 1.0f, 0.45f};
        default: return {0.8f, 0.8f, 0.8f};
    }
}

}  // namespace

void Renderer::drawRayOverlay(GLuint targetFbo, int w, int h) {
    if (!frame_.settings.rtOverlay) return;
    const bool tree = rayDebug_ && !rayTree_.nodes.empty();
    const std::string status = rtStatusLine();
    if (status.empty() && !tree) return;   // plain raster frame: nothing is touched
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, w, h);
    const Mat4 vp = rtCam_.proj * rtCam_.view;
    if (tree) {
        std::vector<LineVertex> lines, points;
        for (const rt::RayNode& n : rayTree_.nodes) {
            const Vec3 c = rayColor(n);
            // the primary ray starts at the eye: begin it a little in front so it is visible as a line
            const Vec3 from = n.kind == rt::RayKind::Primary ? n.from + normalize(n.to - n.from) * 0.4f : n.from;
            lines.push_back({from, c});
            lines.push_back({n.to, c});
            if (n.hit && n.kind != rt::RayKind::Shadow) points.push_back({n.to, c});
        }
        drawLines(lines, GL_LINES, vp, 0.95f, false);
        drawLines(points, GL_POINTS, vp, 1.0f, false);
    }
    hud_.begin(w, h);
    const float px = std::max(12.0f, static_cast<float>(h) / 60.0f);
    if (tree) {
        // Labels at the ray ends, and the tree as an indented list on the right
        auto project = [&](const Vec3& p, float& sx, float& sy) {
            const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
            if (c.w <= 0.01f) return false;
            sx = (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(w);
            sy = (0.5f - c.y / c.w * 0.5f) * static_cast<float>(h);
            return sx > 0 && sy > 0 && sx < w && sy < h;
        };
        const size_t shown = std::min<size_t>(rayTree_.nodes.size(), 28);
        const float panelW = 30.0f * px, x0 = static_cast<float>(w) - panelW - 16.0f, y0 = 70.0f;
        hud_.rect(x0 - 8, y0 - 8, panelW + 8, (shown + 2) * px * 1.15f + 12, {0.0f, 0.0f, 0.0f, 0.6f});
        char buf[200];
        std::vector<std::pair<float, float>> labels;
        const Vec3 L = rayTree_.radiance;
        std::snprintf(buf, sizeof(buf), "RAY TREE  pixel (%.3f, %.3f)  L = (%.4f %.4f %.4f)", rayTree_.u, rayTree_.v, L.x, L.y, L.z);
        hud_.text(x0, y0, buf, px, {1, 1, 1, 1});
        for (size_t i = 0; i < shown; ++i) {
            const rt::RayNode& n = rayTree_.nodes[i];
            const Vec3 c = rayColor(n);
            const Rgba col{c.x, c.y, c.z, 1.0f};
            const float lum = rt::luminance(n.contribution);
            char tb[24];
            if (n.t >= 1e29f) std::snprintf(tb, sizeof(tb), "inf");
            else std::snprintf(tb, sizeof(tb), "%.2f m", n.t);
            std::snprintf(buf, sizeof(buf), "%*s#%zu %s d%d t=%s L=%.4f %s%s", n.depth * 2, "", i, rt::rayKindName(n.kind), n.depth, tb, lum,
                          n.kind == rt::RayKind::Shadow ? (n.hit ? "lit by " : "blocked: ") : "", n.object.substr(0, 22).c_str());
            hud_.text(x0, y0 + (i + 1.2f) * px * 1.15f, buf, px * 0.9f, col);
            // In-scene label at the ray's end (shadow rays: half way), skipped when it would sit on another label.
            float sx, sy;
            if (project(n.kind == rt::RayKind::Shadow ? n.from + (n.to - n.from) * 0.5f : n.to, sx, sy)) {
                bool free = true;
                for (const auto& q : labels) free = free && (std::fabs(q.first - sx) > 9.0f * px || std::fabs(q.second - sy) > 1.2f * px);
                if (free) {
                    labels.emplace_back(sx, sy);
                    std::snprintf(buf, sizeof(buf), "#%zu %s t=%s", i, rt::rayKindName(n.kind), tb);
                    hud_.rect(sx + 4, sy - px * 1.05f, hud_.textWidth(buf, px * 0.85f) + 4, px, {0, 0, 0, 0.45f});
                    hud_.text(sx + 6, sy - px, buf, px * 0.85f, col);
                }
            }
        }
    }
    if (!status.empty()) {
        const float tw = hud_.textWidth(status, px);
        const float x = 0.5f * (static_cast<float>(w) - tw), y = static_cast<float>(h) - 2.6f * px;
        hud_.rect(x - 8, y - 4, tw + 16, px * 1.4f, {0.0f, 0.0f, 0.0f, 0.55f});
        hud_.text(x, y, status, px, {0.75f, 0.95f, 1.0f, 1.0f});
    }
    hud_.end();
}

}  // namespace ts
