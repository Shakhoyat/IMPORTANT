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
    audioLive_ = !headless_ && audio_.startDevice();  // silent when there is no sound device
    interactiveLoop();
    if (audioLive_) audio_.stop();
    return 0;
}

void App::syncWalkWorld() {
    camera_.walkSpace = sa::walkSpace(*scene_);
    camera_.walkExtra.clear();
    for (Controllable* c : sa::controllables(*scene_)) {
        if (!c) continue;
        const std::vector<Box> boxes = c->dynamicObstacles();
        camera_.walkExtra.insert(camera_.walkExtra.end(), boxes.begin(), boxes.end());
    }
}

void App::forceStrike() {
    Combat* combat = sa::combat(*scene_);
    if (!combat) return;
    if (!sa::anyAlive(*scene_)) return;
    const Vec3 target = sa::primaryRover(*scene_) + Vec3{0, 0.15f, 0};
    const Vec3 uav = transformPoint(scene_->uav().root->world, {0, 0, 0}) + Vec3{0, -0.2f, 0};
    // Designator 100 is ours (the scene owns id 0 and rewrites it every frame from the gimbal).
    combat->setDesignator(100, target, true);
    combat->fireMissile(uav, normalize(target - uav), 100, 0);
    strikeTimer_ = 6.0f;
}

void App::restartWorld(float warmUpSeconds) {
    scene_ = std::make_unique<Scene>();
    detection_ = Detection{};
    possess(-1);
    feed_.clear();
    feedMissiles_ = feedScorches_ = 0;
    feedKills_ = -1;
    strikeTimer_ = -1.0f;
    scriptClock_ = -1.0f;
    scriptFirePrev_ = scriptActionPrev_ = scriptStrikePrev_ = false;
    renderer_->resetHistory();
    for (int i = 0; i < static_cast<int>(warmUpSeconds * 60.0f); ++i) step(1.0f / 60.0f);
}

// Decides who is controlled this step: a one-frame interaction, the director's script, or the possessed object.
void App::gatherControl(ControlInput& in, const ControlInput*& ip, int& index) {
    ip = nullptr;
    index = -1;
    if (interactIdx_ >= 0) {  // door / barrier toggle: one frame of possession with actionPressed
        in = ControlInput{};
        in.actionPressed = true;
        ip = &in;
        index = interactIdx_;
        interactIdx_ = -1;
        return;
    }
    if (scriptClock_ >= 0.0f) {
        const DirectorScript ds = directorScript(scriptClock_);
        in = ds.in;
        const bool fire = in.fire, action = in.actionPressed;
        in.firePressed = fire && !scriptFirePrev_;       // levels in the script, edges for the object
        in.actionPressed = action && !scriptActionPrev_;
        scriptFirePrev_ = fire;
        scriptActionPrev_ = action;
        if (ds.strike && !scriptStrikePrev_) forceStrike();
        scriptStrikePrev_ = ds.strike;
        if (ds.slot >= 0) {
            const std::vector<Controllable*> list = sa::controllables(*scene_);
            index = resolveSlot(list, static_cast<Slot>(ds.slot));
            if (index >= 0) ip = &in;
            // Doors: the script means "open them". The rig may already be open, and the action is a toggle.
            if (index >= 0 && ds.slot == static_cast<int>(Slot::Doors) && in.actionPressed &&
                list[index]->status().find("OPEN") != std::string::npos)
                in.actionPressed = false;
        }
        return;
    }
    if (possessed_ >= 0) {
        in = held_;
        in.firePressed = firePressedEdge_;
        in.actionPressed = actionEdge_;
        in.aimYawDelta = aimYawAcc_;
        in.aimPitchDelta = aimPitchAcc_;
        firePressedEdge_ = actionEdge_ = false;
        aimYawAcc_ = aimPitchAcc_ = 0.0f;
        ip = &in;
        index = possessed_;
    }
}

