#include "app/App.h"

#include <glad/gl.h>
#if !defined(TS_NO_GLFW)
#include <GLFW/glfw3.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "app/HeadlessGL.h"
#include "rendering/ImageWriter.h"
#include "rendering/ThermalPalette.h"
#include "scene/Director.h"

namespace ts {

#if !defined(TS_NO_GLFW)
namespace {

App* appFrom(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

}  // namespace
#endif

App::~App() {
    renderer_.reset();  // GL objects must go before the context
    if (headless_) {
        destroyHeadlessContext();
        return;
    }
#if !defined(TS_NO_GLFW)
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
#endif
}

Quality App::autoQuality() const {
    // Discrete GPUs (e.g. the RTX 4070 12 GB target machine) get the EPIC preset; software rasterisers LOW.
    const std::string renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    auto has = [](const std::string& s, const char* what) { return s.find(what) != std::string::npos; };
    if (has(renderer, "llvmpipe") || has(renderer, "softpipe") || has(renderer, "SVGA") || has(renderer, "GDI Generic"))
        return Quality::Low;
    if (has(vendor, "NVIDIA") || has(renderer, "GeForce") || has(renderer, "RTX") || has(renderer, "Radeon RX") ||
        has(renderer, "Arc(TM) A"))
        return Quality::Epic;
    return Quality::High;
}

bool App::initWindow(bool visible) {
    (void)visible;  // unused in headless-only builds
#if defined(TS_NO_GLFW)
    constexpr bool kHaveWindowing = false;
#else
    constexpr bool kHaveWindowing = true;
#endif
    const bool interactive = opt_.mode == AppOptions::Mode::Interactive;
    const bool wantHeadless = opt_.headless || (!interactive && (!kHaveWindowing || noDisplayAvailable()));
    if (wantHeadless) {
        if (interactive) {
            std::fprintf(stderr, "Interactive mode needs a window: run it on a desktop (or via remote desktop).\n"
                                 "Headless modes: --capture, --record, --benchmark, --trace\n");
            return false;
        }
        std::string description;
        if (!createHeadlessContext(opt_.device, description)) return false;
        headless_ = true;
        std::printf("Headless OpenGL context: %s\n", description.c_str());
    } else {
#if defined(TS_NO_GLFW)
        std::fprintf(stderr, "This build has no windowing (TS_NO_GLFW); use --headless modes\n");
        return false;
#else
        if (!glfwInit()) {
            std::fprintf(stderr, "Failed to initialise GLFW\n");
            return false;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 0);  // the scene is rendered off-screen (MSAA + TAA); the window only shows it
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
        GLFWmonitor* monitor = opt_.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
        int w = opt_.width, h = opt_.height;
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            w = mode->width;
            h = mode->height;
        }
        window_ = glfwCreateWindow(w, h, "ThermalSense: Autonomous UAV Patrol", monitor, nullptr);
        if (!window_) {
            std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
            return false;
        }
        glfwMakeContextCurrent(window_);
        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
            std::fprintf(stderr, "Failed to load OpenGL functions\n");
            return false;
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int k, int, int a, int m) { appFrom(w)->onKey(k, a, m); });
        glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int b, int a, int) { appFrom(w)->onMouseButton(b, a); });
        glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) { appFrom(w)->onCursor(x, y); });
        glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) { appFrom(w)->onScroll(dy); });
#endif
    }
    std::printf("OpenGL %s | %s | %s\n", glGetString(GL_VERSION), glGetString(GL_VENDOR), glGetString(GL_RENDERER));

    scene_ = std::make_unique<Scene>();
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init()) {
        std::fprintf(stderr, "Renderer initialisation failed (see shader log above)\n");
        return false;
    }
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    std::printf("Quality preset: %s%s\n", qualityName(settings_.quality), opt_.quality >= 0 ? "" : " (auto-detected)");
    return true;
}

void App::present() {
#if !defined(TS_NO_GLFW)
    if (window_) {
        glfwSwapBuffers(window_);
        glfwPollEvents();
        return;
    }
#endif
    glFlush();
}

void App::setSwapInterval(int interval) {
#if !defined(TS_NO_GLFW)
    if (window_) glfwSwapInterval(interval);
#else
    (void)interval;
#endif
}

bool App::closeRequested() const {
#if !defined(TS_NO_GLFW)
    if (window_) return glfwWindowShouldClose(window_);
#endif
    return false;
}

int App::run() {
    const bool visible = opt_.mode == AppOptions::Mode::Interactive || opt_.mode == AppOptions::Mode::Benchmark;
    if (!initWindow(visible)) return 1;
    if (opt_.mode == AppOptions::Mode::Capture) return captureMode();
    if (opt_.mode == AppOptions::Mode::Benchmark) return benchmarkMode();
    if (opt_.mode == AppOptions::Mode::Trace) return traceMode();
    if (opt_.mode == AppOptions::Mode::Record) return recordMode();
    interactiveLoop();
    return 0;
}

void App::step(float dt) {
    if (!paused_) scene_->update(dt, detection_);
    scene_->explode = lerpf(scene_->explode, exploded_ ? 1.0f : 0.0f, std::min(1.0f, 4.0f * dt));
    scene_->updateWorld();
    // The UAV looks at the world through its own thermal camera; the tracker only sees these pixels.
    cvStats_ = {};
    renderer_->captureUavCamera(*scene_, uavImage_, cvStats_);
    if (!paused_) detection_ = tracker_.process(uavImage_, kCvWidth, kCvHeight);
}

