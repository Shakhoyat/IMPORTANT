#include "app/App.h"

#include <glad/gl.h>
#if !defined(TS_NO_GLFW)
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
        preferWslGpu();
        if (!glfwInit()) {
            std::fprintf(stderr, "Failed to initialise GLFW\n");
            return false;
        }
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
        // Ask for 4.6 / 4.3 first so the GPU ray tracer (compute + SSBOs) is available; 3.3 is the floor.
        static const int kVersions[][2] = {{4, 6}, {4, 3}, {3, 3}};
        for (const auto& v : kVersions) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, v[0]);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, v[1]);
            window_ = glfwCreateWindow(w, h, "ThermalSense: Autonomous UAV Patrol", monitor, nullptr);
            if (window_) break;
        }
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

    scene_ = makeScene();
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init()) {
        std::fprintf(stderr, "Renderer initialisation failed (see shader log above)\n");
        return false;
    }
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    std::printf("Quality preset: %s%s\n", qualityName(settings_.quality), opt_.quality >= 0 ? "" : " (auto-detected)");
    // v7 WP-Q: interactive auto preset = start from the GPU's preset, then keep the best one that holds the target.
    qualityAuto_ = opt_.quality < 0 && interactive;
    const std::string glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    governor_.targetFps = glRenderer.find("D3D12") != std::string::npos ? 30.0f : 60.0f;
    if (const char* t = std::getenv("TS_TARGET_FPS"))
        if (std::atof(t) > 0.0) governor_.targetFps = static_cast<float>(std::atof(t));
    return true;
}