void App::step(float dt) {
    syncWalkWorld();
    if (!paused_) {
        ControlInput in;
        const ControlInput* ip = nullptr;
        int index = -1;
        gatherControl(in, ip, index);
        sa::update(*scene_, dt, detection_, ip, index);
        if (audioLive_) audio_.pollEvents(*scene_);  // events of every simulation step reach the mixer
        if (scriptClock_ >= 0.0f) scriptClock_ += dt;
        if (strikeTimer_ > 0.0f) {  // keep the laser spot on the rover while the missile flies
            strikeTimer_ -= dt;
            if (Combat* combat = sa::combat(*scene_)) combat->setDesignator(100, sa::primaryRover(*scene_) + Vec3{0, 0.15f, 0}, true);
        }
        updateCombatFeed();
    }
    scene_->explode = lerpf(scene_->explode, exploded_ ? 1.0f : 0.0f, std::min(1.0f, 4.0f * dt));
    scene_->updateWorld();
    // The UAV looks at the world through its own thermal camera; the tracker only sees these pixels.
    // v5: both UAVs' thermal images -> CV image stage (threshold, despeckle, components, moments) -> the
    // squad's shared multi-target tracker (submitted for the next scene update).
    cvStats_ = {};
    for (int u = 0; u < kUavCount; ++u) {
        std::vector<uint8_t>& img = u == 0 ? uavImage_ : uavImage2_;
        renderer_->captureUavCamera(*scene_, img, cvStats_, u);
        if (paused_) continue;
        const std::vector<Measurement2D> blobs = cv::detectTargets(img, kCvWidth, kCvHeight, CvConfig{}, &cvFrame_[u]);
        scene_->submitVision(u, blobs, scene_->cameraWorld(u));
    }
    if (!paused_) detection_ = tracker_.process(uavImage_, kCvWidth, kCvHeight);  // v4 single-target view (UAV 0 feed)
}

void App::updateCombatFeed() {
    const float now = scene_->time();
    auto push = [&](const char* text) { feed_.push_back({text, now}); };
    if (const Combat* combat = sa::combat(*scene_)) {
        int missiles = 0;
        for (const Projectile& p : combat->projectiles())
            if (p.alive && p.kind == ProjectileKind::Missile) ++missiles;
        if (missiles > feedMissiles_) push("UAV MISSILE AWAY");
        feedMissiles_ = missiles;
        const int scorches = static_cast<int>(combat->scorches().size());
        if (scorches > feedScorches_) push("DETONATION");
        feedScorches_ = scorches;
    }
    const int kills = sa::kills(*scene_);
    if (kills >= 0) {
        if (feedKills_ >= 0 && kills > feedKills_) push("TARGET DESTROYED");
        feedKills_ = kills;
    }
    while (!feed_.empty() && (now < feed_.front().time || now - feed_.front().time > 8.0f)) feed_.erase(feed_.begin());
    if (feed_.size() > 5) feed_.erase(feed_.begin());
}

const char* App::possessedName() const {
    const std::vector<Controllable*> c = sa::controllables(*scene_);
    return possessed_ >= 0 && possessed_ < static_cast<int>(c.size()) && c[possessed_] ? c[possessed_]->name() : "";
}

void App::possess(int index) {
    const std::vector<Controllable*> c = sa::controllables(*scene_);
    if (index >= static_cast<int>(c.size()) || (index >= 0 && (!c[index] || !c[index]->possessable()))) return;
    if (index < 0) {
        if (possessed_ >= 0 && camera_.mode == CameraMode::Possess) camera_.mode = modeBeforePossess_;
        possessed_ = possessedSlot_ = -1;
        camera_.possessed = -1;
        held_ = ControlInput{};
        return;
    }
    if (camera_.mode != CameraMode::Possess) modeBeforePossess_ = camera_.mode == CameraMode::Cinematic ? CameraMode::Orbit : camera_.mode;
    possessed_ = index;
    possessedSlot_ = slotOfName(c[index]->name());
    camera_.possessed = index;
    camera_.possessSight = false;
    camera_.mode = CameraMode::Possess;
    ammoMax_ = 0.0f;
    firePressedEdge_ = actionEdge_ = false;
    aimYawAcc_ = aimPitchAcc_ = 0.0f;
}

