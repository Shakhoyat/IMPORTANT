// v7 WP-H ray-tracing figures (see CaptureRT.h).
#include "app/CaptureRT.h"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>

#include "objects/Terrain.h"
#include "rendering/ImageWriter.h"
#include "rendering/RayTracerGL.h"
#include "rendering/RenderTarget.h"

namespace ts::rtcap {

namespace {

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGB, top row first
};

void blit(Img& dst, const Img& src, int ox, int oy) {
    for (int y = 0; y < src.h && y + oy < dst.h; ++y)
        std::copy(src.px.begin() + static_cast<size_t>(y) * src.w * 3, src.px.begin() + static_cast<size_t>(y + 1) * src.w * 3,
                  dst.px.begin() + (static_cast<size_t>(y + oy) * dst.w + ox) * 3);
}

Img half(const Img& s) {
    Img d;
    d.w = s.w / 2;
    d.h = s.h / 2;
    d.px.resize(static_cast<size_t>(d.w) * d.h * 3);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x)
            for (int k = 0; k < 3; ++k) {
                int a = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) a += s.px[(static_cast<size_t>(2 * y + j) * s.w + 2 * x + i) * 3 + k];
                d.px[(static_cast<size_t>(y) * d.w + x) * 3 + k] = static_cast<uint8_t>(a / 4);
            }
    return d;
}

bool save(Ctx& c, const std::string& name, const Img& i) {
    const bool ok = writePng(c.dir + "/" + name, i.w, i.h, i.px);
    if (c.log) *c.log << (ok ? "wrote " : "FAILED ") << name << " " << i.w << "x" << i.h << "\n";
    std::printf("%s %s\n", ok ? "wrote" : "FAILED", (c.dir + "/" + name).c_str());
    return ok;
}

// Showcase props on open ground east of the bunker entrance: a glass ball, a mirror ball, a glass block, a chrome post
// and a red block (colour bleeding).
const Vec3 kProps{5.2f, 0.0f, 8.6f};

struct Props {
    std::vector<std::unique_ptr<SceneNode>> nodes;
    std::vector<SceneNode*> saved;
    Scene* scene = nullptr;
    Vec3 glass, mirror;
    void add(PrimitiveType p, const Material& m, const Vec3& pos, const Vec3& size, const Vec3& rot, const char* name) {
        auto n = std::make_unique<SceneNode>();
        n->id = "RT-" + std::to_string(nodes.size());
        n->name = name;
        n->object = "RT showcase";
        n->primitive = p;
        n->shading = ShadingModel::Phong;
        n->material = m;
        n->world = translate(pos) * rotateEulerYXZ(rot) * scale(size);
        n->obstacle = false;
        nodes.push_back(std::move(n));
    }
    void install(Scene& s) {
        scene = &s;
        const float g = terrainHeight(kProps.x, kProps.z);
        Material glassM{{0.86f, 0.93f, 0.96f}, 0.2f, 0.0f, 0.9f, 128.0f};
        glassM.surface = Surface::Glass;
        glassM.roughness = 0.05f;
        Material mirrorM{{0.92f, 0.92f, 0.94f}, 0.2f, 0.0f, 0.9f, 128.0f};
        mirrorM.metalness = 1.0f;
        mirrorM.roughness = 0.04f;
        Material red{{0.75f, 0.10f, 0.08f}, 0.2f, 0.0f, 0.1f, 8.0f};
        red.roughness = 0.8f;
        glass = {kProps.x, g + 0.62f, kProps.z};
        mirror = {kProps.x + 1.5f, g + 0.62f, kProps.z - 0.6f};
        add(PrimitiveType::Sphere, glassM, glass, {1.2f, 1.2f, 1.2f}, {0, 0, 0}, "Glass ball");
        add(PrimitiveType::Sphere, mirrorM, mirror, {1.2f, 1.2f, 1.2f}, {0, 0, 0}, "Mirror ball");
        add(PrimitiveType::Cube, glassM, {kProps.x - 1.4f, g + 0.45f, kProps.z - 0.4f}, {0.9f, 0.9f, 0.9f}, {0, 30, 0}, "Glass block");
        add(PrimitiveType::Cylinder, mirrorM, {kProps.x + 0.4f, g + 0.9f, kProps.z - 1.6f}, {0.25f, 0.25f, 1.8f}, {90, 0, 0}, "Chrome post");
        add(PrimitiveType::Cube, red, {kProps.x + 0.5f, g + 0.35f, kProps.z + 1.1f}, {0.7f, 0.7f, 0.7f}, {0, 20, 0}, "Red block");
        auto& lv = const_cast<std::vector<SceneNode*>&>(s.leaves());
        saved = lv;
        for (auto& n : nodes) lv.push_back(n.get());
    }
    ~Props() {
        if (scene) const_cast<std::vector<SceneNode*>&>(scene->leaves()) = saved;
    }
};