void App::governQuality(double now) {
    if (!qualityAuto_ || governor_.done || paused_) return;
    const double frame = governor_.last >= 0.0 ? now - governor_.last : 0.0;
    governor_.last = now;
    if (governor_.start < 0.0) governor_.start = now;
    // Warm-up (new targets, shader caches, the first frames build the grass / ground map), and any hitch (a frame
    // over 250 ms: window drag, loading) restarts the measurement.
    if (now - governor_.start < 2.5 || frame > 0.25) {
        governor_.measureStart = now;
        governor_.frames = 0;
        return;
    }
    ++governor_.frames;
    if (now - governor_.measureStart < 3.0) return;
    const double fps = governor_.frames / (now - governor_.measureStart);
    if (fps >= 0.9 * governor_.targetFps || settings_.quality == Quality::Low) {
        governor_.done = true;
        std::printf("Auto preset: %s holds %.0f fps (target %.0f)\n", qualityName(settings_.quality), fps, governor_.targetFps);
        std::fflush(stdout);
        return;
    }
    const Quality lower = settings_.quality == Quality::High ? Quality::Low : Quality::High;   // EPIC / ULTRA -> HIGH -> LOW
    std::printf("Auto preset: %s runs at %.0f fps (target %.0f): switching to %s\n", qualityName(settings_.quality), fps,
                governor_.targetFps, qualityName(lower));
    std::fflush(stdout);
    settings_.quality = lower;
    governor_.start = now;   // measure the new preset after its own warm-up
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
    // v9 M2: the player's settings (ThermalSense.ini) for the interactive game; the command line wins for this run.
    if (opt_.mode == AppOptions::Mode::Interactive) {
        loadSettings();
        cliVsync_ = opt_.vsync >= 0;
        cliFpsLimit_ = opt_.fpsLimit >= 0.0f;
        if (opt_.quality < 0 && gs_.quality >= 0) opt_.quality = gs_.quality;
    } else {
        opt_.iniPath.clear();   // offline modes never read or write the player's settings
    }
    const bool visible = opt_.mode == AppOptions::Mode::Interactive || opt_.mode == AppOptions::Mode::Benchmark ||
                         opt_.mode == AppOptions::Mode::BenchShots;
    if (!initWindow(visible)) return 1;
    // v7 (WP-G): an --only list of game_* shots renders the POV / game-mode figures (AppGame.cpp).
    if (opt_.mode == AppOptions::Mode::Capture && opt_.only.find(",game_") != std::string::npos) return captureGame();
    if (opt_.mode == AppOptions::Mode::Capture && opt_.only.find(",play_") != std::string::npos) return capturePlay();
    if (opt_.mode == AppOptions::Mode::Capture) return captureMode();
    if (opt_.mode == AppOptions::Mode::Benchmark) return benchmarkMode();
    if (opt_.mode == AppOptions::Mode::BenchShots) return benchShotsMode();
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

std::unique_ptr<Scene> App::makeScene(bool nightStart) const {
    const bool interactive = opt_.mode == AppOptions::Mode::Interactive;
    // Interactive: the automatic cycle from late afternoon (dusk comes within a minute); offline modes: the v5 night.
    const float hour = nightStart ? 23.0f : opt_.hour >= 0.0f ? opt_.hour : (interactive ? 16.5f : 23.0f);
    const bool autoCycle = !nightStart && (opt_.timeGiven ? opt_.autoCycle : interactive);
    SceneOptions o = SceneOptions::v6(opt_.seed, hour, autoCycle);
    o.garrison = opt_.garrison;
    o.residents = opt_.garrison ? Garrison::kMaxResidents : 0;   // v8: the barracks hall is lived in
    o.randomRoutes = opt_.seed != 0;  // --seed 0 = the legacy v5 run (fixed seeds, legacy routes, ideal sensor)
    auto scene = std::make_unique<Scene>(o);
    if (opt_.cycleSeconds > 0.0f) {
        if (DayCycle* clock = sa::dayCycle(*scene)) {
            // Scale the day and night rates together so one full cycle (incl. the slower twilight) lasts cycleSeconds.
            DayCycleConfig cfg = clock->config();
            const float k = opt_.cycleSeconds / std::max(DayCycle::cycleSeconds(cfg), 1.0f);
            cfg.daySecPerHour *= k;
            cfg.nightSecPerHour *= k;
            if (!sa::setDayCycleConfig(*clock, cfg)) std::fprintf(stderr, "--cycle-seconds: DayCycle has no setConfig(); ignored\n");
        }
    }
    return scene;
}

void App::restartWorld(float warmUpSeconds, bool nightStart) {
    scene_ = makeScene(nightStart);
    detection_ = Detection{};
    possess(-1);
    feed_.clear();
    hudEvents_.reset();
    feedMissiles_ = feedScorches_ = 0;
    feedKills_ = -1;
    scriptClock_ = -1.0f;
    scriptFirePrev_ = scriptActionPrev_ = false;
    sensors_.clear();  // re-seeded by the next step: a restart replays the same sensor noise
    for (int u = 0; u < kUavCount; ++u) framesTotal_[u] = framesDropped_[u] = 0;
    renderer_->resetHistory();
    // v7 game camera: nothing to ride along with in the new world.
    if (camera_.mode == CameraMode::Pov) camera_.mode = CameraMode::Orbit;
    povKey_ = 0;
    povHold_ = -1.0f;
    possessedRover_ = -1;
    pickerOpen_ = false;
    prevView_ = ViewMemo{};
    speedMeter_.clear();
    rig_.cancel();
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
        const DirectorScript ds = directorScript(scriptClock_, scriptCut_);
        if (ds.hour >= 0.0f)  // the director owns the clock (manual time): every run shows the same times of day
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                clock->setAuto(false);
                clock->setHour(ds.hour);
            }
        in = ds.in;
        const bool fire = in.fire, action = in.actionPressed;
        in.firePressed = fire && !scriptFirePrev_;       // levels in the script, edges for the object
        in.actionPressed = action && !scriptActionPrev_;
        scriptFirePrev_ = fire;
        scriptActionPrev_ = action;
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
    } else if (possessedRover_ >= 0) {  // v7: the player drives an intruder rover
        in = held_;
        in.firePressed = firePressedEdge_;
        in.actionPressed = actionEdge_;
        firePressedEdge_ = actionEdge_ = false;
        aimYawAcc_ = aimPitchAcc_ = 0.0f;
        ip = &in;
        index = -2;
    }
}