void App::renderFrame(int w, int h, bool withHud) {
    stats_ = {};
    GLint target = 0;  // default framebuffer, or the capture target bound by the caller
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
    const float aspect = static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1);
    // First-person thermal: the whole screen is the UAV gimbal camera (the image the tracker works on),
    // optionally zoomed; the airframe itself is behind the lens.
    const bool fpv = thermalFirstPerson();
    const CameraView cam = fpv ? uavCameraView(*scene_, aspect, thermalFovDeg_) : camera_.compute(*scene_, aspect);
    RenderSettings s = settings_;
    s.hideUav = fpv || (camera_.mode == CameraMode::UavCamera);
    if (camera_.mode == CameraMode::Cinematic) {
        const DirectorShot shot = directorShot(*scene_, camera_.directorTime);
        if (shot.uavCamera) s.hideUav = true;
        // Cinematic lens: shallow focus on the subject, 180-degree shutter motion blur.
        if (!shot.uavCamera && !shot.thermal) {
            s.dof = 1.0f;
            s.focusDistance = length(shot.target - shot.eye);
        }
        s.motionBlur = 0.5f;
    }
    renderer_->beginFrame(*scene_, cam, w, h, s, stats_);
    drawOverlays(cam);  // drawn into the HDR scene so they are depth-tested and anti-aliased with it
    renderer_->endFrame(static_cast<GLuint>(target), w, h, scene_->time());
    if (settings_.view == ViewMode::Diagnostic && settings_.showLegend) {
        const int size = h / 7;
        renderer_->drawPrimitiveLegend(w - Renderer::kLegendCount * size - 20, 40, size, static_cast<float>(nowSeconds()), stats_);
        glViewport(0, 0, w, h);
    }
    if (camera_.mode == CameraMode::Cinematic) drawCinematicCaption(w, h);
    else if (withHud && showHud_) drawHud(cam, w, h);
}

void App::applyCinematic() {
    if (camera_.mode != CameraMode::Cinematic) return;
    const DirectorShot shot = directorShot(*scene_, camera_.directorTime);
    settings_.view = shot.thermal ? ViewMode::Thermal : ViewMode::Visible;
    settings_.fade = shot.fade;
    settings_.letterbox = 0.5f * (1.0f - (16.0f / 9.0f) / 2.39f);  // 2.39:1 inside a 16:9 frame
}

void App::drawCinematicCaption(int w, int h) {
    const DirectorShot shot = directorShot(*scene_, camera_.directorTime);
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 1080.0f;
    const float bar = settings_.letterbox * static_cast<float>(h);
    hud.begin(w, h);
    const Rgba dim{0.75f, 0.78f, 0.82f, 0.85f * shot.fade};
    hud.text(48.0f * s, static_cast<float>(h) - bar + 34.0f * s, "THERMALSENSE", 26.0f * s, Rgba{0.95f, 0.96f, 1.0f, shot.fade});
    hud.text(48.0f * s, static_cast<float>(h) - bar + 70.0f * s, shot.name, 18.0f * s, dim);
    const char* right = shot.thermal ? "LWIR 160x120  //  THERMAL" : "VISIBLE  //  HDR  PBR  VOLUMETRIC";
    hud.text(static_cast<float>(w) - hud.textWidth(right, 18.0f * s) - 48.0f * s, static_cast<float>(h) - bar + 70.0f * s,
             right, 18.0f * s, dim);
    hud.end();
}

void App::drawOverlays(const CameraView& cam) {
    const Mat4 vp = cam.proj * cam.view;
    std::vector<LineVertex> lines;
    if (settings_.showAxes) {
        // Local frame of every hierarchy level: X red, Y green, Z blue.
        const std::pair<const SceneNode*, float> frames[] = {
            {scene_->uav().root.get(), 0.35f},       {scene_->uav().gimbalYaw, 0.2f},
            {scene_->uav().gimbalPitch, 0.2f},       {scene_->roverRig().root.get(), 0.45f},
            {scene_->roverRig().scannerPivot, 0.15f}};
        for (const auto& [node, len] : frames) {
            const Vec3 o = transformPoint(node->world, {0, 0, 0});
            const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (const Vec3& a : axes) {
                const Vec3 tip = o + normalize(transformDirection(node->world, a)) * len;
                lines.push_back({o, a});
                lines.push_back({tip, a});
            }
        }
        renderer_->drawLines(lines, GL_LINES, vp, 1.0f, false);
    }
    if (settings_.showPlan) {
        // MicroMouse flood-fill field: goal (hidden) cells cyan, colour runs to red with distance.
        const HideController& hc = scene_->hideController();
        std::vector<LineVertex> cells;
        for (int z = 0; z < kGridCells; ++z) {
            for (int x = 0; x < kGridCells; ++x) {
                if (!hc.cellFree(x, z)) continue;
                const int v = hc.floodValue(x, z);
                const float t = v < 0 ? 1.0f : clampf(static_cast<float>(v) / 25.0f, 0.0f, 1.0f);
                const Vec3 c = v == 0 ? Vec3{0.1f, 1.0f, 1.0f} : lerp(Vec3{0.2f, 0.9f, 0.3f}, Vec3{1.0f, 0.15f, 0.1f}, t);
                cells.push_back({HideController::cellCentre(x, z) + Vec3{0, 0.05f, 0}, c});
            }
        }
        renderer_->drawLines(cells, GL_POINTS, vp, 0.85f, true);
        std::vector<LineVertex> path;
        for (const Vec3& p : hc.guideLine()) path.push_back({p + Vec3{0, 0.06f, 0}, {1.0f, 1.0f, 1.0f}});
        renderer_->drawLines(path, GL_LINE_STRIP, vp, 1.0f, false);
    }
    if (settings_.depthTest) glEnable(GL_DEPTH_TEST);
}

}  // namespace ts