CameraView makeCam(const Vec3& eye, const Vec3& at, float fovDeg, int w, int h) {
    CameraView c;
    c.view = lookAt(eye, at, {0, 1, 0});
    c.proj = perspective(radians(fovDeg), static_cast<float>(w) / h, 0.05f, 600.0f);
    c.position = eye;
    return c;
}

struct Frame {
    Ctx& c;
    RenderTarget target;
    int w, h;
    Frame(Ctx& cc, int W, int H) : c(cc), w(W), h(H) { target.create(w, h, 0, false); }
    RenderSettings settings(rt::RtMode m) const {
        RenderSettings s;
        s.quality = c.quality;
        s.rtMode = m;
        s.rtOverlay = false;
        s.rtWhittedScale = 1.0f;   // figures: Whitted at full resolution (interactive default 0.75)
        return s;
    }
    // Renders 'frames' frames of a still scene (the tracer accumulates, TAA / exposure settle). Returns ms / frame
    // (wall clock with glFinish) of the last 'timed' frames.
    double render(const CameraView& cam, const RenderSettings& s, int frames, int timed = 0) {
        c.renderer->resetHistory();
        if (RayTracerGL* rt = c.renderer->rayTracer()) rt->resetAccumulation();
        double ms = 0.0;
        for (int f = 0; f < frames; ++f) {
            target.bind();
            GLint fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            FrameStats st;
            const bool timing = f >= frames - timed;
            if (timing) glFinish();
            const auto t0 = std::chrono::steady_clock::now();
            c.renderer->beginFrame(*c.scene, cam, w, h, s, st);
            c.renderer->endFrame(static_cast<GLuint>(fbo), w, h, c.scene->time());
            if (timing) {
                glFinish();
                ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }
        }
        return timed > 0 ? ms / timed : 0.0;
    }
    void label(const std::string& title, const std::string& sub) {
        target.bind();
        Hud& hud = c.renderer->hud();
        hud.begin(w, h);
        const float px = h / 24.0f;
        hud.rect(0, 0, static_cast<float>(w), px * 2.5f, {0, 0, 0, 0.55f});
        hud.text(px * 0.5f, px * 0.3f, title, px, {1, 1, 1, 1});
        hud.text(px * 0.5f, px * 1.45f, sub, px * 0.7f, {0.75f, 0.9f, 1.0f, 1});
        hud.end();
    }
    Img read() {
        Img i;
        i.w = w;
        i.h = h;
        target.read(i.px);
        return i;
    }
};

const rt::RtMode kModes[rt::kRtModeCount] = {rt::RtMode::Raster, rt::RtMode::Hybrid, rt::RtMode::Whitted, rt::RtMode::PathTrace,
                                             rt::RtMode::LightTrace};
const char* kModeFile[rt::kRtModeCount] = {"raster", "hybrid", "whitted", "path", "light"};

CameraView showcaseCam(int w, int h) { return makeCam(kProps + Vec3{-4.6f, 2.3f, 6.2f}, kProps + Vec3{0.6f, 0.5f, -0.8f}, 50.0f, w, h); }

void setHour(Ctx& c) {
    if (c.hour < 0.0f) return;
    DayCycle& d = c.scene->dayCycle();
    d.setAuto(false);
    d.setHour(c.hour);
    c.scene->setMissionEnabled(false);
    for (int i = 0; i < 4; ++i) c.scene->update(1.0f / 30.0f, Detection{});
}

int framesFor(rt::RtMode m) {
    switch (m) {
        case rt::RtMode::PathTrace: return 384;
        case rt::RtMode::LightTrace: return 256;
        case rt::RtMode::Whitted: return 48;    // soft shadows + AO + anti-aliasing accumulate
        default: return 48;
    }
}