void App::step(float dt) {
    stepSim(dt);
    stepVision();
    joinVision();   // offline modes: the results are in place right after the step (the HUD feed of this frame)
}

void App::stepSim(float dt) {
    if (pendingVision_.active) stepVision();   // never two steps' images in flight
    joinVision();   // v9 M1: the previous tick's CV results enter the world here, before this tick's update
    syncWalkWorld();
    if (!paused_) {
        ControlInput in;
        const ControlInput* ip = nullptr;
        int index = -1;
        gatherControl(in, ip, index);
        // v8: a possessed soldier moves game-like (first person: strafe + mouse turn; third person: camera-relative).
        if (ip == &in && index == possessed_ && scriptClock_ < 0.0f && possessedSoldierIndex() >= 0 &&
            (opt_.mode == AppOptions::Mode::Interactive || playCapture_))
            shapePlayerInput(in, dt);
        if (index == -2) {  // v7: the rover input goes to Scene's player-driven intruder, no controllable gets it
            scene_->setPlayerIntruder(possessedRover_, ip);
            ip = nullptr;
            index = -1;
        } else {
            scene_->setPlayerIntruder(-1, nullptr);
        }
        sa::update(*scene_, dt, detection_, ip, index);
        if (audioLive_) audio_.pollEvents(*scene_);  // events of every simulation step reach the mixer
        hudEvents_.consume(scene_->events(), scene_->time());
        if (scriptClock_ == 0.0f && scene_->mission().state().wave > 0) {
            // The director starts (video / cinematic K / capture script): announce the running wave on screen.
            GameEvent announce;
            announce.type = GameEventType::WaveStart;
            announce.value = scene_->mission().state().wave;
            announce.time = scene_->time();
            hudEvents_.consume({announce}, scene_->time());
        }
        if (scriptClock_ >= 0.0f) scriptClock_ += dt;
        updateCombatFeed();
    }
    scene_->explode = lerpf(scene_->explode, exploded_ ? 1.0f : 0.0f, std::min(1.0f, 4.0f * dt));
    scene_->updateWorld();
    // The UAV looks at the world through its own thermal camera; the tracker only sees these pixels.
    // v6, per UAV and frame: lost frame? -> gimbal pointing jitter (the image is rendered from the TRUE, jittered pose)
    // -> sensor model (PSF, fixed pattern, NETD noise, stuck pixels) -> CV image stage with blob classes -> only the
    // Target blobs go to the squad's tracker, geolocated with the REPORTED (unjittered) gimbal pose, so pointing
    // error becomes measurement error exactly as on a real platform. Nothing reads the rovers' true positions.
    cvStats_ = {};
    if (static_cast<int>(sensors_.size()) != kUavCount) {  // per-run sensors (fixed 101 / 102 for the legacy seed 0)
        sensors_.clear();
        for (int u = 0; u < kUavCount; ++u) {
            const uint32_t k = 100u + static_cast<uint32_t>(u);
            sensors_.emplace_back(opt_.seed != 0 ? deriveSeed(opt_.seed, k) : k + 1u);
            sensors_.back().nuc();  // start-up shutter calibration: without it stuck pixels look like hot cores
        }
    }
    // Each sensor keeps its own random sequence (drop, jitter, then noise in stepVision), and nothing below changes
    // the world, so rendering both cameras before either image is processed gives the same run as before.
    pendingVision_ = PendingVision{};
    pendingVision_.active = true;
    for (int u = 0; u < kUavCount; ++u) {
        if (paused_) {
            renderer_->beginUavCamera(*scene_, cvStats_, u, nullptr, u);
            pendingVision_.rendered[u] = true;
            continue;
        }
        ++framesTotal_[u];
        SensorModel& sensor = sensors_[u];
        if (sensor.dropFrame()) {  // the frame never arrives: no measurement, the tracker coasts
            ++framesDropped_[u];
            continue;
        }
        const Mat4 reported = scene_->cameraWorld(u);
        float jyaw = 0.0f, jpitch = 0.0f;
        sensor.gimbalJitter(jyaw, jpitch);
        const Mat4 truePose = jitterCamera(reported, jyaw, jpitch);
        renderer_->beginUavCamera(*scene_, cvStats_, u, &truePose, u);
        pendingVision_.rendered[u] = true;
        pendingVision_.reported[u] = reported;
    }
}

