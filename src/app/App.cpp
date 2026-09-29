#include "app/App.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <string>

#include "rendering/ImageWriter.h"
#include "rendering/ThermalPalette.h"
#include "scene/Director.h"

namespace ts {

namespace {

App* appFrom(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

}  // namespace

App::~App() {
    renderer_.reset();  // GL objects must go before the context
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

bool App::initWindow(bool visible) {
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    window_ = glfwCreateWindow(opt_.width, opt_.height, "ThermalSense: Autonomous UAV Patrol", nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create an OpenGL 3.3 window\n");
        return false;
    }
    glfwMakeContextCurrent(window_);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
        return false;
    }
    std::printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, [](GLFWwindow* w, int k, int, int a, int m) { appFrom(w)->onKey(k, a, m); });
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int b, int a, int) { appFrom(w)->onMouseButton(b, a); });
    glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) { appFrom(w)->onCursor(x, y); });
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) { appFrom(w)->onScroll(dy); });

    glEnable(GL_MULTISAMPLE);
    scene_ = std::make_unique<Scene>();
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init()) {
        std::fprintf(stderr, "Renderer initialisation failed (see shader log above)\n");
        return false;
    }
    return true;
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
    const CameraView cam = camera_.compute(*scene_, static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1));
    renderer_->beginFrame(*scene_, cam, w, h, settings_, stats_);
    drawOverlays(cam);  // drawn into the HDR scene so they are depth-tested and anti-aliased with it
    renderer_->endFrame(static_cast<GLuint>(target), w, h, scene_->time());
    if (settings_.view == ViewMode::Diagnostic && settings_.showLegend) {
        const int size = h / 7;
        renderer_->drawPrimitiveLegend(w - Renderer::kLegendCount * size - 20, 40, size, static_cast<float>(glfwGetTime()), stats_);
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

void App::saveScreenshot(int w, int h) {
    std::vector<uint8_t> bottomUp(static_cast<size_t>(w) * h * 3), rgb(bottomUp.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, bottomUp.data());
    const size_t row = static_cast<size_t>(w) * 3;
    for (int y = 0; y < h; ++y)
        std::copy(bottomUp.begin() + (h - 1 - y) * row, bottomUp.begin() + (h - y) * row, rgb.begin() + y * row);
    char name[64];
    std::snprintf(name, sizeof(name), "screenshot_%03d.png", screenshotCount_++);
    std::printf(writePng(name, w, h, rgb) ? "Saved %s\n" : "Could not save %s\n", name);
}

void App::updateTitle(double fps) {
    char title[256];
    std::snprintf(title, sizeof(title),
                  "ThermalSense | %s %s | %s | %s | cam %s | UAV %s | rover %s | %.0f FPS | %d draws | %lld tris",
                  viewModeName(settings_.view), qualityName(settings_.quality), edgeModeName(settings_.edges), overrideName(settings_.shading),
                  cameraModeName(camera_.mode), droneModeName(scene_->drone.mode),
                  scene_->rover.hidden ? "HIDDEN" : "EXPOSED", fps, stats_.drawCalls + cvStats_.drawCalls,
                  stats_.triangles + cvStats_.triangles);
    glfwSetWindowTitle(window_, title);
}

void App::interactiveLoop() {
    glfwSwapInterval(1);
    double last = glfwGetTime(), titleTimer = 0.0;
    int frames = 0;
    while (!glfwWindowShouldClose(window_)) {
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::min(now - last, 0.05));
        last = now;

        // Free-fly movement (continuous keys).
        if (camera_.mode == CameraMode::Free) {
            const float speed = 6.0f * dt;
            const auto down = [this](int k) { return glfwGetKey(window_, k) == GLFW_PRESS ? 1.0f : 0.0f; };
            camera_.moveFree((down(GLFW_KEY_W) - down(GLFW_KEY_S)) * speed, (down(GLFW_KEY_D) - down(GLFW_KEY_A)) * speed,
                             (down(GLFW_KEY_PAGE_UP) - down(GLFW_KEY_PAGE_DOWN)) * speed);
        }
        step(dt);
        if (camera_.mode == CameraMode::Cinematic && !paused_) camera_.directorTime += dt;
        // Weather: rain eases in/out over ~3 s; the ground soaks in ~20 s and dries in ~90 s.
        settings_.rain = lerpf(settings_.rain, rainTarget_, std::min(1.0f, dt / 3.0f));
        if (!paused_) {
            const float tau = settings_.rain > scene_->wetness ? 20.0f : 90.0f;
            scene_->wetness = lerpf(scene_->wetness, settings_.rain, std::min(1.0f, dt / tau));
        }
        applyCinematic();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        if (w > 0 && h > 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            renderFrame(w, h, true);
        }
        ++frames;
        titleTimer += dt;
        if (titleTimer >= 0.25) {
            fps_ = frames / titleTimer;
            updateTitle(fps_);
            frames = 0;
            titleTimer = 0.0;
        }
        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void App::onKey(int key, int action, int) {
    if (action != GLFW_PRESS || opt_.mode != AppOptions::Mode::Interactive) return;  // benchmarks ignore input
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(window_, 1); break;
        case GLFW_KEY_C: camera_.cycle(); break;
        case GLFW_KEY_K:
            if (camera_.mode == CameraMode::Cinematic) {
                camera_.mode = CameraMode::Orbit;
                settings_.letterbox = 0.0f;
                settings_.fade = 1.0f;
            } else {
                camera_.mode = CameraMode::Cinematic;
                camera_.directorTime = 0.0f;
            }
            break;
        case GLFW_KEY_V:
            settings_.view = static_cast<ViewMode>((static_cast<int>(settings_.view) + 1) % 4);
            break;
        case GLFW_KEY_X: settings_.edges = static_cast<EdgeMode>((static_cast<int>(settings_.edges) + 1) % 3); break;
        case GLFW_KEY_Z: settings_.depthTest = !settings_.depthTest; break;
        case GLFW_KEY_O: settings_.shading = ShadingOverride::PerObject; break;
        case GLFW_KEY_F: settings_.shading = ShadingOverride::Flat; break;
        case GLFW_KEY_G: settings_.shading = ShadingOverride::Gouraud; break;
        case GLFW_KEY_P: settings_.shading = ShadingOverride::Phong; break;
        case GLFW_KEY_1: settings_.moonOn = !settings_.moonOn; break;
        case GLFW_KEY_2: settings_.beaconOn = !settings_.beaconOn; break;
        case GLFW_KEY_3: settings_.spotOn = !settings_.spotOn; break;
        case GLFW_KEY_4: settings_.practicalsOn = !settings_.practicalsOn; break;
        case GLFW_KEY_E: exploded_ = !exploded_; break;
        case GLFW_KEY_H: settings_.showAxes = !settings_.showAxes; break;
        case GLFW_KEY_M: settings_.showPlan = !settings_.showPlan; break;
        case GLFW_KEY_I: settings_.indexed = !settings_.indexed; break;
        case GLFW_KEY_U: showFeed_ = !showFeed_; break;
        case GLFW_KEY_F5: settings_.quality = static_cast<Quality>((static_cast<int>(settings_.quality) + 1) % 3); break;
        case GLFW_KEY_B: settings_.pbr = !settings_.pbr; break;
        case GLFW_KEY_R: rainTarget_ = rainTarget_ > 0.5f ? 0.0f : 1.0f; break;
        case GLFW_KEY_T: settings_.palette = (settings_.palette + 1) % kPaletteCount; break;
        case GLFW_KEY_N: settings_.cinematic = !settings_.cinematic; break;
        case GLFW_KEY_EQUAL: settings_.exposure *= 1.15f; break;
        case GLFW_KEY_MINUS: settings_.exposure /= 1.15f; break;
        case GLFW_KEY_TAB: showHud_ = !showHud_; break;
        case GLFW_KEY_F1: showHelp_ = !showHelp_; break;
        case GLFW_KEY_SPACE: paused_ = !paused_; break;
        case GLFW_KEY_F12: {
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            renderFrame(w, h, true);
            saveScreenshot(w, h);
            break;
        }
        default: break;
    }
}

void App::onMouseButton(int button, int action) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        dragging_ = action == GLFW_PRESS;
        glfwGetCursorPos(window_, &lastX_, &lastY_);
    }
}

void App::onCursor(double x, double y) {
    if (dragging_) camera_.drag(static_cast<float>(x - lastX_), static_cast<float>(y - lastY_));
    lastX_ = x;
    lastY_ = y;
}

void App::onScroll(double dy) {
    if (opt_.mode == AppOptions::Mode::Interactive) camera_.zoom(static_cast<float>(dy));
}

}  // namespace ts