bool modes(Ctx& c) {
    Frame f(c, c.width, c.height);
    const CameraView cam = showcaseCam(c.width, c.height);
    Img montage;
    montage.w = c.width / 2 * 3;
    montage.h = c.height / 2 * 2;
    montage.px.assign(static_cast<size_t>(montage.w) * montage.h * 3, 0);
    bool ok = true;
    for (int i = 0; i < rt::kRtModeCount; ++i) {
        RenderSettings s = f.settings(kModes[i]);
        f.render(cam, s, framesFor(kModes[i]));
        RayTracerGL* rt = c.renderer->rayTracer();
        ok = save(c, std::string("raytrace_mode_") + kModeFile[i] + ".png", f.read()) && ok;
        char sub[160];
        if (kModes[i] == rt::RtMode::Raster) std::snprintf(sub, sizeof(sub), "%s", rt::rtModeCaption(kModes[i]));
        else if (kModes[i] == rt::RtMode::Hybrid) std::snprintf(sub, sizeof(sub), "%s", rt::rtModeCaption(kModes[i]));
        else std::snprintf(sub, sizeof(sub), "%d spp at %dx%d: %s", rt ? rt->samples() : 0, rt ? rt->internalWidth() : 0,
                           rt ? rt->internalHeight() : 0, rt::rtModeCaption(kModes[i]));
        f.label(std::string("(") + static_cast<char>('a' + i) + ") " + rt::rtModeName(kModes[i]), sub);
        blit(montage, half(f.read()), (i % 3) * (c.width / 2), (i / 3) * (c.height / 2));
    }
    // sixth panel: the ray debugger tree from the side
    {
        const CameraView side = makeCam(kProps + Vec3{-7.0f, 3.0f, -1.5f}, kProps + Vec3{-1.0f, 1.0f, 2.5f}, 55.0f, c.width, c.height);
        float u = 0.5f, v = 0.5f;
        rt::RtCamera rc = rt::RtCamera::fromView(cam.view, cam.proj);
        rc.project(kProps + Vec3{0.0f, 0.62f + terrainHeight(kProps.x, kProps.z), 0.0f} + Vec3{-0.15f, 0.2f, 0.0f}, u, v);
        RenderSettings s = f.settings(rt::RtMode::Raster);
        f.render(cam, s, 2);
        c.renderer->setRayDebug(true);
        c.renderer->rayDebugPick(*c.scene, u, v);
        s.rtOverlay = true;
        f.render(side, s, 24);
        c.renderer->setRayDebug(false);
        f.label("(f) RAY DEBUGGER", "Whitted ray tree of one pixel on the glass ball, seen from the side");
        blit(montage, half(f.read()), 2 * (c.width / 2), c.height / 2);
    }
    return save(c, "raytrace_modes.png", montage) && ok;
}

bool tree(Ctx& c) {
    Frame f(c, c.width, c.height);
    const CameraView cam = showcaseCam(c.width, c.height);
    rt::RtCamera rc = rt::RtCamera::fromView(cam.view, cam.proj);
    float u = 0.5f, v = 0.5f;
    rc.project(kProps + Vec3{-0.15f, 0.82f + terrainHeight(kProps.x, kProps.z), 0.0f}, u, v);
    RenderSettings s = f.settings(rt::RtMode::Whitted);
    f.render(cam, s, 24);
    c.renderer->setRayDebug(true);
    c.renderer->rayDebugPick(*c.scene, u, v);
    s.rtOverlay = true;
    f.render(cam, s, 24);
    Img pixel = f.read();
    // mark the picked pixel
    const int mx = static_cast<int>(u * c.width), my = static_cast<int>(v * c.height);
    for (int k = -12; k <= 12; ++k)
        for (int t = -1; t <= 1; ++t) {
            const int xs[2] = {mx + k, mx + t}, ys[2] = {my + t, my + k};
            for (int a = 0; a < 2; ++a)
                if (xs[a] >= 0 && ys[a] >= 0 && xs[a] < c.width && ys[a] < c.height) {
                    uint8_t* p = &pixel.px[(static_cast<size_t>(ys[a]) * c.width + xs[a]) * 3];
                    p[0] = 255; p[1] = 60; p[2] = 40;
                }
        }
    bool ok = save(c, "raytrace_tree_pixel.png", pixel);
    const CameraView side = makeCam(kProps + Vec3{-7.0f, 3.0f, -1.5f}, kProps + Vec3{-1.0f, 1.0f, 2.5f}, 55.0f, c.width, c.height);
    f.render(side, s, 24);
    ok = save(c, "raytrace_tree.png", f.read()) && ok;
    if (const rt::RayTree* t = c.renderer->rayTree()) {
        std::ofstream j(c.dir + "/raytrace_tree.json");
        j << "{\n  \"pixel\": [" << t->u << ", " << t->v << "],\n  \"radiance\": [" << t->radiance.x << ", " << t->radiance.y << ", "
          << t->radiance.z << "],\n  \"rays\": [\n";
        for (size_t i = 0; i < t->nodes.size(); ++i) {
            const rt::RayNode& n = t->nodes[i];
            j << "    {\"id\": " << i << ", \"kind\": \"" << rt::rayKindName(n.kind) << "\", \"parent\": " << n.parent << ", \"depth\": " << n.depth
              << ", \"t\": " << (n.t >= 1e29f ? -1.0f : n.t) << ", \"hit\": " << (n.hit ? "true" : "false") << ", \"object\": \"" << n.object
              << "\", \"contribution\": " << rt::luminance(n.contribution) << "}" << (i + 1 < t->nodes.size() ? "," : "") << "\n";
        }
        j << "  ]\n}\n";
    }
    c.renderer->setRayDebug(false);
    return ok;
}