void App::stepVision(bool collectNow) {
    if (pendingVision_.collectLater) {
        pendingVision_.collectLater = false;
        pendingVision_.active = true;
        collectNow = true;
    }
    if (!pendingVision_.active) return;
    pendingVision_.active = false;
    // Hand the commands queued so far (the main frame, when called after renderFrame) to the GPU, so it renders
    // while the CPU runs the sensor model and CV (Mesa d3d12 would otherwise hold them until the swap).
    glFlush();
    // v9 M1: images still on their way (GPU behind): collect them at the next tick instead of waiting here.
    bool ready = true;
    for (int u = 0; u < kUavCount; ++u)
        if (pendingVision_.rendered[u] && !renderer_->uavCameraReady(u)) ready = false;
    if (!ready && !collectNow) {
        pendingVision_.collectLater = true;
        return;
    }
    // v9 M1: read the images back here (GL thread); the sensor model, CV and tracker run on the vision worker while
    // this thread presents and starts the next frame. joinVision applies the results at the next tick.
    VisionJob job;
    job.width = kCvWidth;
    job.height = kCvHeight;
    job.paused = paused_;
    for (int u = 0; u < kUavCount && u < VisionJob::kUavs; ++u) {
        if (!pendingVision_.rendered[u]) continue;
        job.rendered[u] = true;
        job.reported[u] = pendingVision_.reported[u];
        renderer_->finishUavCamera(u, job.image[u]);
    }
    if (!visionWorker_) {
        const char* env = std::getenv("TS_VISION_THREAD");
        visionWorker_ = std::make_unique<VisionWorker>(sensors_, tracker_, !(env && env[0] == '0'));
    }
    visionWorker_->submit(std::move(job));
}