void App::possessSlot(Slot slot) {
    const int index = resolveSlot(sa::controllables(*scene_), slot);
    if (index >= 0) possess(index);
}

void App::cyclePossession(int dir) {
    const int next = cyclePossessable(sa::controllables(*scene_), possessed_, dir);
    if (next >= 0) possess(next);
}

void App::toggleSight() {
    camera_.possessSight = !camera_.possessSight;
    // The manual UAV's sight is the thermal gimbal camera: switch to the thermal view for it.
    if (camera_.possessSight && possessedSlot_ == static_cast<int>(Slot::Uav)) settings_.view = ViewMode::Thermal;
}

void App::interact() {
    const Vec3 p{camera_.walkX, camera_.walkFeetY, camera_.walkZ};
    interactIdx_ = nearestToggle(sa::controllables(*scene_), p, 3.5f);
}

void App::renderFrame(int w, int h, bool withHud) {
    stats_ = {};
    GLint target = 0;  // default framebuffer, or the capture target bound by the caller
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
    const float aspect = static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1);
    // First-person thermal: the whole screen is the UAV gimbal camera (the image the tracker works on),
    // optionally zoomed; the airframe itself is behind the lens.
    const bool fpv = thermalFirstPerson();
    CameraView cam = fpv ? uavCameraView(*scene_, aspect, thermalFovDeg_) : camera_.compute(*scene_, aspect);
    if (audioLive_) {  // listener = the camera the player sees through
        const double now = nowSeconds();
        const float frameDt = audioClock_ < 0.0 ? 1.0f / 60.0f : static_cast<float>(std::min(0.1, now - audioClock_));
        audioClock_ = now;
        const Vec3 fwd{-cam.view.at(2, 0), -cam.view.at(2, 1), -cam.view.at(2, 2)};
        const Vec3 up{cam.view.at(1, 0), cam.view.at(1, 1), cam.view.at(1, 2)};
        audio_.setMasterVolume(paused_ ? 0.0f : 1.0f);
        audio_.update(*scene_, cam.position, fwd, up, frameDt);
    }
    RenderSettings s = settings_;
    s.hideUav = fpv || (camera_.mode == CameraMode::UavCamera);
    bool uavView = fpv || camera_.mode == CameraMode::UavCamera;
    if (camera_.mode == CameraMode::Cinematic) {
        const DirectorShot shot = directorShot(*scene_, camera_.directorTime);
        if (shot.uavCamera || shot.fpvHud) {
            s.hideUav = true;
            uavView = true;
        }
        // Cinematic lens: shallow focus on the subject, 180-degree shutter motion blur.
        if (!shot.uavCamera && !shot.thermal) {
            s.dof = 1.0f;
            s.focusDistance = length(shot.target - shot.eye);
        }
        s.motionBlur = 0.5f;
    }
    // Explosions shake the camera (not the UAV's stabilised gimbal).
    if (!uavView)
        if (const Combat* combat = sa::combat(*scene_)) applyShake(cam, combat->shake(cam.position), scene_->time());
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
    if (shot.fpvHud) thermalFovDeg_ = shot.fovDeg;
}

void App::drawCinematicCaption(int w, int h) {
    const DirectorShot shot = directorShot(*scene_, camera_.directorTime);
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 1080.0f;
    const float bar = settings_.letterbox * static_cast<float>(h);
    hud.begin(w, h);
    if (shot.fpvHud) {  // synthetic UAV strike shot: the FLIR symbology over the gimbal view
        const float aspect = static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1);
        CameraView cv;
        cv.view = lookAt(shot.eye, shot.target, {0, 1, 0});
        cv.proj = perspective(radians(shot.fovDeg), aspect, 0.05f, 600.0f);
        cv.position = shot.eye;
        hud.setPalette(settings_.palette);
        drawThermalFpvHud(cv, w, h);
    }
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