bool ssr(Ctx& c) {
    // Wet ground (standing water): the raster reflects with SSR, the hybrid mode with traced rays.
    const float wet = c.scene->wetness;
    c.scene->wetness = 1.0f;
    Frame f(c, c.width, c.height);
    // Close to the mirror ball and the chrome post: SSR can only mirror what is on screen (and not hidden), the
    // traced rays mirror the whole camp, including what is behind the camera.
    const Vec3 ball = kProps + Vec3{1.5f, 0.62f + terrainHeight(kProps.x, kProps.z), -0.6f};
    const CameraView cam = makeCam(ball + Vec3{0.5f, 0.45f, 2.4f}, ball + Vec3{-0.2f, -0.05f, 0.0f}, 50.0f, c.width, c.height);
    Img a, b, la, lb;
    RenderSettings s = f.settings(rt::RtMode::Raster);
    f.render(cam, s, 24);
    a = f.read();
    f.label("(a) SCREEN-SPACE REFLECTIONS (raster)", "depth-buffer ray march: only what is on screen can be reflected");
    la = f.read();
    s.rtMode = rt::RtMode::Hybrid;
    f.render(cam, s, 24);
    b = f.read();
    f.label("(b) RAY-TRACED REFLECTIONS (hybrid)", "BVH rays from the G-buffer: off-screen and hidden objects are mirrored too");
    lb = f.read();
    c.scene->wetness = wet;
    Img out;
    out.w = c.width;
    out.h = c.height / 2;
    out.px.assign(static_cast<size_t>(out.w) * out.h * 3, 0);
    blit(out, half(la), 0, 0);
    blit(out, half(lb), c.width / 2, 0);
    bool ok = save(c, "raytrace_ssr.png", out);
    ok = save(c, "raytrace_ssr_raster.png", a) && ok;
    ok = save(c, "raytrace_ssr_hybrid.png", b) && ok;
    return ok;
}

bool photons(Ctx& c) {
    Frame f(c, c.width, c.height);
    const CameraView cam = showcaseCam(c.width, c.height);
    RenderSettings s = f.settings(rt::RtMode::Hybrid);   // syncs the ray-traced scene
    f.render(cam, s, 2);
    s.rtMode = rt::RtMode::Raster;
    f.render(cam, s, 24);
    RayTracerGL* rt = c.renderer->rayTracer();
    if (!rt) return false;
    const rt::RtScene& rs = rt->scene();
    rt::TraceSettings ts;
    ts.emitRadius = 14.0f;      // concentrate the sun / moon photons on the showcase
    ts.emitCentre = kProps;
    std::vector<rt::Photon> ph;
    rt::tracePhotons(rs, 400000, 7, ph, ts);
    std::vector<LineVertex> pts;
    const Vec3 eye = cam.position;
    int caustic = 0, direct = 0, indirect = 0;
    for (const rt::Photon& p : ph) {
        rt::Ray r;
        r.o = p.p + normalize(eye - p.p) * 0.01f;
        r.d = normalize(eye - p.p);
        r.tmax = length(eye - p.p) * 0.999f;
        if (rt::maxComponent(rs.transmittance(r)) <= 0.0f) continue;
        Vec3 col;
        if (p.caustic) { col = {0.2f, 1.0f, 1.0f}; ++caustic; }
        else if (p.bounce == 0) { col = {1.0f, 0.9f, 0.4f}; ++direct; if (direct % 6) continue; }
        else { col = {1.0f, 0.45f, 0.1f}; ++indirect; if (indirect % 2) continue; }
        pts.push_back({p.p, col});
    }
    f.target.bind();
    glViewport(0, 0, c.width, c.height);
    c.renderer->drawLines(pts, GL_POINTS, cam.proj * cam.view, 0.9f, false);
    char sub[200];
    std::snprintf(sub, sizeof(sub), "%d direct (yellow, 1/6 drawn), %d indirect (orange, 1/2), %d caustic (cyan: via glass / mirror)",
                  direct, indirect, caustic);
    f.label("PHOTON MAP (reverse ray tracing from the lights)", sub);
    return save(c, "raytrace_photons.png", f.read());
}