void App::joinVision() {
    if (pendingVision_.collectLater) stepVision(true);   // the images left behind by the last frame: read them now
    if (!visionWorker_ || !visionWorker_->inFlight()) return;
    VisionResult r = visionWorker_->join();
    for (int u = 0; u < kUavCount && u < VisionResult::kUavs; ++u) {
        if (!r.valid[u]) continue;
        (u == 0 ? uavImage_ : uavImage2_) = std::move(r.image[u]);
        if (r.paused) continue;
        cvFrame_[u] = r.stats[u];
        scene_->submitVision(u, r.targets[u], r.reported[u]);
        uavBlobs_[u] = std::move(r.persons[u]);  // HUD 'PERS' brackets
    }
    if (r.hasDetection) detection_ = r.detection;  // v4 single-target view (UAV 0 feed)
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
    possessAlt_ = false;
    releaseRover();  // v7: one thing under control at a time
    tpPoseValid_ = false;   // v8: the spring arm re-centres on the next subject
    aiming_ = false;
    jumpEdge_ = false;
    alignYaw_ = 0.0f;
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

void App::possessNextSoldier() {
    const Garrison* g = sa::garrison(*scene_);
    if (!g || g->count() == 0) return;
    for (int k = 0; k < g->count(); ++k) {  // next soldier after the last one possessed (skips missing entries)
        soldierCursor_ = (soldierCursor_ + 1) % g->count();
        const int index = sa::soldierIndex(*scene_, soldierCursor_);
        if (index >= 0) {
            possess(index);
            return;
        }
    }
}

void App::toggleSight() {
    // v7: the tank and the UAV have a third camera (commander hatch / belly): chase -> sight -> alt -> chase.
    PovSource src;
    if (camera_.possessSight && !possessAlt_ && currentSource(src) && src.hasAlt) {
        possessAlt_ = true;
        camera_.possessSight = false;
        return;
    }
    if (possessAlt_) {
        possessAlt_ = false;
        return;
    }
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
    lastAspect_ = aspect;
    lastW_ = w;
    lastH_ = h;
    // v7: every other view goes through the game camera rig (blend between POVs, FOV kick); v6 views pass through.
    CameraView cam = fpv ? uavCameraView(*scene_, aspect, thermalFovDeg_) : composeView(aspect);
    if (fpv) {
        shownPose_ = RigPose::fromView(cam.view, cam.position, thermalFovDeg_, cam.nearZ);
        shownValid_ = true;
    }
    if (audioLive_) {  // listener = the camera the player sees through
        const double now = nowSeconds();
        const float frameDt = audioClock_ < 0.0 ? 1.0f / 60.0f : static_cast<float>(std::min(0.1, now - audioClock_));
        audioClock_ = now;
        const Vec3 fwd{-cam.view.at(2, 0), -cam.view.at(2, 1), -cam.view.at(2, 2)};
        const Vec3 up{cam.view.at(1, 0), cam.view.at(1, 1), cam.view.at(1, 2)};
        audio_.setMasterVolume(paused_ ? 0.0f : 1.0f);
        // v7: listener = the camera; when it follows an object (riding along / control, no blend running) the object's
        // velocity gives clean Doppler.
        audio::ListenerState ls;
        ls.position = cam.position;
        ls.forward = fwd;
        ls.up = up;
        PovSource src;
        if (!rig_.active() && currentSource(src) && length(src.velocity) > 0.0f) {
            ls.velocity = src.velocity;
            ls.hasVelocity = true;
        }
        // v7 WP-L: the possessed vehicle's engine follows the throttle; first person inside it -> cockpit / hull filter.
        if (camera_.mode == CameraMode::Possess && possessed_ >= 0) {
            ls.selfControllable = possessed_;
            ls.selfInterior = camera_.possessSight && !possessAlt_;
            ls.selfThrottle = held_.forward;
        }
        if (possessedRover_ >= 0) {
            ls.selfRover = possessedRover_;
            ls.selfInterior = povView_ != PovView::Chase && !rig_.active();
            ls.selfThrottle = held_.forward;
        }
        audio_.update(*scene_, ls, frameDt);
    }
    RenderSettings s = settings_;
    s.hideUav = fpv || (camera_.mode == CameraMode::UavCamera);
    bool uavView = fpv || camera_.mode == CameraMode::UavCamera;
    // v7: riding a UAV's gimbal / belly camera hides the airframes (they sit on the lens); jump letterbox.
    if (camera_.mode == CameraMode::Pov && povHold_ < 0.0f && povSrc_.kind == PovKind::Uav && povView_ != PovView::Chase) {
        s.hideUav = true;
        uavView = povView_ == PovView::First;
    }
    if (letterbox_ > s.letterbox) s.letterbox = letterbox_;
    if (camera_.mode == CameraMode::Cinematic) {
        const DirectorShot shot = directorShot(*scene_, camera_.directorTime, camera_.directorCut);
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
    // v7 game mode: a stronger kick from blasts near the viewer.
    if (!uavView)
        if (const Combat* combat = sa::combat(*scene_))
            applyShake(cam, std::min(1.0f, combat->shake(cam.position) * (gameMode_ ? 1.6f : 1.0f)), scene_->time());
    // v7: a soldier's eyes are the camera: rifle and hands in front of them, head and arms off the lens.
    int vmSoldier = -1;
    const bool viewmodel = !fpv && firstPersonSoldier(vmSoldier);
    if (viewmodel) applyViewmodel(vmSoldier, shownPose_);
    // v8: third person squeezed against a wall (arm shorter than a head): the camera would sit inside the character,
    // so the character is not drawn for this frame (as third-person games do).
    std::vector<std::pair<SceneNode*, bool>> selfHidden;
    if (controllingSoldier() && !camera_.possessSight && tpPoseValid_ && tp_.arm() < 0.45f)
        if (SceneNode* root = sa::controllables(*scene_)[possessed_]->root()) {
            std::vector<SceneNode*> stack{root};
            while (!stack.empty()) {
                SceneNode* n = stack.back();
                stack.pop_back();
                selfHidden.push_back({n, n->visible});
                n->visible = false;
                for (auto& c : n->children) stack.push_back(c.get());
            }
        }
    renderer_->beginFrame(*scene_, cam, w, h, s, stats_);
    drawOverlays(cam);  // drawn into the HDR scene so they are depth-tested and anti-aliased with it
    renderer_->endFrame(static_cast<GLuint>(target), w, h, scene_->time());
    for (auto& [n, v] : selfHidden) n->visible = v;
    if (viewmodel) restoreViewmodel();
    if (settings_.view == ViewMode::Diagnostic && settings_.showLegend) {
        const int size = h / 7;
        renderer_->drawPrimitiveLegend(w - Renderer::kLegendCount * size - 20, 40, size, static_cast<float>(nowSeconds()), stats_);
        glViewport(0, 0, w, h);
    }
    if (camera_.mode == CameraMode::Cinematic) drawCinematicCaption(w, h);
    else if (withHud && showHud_) drawHud(cam, w, h);
    // v9 M2: the pause menu / help also over the cinematic and with the HUD off (F3).
    if (withHud && (menuOpen_ || showHelp_) && (camera_.mode == CameraMode::Cinematic || !showHud_)) {
        renderer_->hud().begin(w, h);
        if (menuOpen_) drawPauseMenu(w, h);
        else drawHelp(w, h);
        renderer_->hud().end();
    }
}

void App::applyCinematic() {
    if (camera_.mode != CameraMode::Cinematic) return;
    const DirectorShot shot = directorShot(*scene_, camera_.directorTime, camera_.directorCut);
    settings_.view = shot.thermal ? ViewMode::Thermal : ViewMode::Visible;
    settings_.fade = shot.fade;
    settings_.letterbox = 0.5f * (1.0f - (16.0f / 9.0f) / 2.39f);  // 2.39:1 inside a 16:9 frame
    if (shot.fpvHud) thermalFovDeg_ = shot.fovDeg;
}

void App::drawCinematicCaption(int w, int h) {
    const DirectorShot shot = directorShot(*scene_, camera_.directorTime, camera_.directorCut);
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 1080.0f;
    const float bar = settings_.letterbox * static_cast<float>(h);
    hud.begin(w, h);
    if (shot.fpvHud || shot.gameHud) {  // FLIR symbology over the gimbal view and / or the game HUD
        const float aspect = static_cast<float>(w) / static_cast<float>(h > 0 ? h : 1);
        CameraView cv;
        if (shot.uavCamera) {
            cv = uavCameraView(*scene_, aspect, shot.fovDeg);
        } else {
            cv.view = lookAt(shot.eye, shot.target, {0, 1, 0});
            cv.proj = perspective(radians(shot.fovDeg), aspect, 0.05f, 600.0f);
            cv.position = shot.eye;
        }
        hud.setPalette(settings_.palette);
        if (shot.fpvHud) drawThermalFpvHud(cv, w, h);
        if (shot.gameHud) {
            const float gs = static_cast<float>(h) / 900.0f;
            const float inset = shot.fpvHud ? std::max(0.0f, bar + 10.0f * gs - 92.0f * gs) : bar;
            drawGameHud(cv, w, h, shot.fpvHud, inset);
        }
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
        std::vector<std::pair<const SceneNode*, float>> all(std::begin(frames), std::end(frames));
        // v6: every joint group of one controllable (the soldier's FK chain: pelvis, spine, head, shoulders, hips...)
        const std::vector<Controllable*> ctl = sa::controllables(*scene_);
        if (axesControllable_ >= 0 && axesControllable_ < static_cast<int>(ctl.size()) && ctl[axesControllable_])
            if (const SceneNode* root = ctl[axesControllable_]->root()) {
                std::vector<const SceneNode*> stack{root};
                while (!stack.empty()) {
                    const SceneNode* n = stack.back();
                    stack.pop_back();
                    if (!n->isLeaf()) all.push_back({n, 0.12f});
                    for (const auto& ch : n->children) stack.push_back(ch.get());
                }
            }
        for (const auto& [node, len] : all) {
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
        if (!planRoutesOnly_) {
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
        // v6 soldiers' duty routes (centripetal Catmull-Rom splines), sampled every 0.5 m, cyan-green.
        if (const Garrison* g = sa::garrison(*scene_)) {
            for (const CatmullRomPath& r : g->routes()) {
                if (r.empty()) continue;
                std::vector<LineVertex> curve;
                for (float d = 0.0f; d <= r.length() + 1e-3f; d += 0.5f) curve.push_back({r.pointAtDistance(d) + Vec3{0, 0.1f, 0}, {0.3f, 1.0f, 0.75f}});
                if (r.closed() && !curve.empty()) curve.push_back(curve.front());
                renderer_->drawLines(curve, GL_LINE_STRIP, vp, 0.9f, false);
            }
        }
        // v6 random routes: every live rover's chain spawn -> via points -> objective (yellow), passed vias dimmed.
        for (const auto& in : scene_->intruders()) {
            if (in->life != Intruder::Life::Alive) continue;
            const std::vector<Vec3>& via = in->hide.via();
            std::vector<LineVertex> chain;
            chain.push_back({in->state.position + Vec3{0, 0.12f, 0}, {1.0f, 0.85f, 0.2f}});
            for (size_t k = static_cast<size_t>(std::max(in->hide.viaReached(), 0)); k < via.size(); ++k)
                chain.push_back({via[k] + Vec3{0, 0.12f, 0}, {1.0f, 0.85f, 0.2f}});
            chain.push_back({in->objective + Vec3{0, 0.12f, 0}, {1.0f, 0.3f, 0.2f}});
            renderer_->drawLines(chain, GL_LINE_STRIP, vp, 0.9f, false);
            std::vector<LineVertex> marks;
            for (const Vec3& v : via) {
                marks.push_back({v + Vec3{-0.4f, 0.12f, 0}, {1.0f, 0.85f, 0.2f}});
                marks.push_back({v + Vec3{0.4f, 0.12f, 0}, {1.0f, 0.85f, 0.2f}});
                marks.push_back({v + Vec3{0, 0.12f, -0.4f}, {1.0f, 0.85f, 0.2f}});
                marks.push_back({v + Vec3{0, 0.12f, 0.4f}, {1.0f, 0.85f, 0.2f}});
            }
            renderer_->drawLines(marks, GL_LINES, vp, 1.0f, false);
        }
    }
    // v8: a ring on the ground around the selected object (and a fainter one under the hovered one).
    {
        const std::vector<PovSource> list = listPovSources(*scene_, cam.position);
        auto ring = [&](uint32_t key, const Vec3& colour) {
            const int i = findPov(list, key);
            if (i < 0 || camera_.mode == CameraMode::Possess) return;
            const PovSource& src = list[i];
            const float r = std::max(0.45f, src.radius * 0.9f);
            const float y = src.kind == PovKind::Soldier ? src.position.y + 0.04f : src.centre.y - src.radius * 0.8f;
            std::vector<LineVertex> v;
            for (int k = 0; k <= 48; ++k) {
                const float a = 6.2831853f * static_cast<float>(k) / 48.0f;
                v.push_back({Vec3{src.position.x + r * std::cos(a), y, src.position.z + r * std::sin(a)}, colour});
            }
            renderer_->drawLines(v, GL_LINE_STRIP, vp, 1.0f, true);
        };
        if (hoverKey_ != 0 && hoverKey_ != selKey_) ring(hoverKey_, {1.4f, 1.5f, 1.6f});
        if (selKey_ != 0) ring(selKey_, {0.5f, 3.2f, 1.0f});
    }
    if (settings_.depthTest) glEnable(GL_DEPTH_TEST);
}

}  // namespace ts