bool perf(Ctx& c) {
    Frame f(c, c.width, c.height);
    const CameraView cam = showcaseCam(c.width, c.height);
    std::ofstream j(c.dir + "/raytrace_perf.json");
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    j << "{\n  \"renderer\": \"" << (gl ? gl : "?") << "\",\n  \"size\": [" << c.width << ", " << c.height << "],\n  \"modes\": [\n";
    for (int i = 0; i < rt::kRtModeCount; ++i) {
        RenderSettings s = f.settings(kModes[i]);
        s.rtWhittedScale = RenderSettings{}.rtWhittedScale;   // the interactive defaults
        const double ms = f.render(cam, s, 30, 20);
        double gpu = 0.0;
        for (const GpuTimer::Entry& e : c.renderer->timer().entries())
            if (e.name == "ray tracing") gpu = e.ms;
        RayTracerGL* rt = c.renderer->rayTracer();
        char line[300];
        std::snprintf(line, sizeof(line), "    {\"mode\": \"%s\", \"ms_per_frame\": %.2f, \"rt_pass_ms\": %.2f, \"internal\": [%d, %d]}%s",
                      rt::rtModeName(kModes[i]), ms, kModes[i] == rt::RtMode::Raster ? 0.0 : gpu,
                      kModes[i] == rt::RtMode::Hybrid ? c.width : (rt ? rt->internalWidth() : 0),
                      kModes[i] == rt::RtMode::Hybrid ? c.height : (rt ? rt->internalHeight() : 0), i + 1 < rt::kRtModeCount ? "," : "");
        j << line << "\n";
        std::printf("perf %-14s %7.2f ms / frame (ray tracing pass %.2f ms)\n", rt::rtModeName(kModes[i]), ms, gpu);
        if (c.log) {   // per-pass GPU times of this mode (smoothed)
            for (const GpuTimer::Entry& e : c.renderer->timer().entries())
                if (e.ms > 0.05) *c.log << "      " << e.name << " " << e.ms << " ms\n";
        }
        if (c.log) *c.log << line << "\n";
    }
    j << "  ]\n}\n";
    return true;
}

}  // namespace

const std::vector<std::string>& shotNames() {
    static const std::vector<std::string> names = {"raytrace_modes", "raytrace_tree", "raytrace_ssr", "raytrace_photons", "raytrace_perf"};
    return names;
}

bool figure(Ctx& c, const std::string& shot) {
    if (!c.scene || !c.renderer) return false;
    setHour(c);
    Props props;
    props.install(*c.scene);
    c.scene->updateWorld();
    bool ok = false;
    if (shot == "raytrace_modes") ok = modes(c);
    else if (shot == "raytrace_tree") ok = tree(c);
    else if (shot == "raytrace_ssr") ok = ssr(c);
    else if (shot == "raytrace_photons") ok = photons(c);
    else if (shot == "raytrace_perf") ok = perf(c);
    else if (shot.rfind("raytrace_view_", 0) == 0) {   // debugging aid: one mode of the showcase view (not in shotNames)
        const std::string m = shot.substr(14);
        for (int i = 0; i < rt::kRtModeCount; ++i)
            if (m == kModeFile[i]) {
                Frame f(c, c.width, c.height);
                f.render(showcaseCam(c.width, c.height), f.settings(kModes[i]), framesFor(kModes[i]));
                ok = save(c, shot + ".png", f.read());
            }
    }
    c.renderer->labInvalidateCaches();
    return ok;
}

}  // namespace ts::rtcap
