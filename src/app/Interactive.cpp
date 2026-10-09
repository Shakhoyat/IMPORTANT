// Interactive mode: window loop, keyboard / mouse controls, window title and F12 screenshots (GLFW).
#include <glad/gl.h>

#include "app/App.h"

#if !defined(TS_NO_GLFW)
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "rendering/FrameExplorer.h"  // v7 WP-I
#include "rendering/ImageWriter.h"
#include "rendering/ThermalPalette.h"
#include "scene/Director.h"

namespace ts {

namespace {
float g_fovTarget = 40.0f;   // thermal FPV zoom target (wheel steps 40 -> 20 -> 8), eased by the loop
float g_nucClosed = -1.0f;   // seconds since the NUC shutter closed; < 0 = open
unsigned g_nucSeed = 12345u;
constexpr float kNucDuration = 0.4f;
// Deterministic pseudo-random NUC interval, 25..40 s.
float nextNucInterval() {
    g_nucSeed = g_nucSeed * 1664525u + 1013904223u;
    return 25.0f + 15.0f * static_cast<float>((g_nucSeed >> 8) & 0xFFFF) / 65535.0f;
}
// v7 WP-I: the Frame Explorer (` key). Never freed: its GL objects must not be deleted after the context is gone.
FrameExplorer& wpiExplorer() {
    static FrameExplorer* e = new FrameExplorer();
    return *e;
}
// v7 WP-I end
}  // namespace

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
    char title[400];
    // Compact switch list: uppercase = on, lowercase = off (TAA, SSR, flare, auto-exposure, DoF, foliage, particles).
    const auto sw = [](bool on, char c) { return on ? c : static_cast<char>(c + 32); };
    char fx[16];
    std::snprintf(fx, sizeof(fx), "%c%c%c%c%c%c%c", sw(settings_.taaOn, 'T'), sw(settings_.ssrOn, 'S'), sw(settings_.flareOn, 'F'),
                  sw(settings_.autoExposureOn, 'A'), sw(settings_.dof > 0.0f, 'D'), sw(settings_.foliageOn, 'L'),
                  sw(settings_.particlesOn, 'J'));
    char thermal[48] = "";
    if (settings_.view == ViewMode::Thermal)
        std::snprintf(thermal, sizeof(thermal), thermalFpv_ ? " FPV pal %d fov %.0f" : " EXT pal %d", settings_.palette,
                      thermalFovDeg_);
    const DayCycle* clock = sa::dayCycle(*scene_);
    const std::string when = clock ? clock->clockText() : std::string("--:--");
    std::snprintf(title, sizeof(title),
                  "ThermalSense | %s | seed %u | %s %s%s | %s | %s | cam %s%s%s | UAV %s | rover %s | fx %s | %.0f FPS | %d draws | %lld tris",
                  when.c_str(), opt_.seed, viewModeName(settings_.view), qualityName(settings_.quality), thermal, edgeModeName(settings_.edges),
                  overrideName(settings_.shading), cameraModeName(camera_.mode), possessed_ >= 0 ? " " : "", possessedName(),
                  droneModeName(scene_->drone.mode), sa::roverHidden(*scene_) ? "HIDDEN" : "EXPOSED", fx, fps, stats_.drawCalls + cvStats_.drawCalls,
                  stats_.triangles + cvStats_.triangles);
    glfwSetWindowTitle(window_, title);
}

void App::interactiveLoop() {
    glfwSwapInterval(opt_.vsync >= 0 ? opt_.vsync : 1);
    if (!opt_.inputScript.empty() && !loadInputScript()) return;
    double last = glfwGetTime(), titleTimer = 0.0;
    int frames = 0;
    while (!glfwWindowShouldClose(window_)) {
        const double now = glfwGetTime();
        if (script_.active()) {   // v9 M0: scripted input (the clock starts at the first frame)
            if (scriptT0_ < 0.0) scriptT0_ = now;
            runScript(now - scriptT0_);
        }
        if (loopT_.start < 0.0) loopT_.start = now;
        const float dt = static_cast<float>(std::min(now - last, 0.05));
        frameMs_[frameHead_] = static_cast<float>(1000.0 * (now - last));   // v7 WP-Q: HUD frame-time graph
        frameHead_ = (frameHead_ + 1) % kFrameHistory;
        governQuality(now);                                                  // v7 WP-Q: auto preset
        last = now;

        // v7 WP-I: while the Frame Explorer is open the world stays frozen and the explorer owns the screen.
        if (wpiExplorer().isOpen()) {
            int ew = 0, eh = 0;
            glfwGetFramebufferSize(window_, &ew, &eh);
            if (ew > 0 && eh > 0) wpiExplorer().frame(*renderer_, 0, ew, eh, dt);
            glfwSwapBuffers(window_);
            glfwPollEvents();
            continue;
        }
        // v7 WP-I end

        // v7 WP-G: game mode captures the cursor for mouse look in first-person / free views (shown again for the picker).
        {
            // v8: controlling a soldier always captures the mouse (look / aim / fire like a shooter; Esc lets go).
            const bool look = !pickerOpen_ && !showHelp_ &&
                              ((gameMode_ && (camera_.mode == CameraMode::Free || camera_.mode == CameraMode::Walk ||
                                              camera_.mode == CameraMode::Pov || camera_.mode == CameraMode::Possess)) ||
                               controllingSoldier());
            if (look != cursorCaptured_) {
                glfwSetInputMode(window_, GLFW_CURSOR, look ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
                cursorPos(lastX_, lastY_);
                cursorCaptured_ = look;
            }
        }
        // Free-fly movement (continuous keys). v7: Home's fly camera glides (smoothed velocity, wheel = speed, Shift x3).
        if (camera_.mode == CameraMode::Free && flyCam_) {
            const auto down = [this](int k) { return keyDown(k) ? 1.0f : 0.0f; };
            const bool boost = down(GLFW_KEY_LEFT_SHIFT) > 0.0f || down(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
            held_.boost = boost;
            camera_.freePos = camera_.freePos + fly_.step(camera_.freeYawDeg, camera_.freePitchDeg, down(GLFW_KEY_W) - down(GLFW_KEY_S),
                                                          down(GLFW_KEY_D) - down(GLFW_KEY_A),
                                                          down(GLFW_KEY_PAGE_UP) - down(GLFW_KEY_PAGE_DOWN), boost, dt);
        } else if (camera_.mode == CameraMode::Free) {
            const float speed = 6.0f * dt;
            const auto down = [this](int k) { return keyDown(k) ? 1.0f : 0.0f; };
            camera_.moveFree((down(GLFW_KEY_W) - down(GLFW_KEY_S)) * speed, (down(GLFW_KEY_D) - down(GLFW_KEY_A)) * speed,
                             (down(GLFW_KEY_PAGE_UP) - down(GLFW_KEY_PAGE_DOWN)) * speed);
        }
        // v7 WP-G: riding along, any drive / aim key takes control (possession, or the rover's wheel).
        if (camera_.mode == CameraMode::Pov && possessedRover_ < 0 && !pickerOpen_ && !paused_) {
            const int keys[] = {GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT,
                                GLFW_KEY_RIGHT, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN};
            for (int k : keys)
                if (keyDown(k)) {
                    takeControl();
                    break;
                }
        }
        // Possessed object: sample the held keys (edges and mouse look come from the callbacks).
        if (camera_.mode == CameraMode::Possess || possessedRover_ >= 0) {
            const auto down = [this](int k) { return keyDown(k) ? 1.0f : 0.0f; };
            held_.forward = down(GLFW_KEY_W) - down(GLFW_KEY_S);
            held_.turn = down(GLFW_KEY_D) - down(GLFW_KEY_A);
            held_.lift = down(GLFW_KEY_PAGE_UP) - down(GLFW_KEY_PAGE_DOWN);
            held_.aimYaw = down(GLFW_KEY_RIGHT) - down(GLFW_KEY_LEFT);
            held_.aimPitch = down(GLFW_KEY_UP) - down(GLFW_KEY_DOWN);
            held_.fire = down(GLFW_KEY_ENTER) > 0.0f || down(GLFW_KEY_KP_ENTER) > 0.0f || down(GLFW_KEY_LEFT_CONTROL) > 0.0f;
            held_.boost = down(GLFW_KEY_LEFT_SHIFT) > 0.0f || down(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
            // v8: a soldier under control: left mouse fires, right mouse aims (zoom / over the shoulder).
            const bool soldierMouse = cursorCaptured_ && controllingSoldier();
            if (soldierMouse && mouseDown(GLFW_MOUSE_BUTTON_LEFT)) held_.fire = true;
            aiming_ = soldierMouse && mouseDown(GLFW_MOUSE_BUTTON_RIGHT);
            if (pickerOpen_) held_.aimYaw = held_.aimPitch = held_.forward = held_.turn = 0.0f, held_.fire = false;  // keys drive the list
        } else {
            aiming_ = false;
        }
        // v8: what is under the mouse pointer (name tag; a click selects it).
        if (!cursorCaptured_ && !pickerOpen_ && camera_.mode != CameraMode::Possess && camera_.mode != CameraMode::Cinematic) {
            double cx = 0.0, cy = 0.0;
            int ww = 1, wh = 1;
            cursorPos(cx, cy);
            glfwGetWindowSize(window_, &ww, &wh);
            if (cx >= 0.0 && cy >= 0.0 && cx < ww && cy < wh) updateHover(cx / std::max(ww, 1), cy / std::max(wh, 1));
            else hoverKey_ = 0;
        } else {
            hoverKey_ = 0;
        }
        // Walk: WASD, Shift = run.
        syncWalkWorld();
        if (camera_.mode == CameraMode::Walk) {
            const auto down = [this](int k) { return keyDown(k) ? 1.0f : 0.0f; };
            const bool run = down(GLFW_KEY_LEFT_SHIFT) > 0.0f || down(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
            camera_.moveWalk(down(GLFW_KEY_W) - down(GLFW_KEY_S), down(GLFW_KEY_D) - down(GLFW_KEY_A), dt, run);
        }
        // v6 manual clock: ',' / '.' held scrub the time of day (a press in auto mode switched the clock to manual).
        if (DayCycle* clock = sa::dayCycle(*scene_)) {
            const auto down = [this](int k) { return keyDown(k) ? 1.0f : 0.0f; };
            const float dir = down(GLFW_KEY_PERIOD) - down(GLFW_KEY_COMMA);
            if (dir != 0.0f && !clock->autoRun() && !paused_) clock->scrub(dir * clock->config().scrubHoursPerSec * dt);
        }
        // Thermal sensor: display zoom easing and the periodic NUC shutter (thermal view only).
        thermalFovDeg_ = lerpf(thermalFovDeg_, g_fovTarget, std::min(1.0f, dt * 10.0f));
        if (settings_.view == ViewMode::Thermal) {
            if (g_nucClosed < 0.0f) {
                nucTimer_ -= dt;
                if (nucTimer_ <= 0.0f) g_nucClosed = 0.0f;
            }
            if (g_nucClosed >= 0.0f) {
                g_nucClosed += dt;
                // Ramp shut in 0.06 s, hold, ramp open over the last 0.1 s.
                settings_.nucShutter = clampf(std::min(g_nucClosed / 0.06f, (kNucDuration - g_nucClosed) / 0.1f), 0.0f, 1.0f);
                if (g_nucClosed >= kNucDuration) {
                    g_nucClosed = -1.0f;
                    settings_.nucShutter = 0.0f;
                    nucTimer_ = nextNucInterval();
                }
            }
        } else if (g_nucClosed >= 0.0f) {
            g_nucClosed = -1.0f;
            settings_.nucShutter = 0.0f;
        }
        const double tSim = glfwGetTime();
        stepSim(dt);           // v7 WP-Q: the UAV images are collected after the main frame (stepVision below)
        updateGameCamera(dt);  // v7 WP-G: POV follow, blends, game feel, soldier rifle
        loopT_.sim += glfwGetTime() - tSim;
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
        const double tRender = glfwGetTime();
        if (w > 0 && h > 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            renderFrame(w, h, true);
            if (!scriptShots_.empty() || !scriptLogs_.empty()) scriptAfterRender(w, h);
        }
        const double tVision = glfwGetTime();
        loopT_.render += tVision - tRender;
        stepVision();          // v7 WP-Q: CV + tracker on the CPU while the GPU draws the frame
        loopT_.vision += glfwGetTime() - tVision;
        ++frames;
        titleTimer += dt;
        if (titleTimer >= 0.25) {
            fps_ = frames / titleTimer;
            updateTitle(fps_);
            frames = 0;
            titleTimer = 0.0;
        }
        const double tSwap = glfwGetTime();
        glfwSwapBuffers(window_);
        glfwPollEvents();
        loopT_.swap += glfwGetTime() - tSwap;
        ++loopT_.frames;
    }
}

// ------------------------------------------------------------------------------------------- v9 M0: input script
bool App::keyDown(int key) const { return (window_ && glfwGetKey(window_, key) == GLFW_PRESS) || script_.keyHeld(key); }

bool App::mouseDown(int button) const {
    return (window_ && glfwGetMouseButton(window_, button) == GLFW_PRESS) || script_.buttonHeld(button);
}

void App::cursorPos(double& x, double& y) const {
    if (scriptPointer_ && window_) {
        int ww = 1, wh = 1;
        glfwGetWindowSize(window_, &ww, &wh);
        x = scriptPx_ * ww;
        y = scriptPy_ * wh;
        return;
    }
    x = y = 0.0;
    if (window_) glfwGetCursorPos(window_, &x, &y);
}

bool App::loadInputScript() {
    std::FILE* f = std::fopen(opt_.inputScript.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "--input-script: cannot open %s\n", opt_.inputScript.c_str());
        return false;
    }
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
    std::fclose(f);
    std::vector<ScriptEvent> events;
    std::string error;
    if (!parseInputScript(text, events, error)) {
        std::fprintf(stderr, "--input-script %s: %s\n", opt_.inputScript.c_str(), error.c_str());
        return false;
    }
    std::printf("Input script %s: %zu events, frames to %s\n", opt_.inputScript.c_str(), events.size(), opt_.framesDir.c_str());
    std::fflush(stdout);
    std::error_code ec;
    std::filesystem::create_directories(opt_.framesDir, ec);
    script_.load(std::move(events));
    return true;
}

void App::runScript(double t) {
    for (const ScriptEvent& e : script_.due(t)) {
        switch (e.kind) {
            case ScriptEvent::Kind::Key: onKey(e.code, e.action ? GLFW_PRESS : GLFW_RELEASE, 0); break;
            case ScriptEvent::Kind::Button: onMouseButton(e.code, e.action ? GLFW_PRESS : GLFW_RELEASE); break;
            case ScriptEvent::Kind::Look: onCursor(lastX_ + e.x, lastY_ + e.y); break;
            case ScriptEvent::Kind::Pointer: {
                scriptPointer_ = true;
                scriptPx_ = e.x;
                scriptPy_ = e.y;
                double x = 0.0, y = 0.0;
                cursorPos(x, y);
                onCursor(x, y);
                break;
            }
            case ScriptEvent::Kind::Shot: scriptShots_.push_back(e.text); break;
            case ScriptEvent::Kind::Log: scriptLogs_.push_back(e.text); break;
            case ScriptEvent::Kind::Quit:
                std::printf("script: quit at %.2f s\n", t);
                std::fflush(stdout);
                glfwSetWindowShouldClose(window_, 1);
                break;
        }
    }
}

void App::scriptAfterRender(int w, int h) {
    for (const std::string& name : scriptShots_) {
        std::vector<uint8_t> bottomUp(static_cast<size_t>(w) * h * 3), rgb(bottomUp.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, bottomUp.data());
        const size_t row = static_cast<size_t>(w) * 3;
        for (int y = 0; y < h; ++y)
            std::copy(bottomUp.begin() + (h - 1 - y) * row, bottomUp.begin() + (h - y) * row, rgb.begin() + y * row);
        const std::string path = opt_.framesDir + "/" + name + ".png";
        const bool ok = writePng(path, w, h, rgb);
        std::printf("script shot %s %s | %s\n", ok ? "wrote" : "FAILED", path.c_str(), stateLine().c_str());
    }
    for (const std::string& label : scriptLogs_) {
        const double span = glfwGetTime() - loopT_.start;
        const int n = std::max(loopT_.frames, 1);
        std::printf("script log %s | %s | %.1f fps, ms/frame: sim %.2f render %.2f vision %.2f swap %.2f (%d frames)\n",
                    label.c_str(), stateLine().c_str(), loopT_.frames / std::max(span, 1e-6), 1000.0 * loopT_.sim / n,
                    1000.0 * loopT_.render / n, 1000.0 * loopT_.vision / n, 1000.0 * loopT_.swap / n, loopT_.frames);
        loopT_ = LoopTiming{};
        loopT_.start = glfwGetTime();
    }
    if (!scriptShots_.empty() || !scriptLogs_.empty()) std::fflush(stdout);
    scriptShots_.clear();
    scriptLogs_.clear();
}

void App::onKey(int key, int action, int) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;  // benchmarks ignore input
    // v7 WP-I: ` freezes the frame into the Frame Explorer; while it is open it gets every key (` / Esc close it).
    if (wpiExplorer().isOpen()) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) wpiExplorer().onKey(key);
        return;
    }
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        int ew = 0, eh = 0;
        glfwGetFramebufferSize(window_, &ew, &eh);
        if (ew <= 0 || eh <= 0) return;
        const float aspect = static_cast<float>(ew) / static_cast<float>(eh);
        const bool fpv = thermalFirstPerson();
        const CameraView cam = fpv ? uavCameraView(*scene_, aspect, thermalFovDeg_) : composeView(aspect);
        RenderSettings es = settings_;
        es.hideUav = fpv || camera_.mode == CameraMode::UavCamera;
        if (cursorCaptured_) {  // the explorer needs the mouse pointer
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            cursorCaptured_ = false;
        }
        // The live view's TAA history is already converged on this camera in the visible view: 3 frames suffice.
        const bool liveHistory = settings_.view == ViewMode::Visible && settings_.edges == EdgeMode::Solid && !fpv;
        wpiExplorer().setConvergence(liveHistory ? 3 : 8, liveHistory);
        wpiExplorer().setProgress([this, ew, eh](float f, const std::string& label) {  // progress overlay while it works
            wpiExplorer().drawProgress(*renderer_, 0, ew, eh, f, label);
            glfwSwapBuffers(window_);
        });
        if (wpiExplorer().open(*renderer_, *scene_, cam, es, ew, eh))
            std::printf("Frame Explorer: frame frozen in %.2f s (arrows: stages / layers, ` or Esc: back)\n", wpiExplorer().openSeconds());
        else
            std::printf("Frame Explorer: could not freeze the frame\n");
        return;
    }
    // v7 WP-I end
    // v7 WP-G: the POV picker owns the arrows, Enter and Esc while it is open (held keys repeat).
    if (pickerOpen_ && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        switch (key) {
            case GLFW_KEY_UP: pickerMove(-1); return;
            case GLFW_KEY_DOWN: pickerMove(1); return;
            case GLFW_KEY_PAGE_UP: pickerMove(-8); return;
            case GLFW_KEY_PAGE_DOWN: pickerMove(8); return;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER: pickerConfirm(); return;
            case GLFW_KEY_ESCAPE:
            case GLFW_KEY_F4: togglePicker(); return;  // closes (PickerClose cue)
            default: break;
        }
    }
    if (action != GLFW_PRESS) return;
    // v8: Enter takes control of the selected object (when nothing is under control yet).
    if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && selKey_ != 0 && camera_.mode != CameraMode::Possess &&
        possessedRover_ < 0 && camera_.mode != CameraMode::Walk) {
        controlSelected();
        return;
    }
    // v7 WP-G: possession keys blend the camera from the view on screen (End returns to it).
    const ViewMemo before = memo();
    const RigPose from = shownPose_;
    const int wasPossessed = possessed_;
    switch (key) {
        case GLFW_KEY_ESCAPE:   // v8: leave control / riding along / the selection first; quit when there is nothing left
            if (!releaseControl()) glfwSetWindowShouldClose(window_, 1);
            break;
        case GLFW_KEY_C:
            if (controllingSoldier()) {   // v8: first <-> third person (same as V)
                setViewThirdPerson(camera_.possessSight);
                break;
            }
            if (camera_.mode == CameraMode::Pov) {
                cycleView();  // v7: first person -> chase -> alt camera
                break;
            }
            if (camera_.mode == CameraMode::Possess) toggleSight();
            else camera_.cycle();
            flyCam_ = false;
            beginJump(from);
            break;
        case GLFW_KEY_K:
            if (camera_.mode == CameraMode::Cinematic) {
                camera_.mode = CameraMode::Orbit;
                scriptClock_ = -1.0f;
                settings_.letterbox = 0.0f;
                settings_.fade = 1.0f;
                // The director drove the clock by hand: give it back to the cycle the run was started with.
                if (DayCycle* clock = sa::dayCycle(*scene_)) clock->setAuto(opt_.timeGiven ? opt_.autoCycle : true);
            } else {
                // The sequence is scripted against a fresh world (same start as the recorded video).
                possess(-1);
                camera_.mode = CameraMode::Orbit;
                restartWorld(8.0f, true);
                camera_.mode = CameraMode::Cinematic;
                camera_.directorCut = scriptCut_ = DirectorCut::V6;
                camera_.directorTime = 0.0f;
                scriptClock_ = 0.0f;
            }
            break;
        case GLFW_KEY_5: possessSlot(Slot::Tank); break;
        case GLFW_KEY_6: possessSlot(Slot::Helicopter); break;
        case GLFW_KEY_7: possessSlot(Slot::AaGun); break;
        case GLFW_KEY_8: possessSlot(Slot::Radar); break;
        case GLFW_KEY_9: possessSlot(Slot::Doors); break;
        case GLFW_KEY_0: possessSlot(Slot::Uav); break;
        case GLFW_KEY_F2:  // v6: automatic day / night cycle <-> manual clock
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                clock->toggleAuto();
                std::printf("Day cycle %s at %s\n", clock->autoRun() ? "AUTO" : "MANUAL", clock->clockText().c_str());
            }
            break;
        case GLFW_KEY_COMMA:
        case GLFW_KEY_PERIOD:  // scrubbing is manual: a press in auto mode stops the clock (the loop scrubs while held)
            if (DayCycle* clock = sa::dayCycle(*scene_)) clock->setAuto(false);
            break;
        case GLFW_KEY_SLASH:
            if (DayCycle* clock = sa::dayCycle(*scene_)) clock->jumpToNextPhase();
            break;
        case GLFW_KEY_F3: possessNextSoldier(); break;
        case GLFW_KEY_LEFT_BRACKET: cyclePossession(-1); break;
        case GLFW_KEY_RIGHT_BRACKET: cyclePossession(+1); break;
        case GLFW_KEY_BACKSPACE:
            if (possessedRover_ >= 0 || possessed_ >= 0) audio_.uiCue(audio::UiCue::Release);
            if (possessedRover_ >= 0) releaseRover();  // v7: the rover drives itself again, the camera stays on it
            else possess(-1);
            break;
        case GLFW_KEY_BACKSLASH: actionEdge_ = true; break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_LEFT_CONTROL:
            firePressedEdge_ = true;
            if (camera_.mode == CameraMode::Walk) interact();
            break;
        case GLFW_KEY_V:
            if (camera_.mode == CameraMode::Possess && possessedSoldierIndex() >= 0) {   // v8: first <-> third person
                setViewThirdPerson(camera_.possessSight);
                break;
            }
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
        case GLFW_KEY_F5:
            settings_.quality = static_cast<Quality>((static_cast<int>(settings_.quality) + 1) % kQualityCount);
            qualityAuto_ = false;   // v7 WP-Q: a manual choice ends the auto preset governor
            break;
        case GLFW_KEY_B: settings_.pbr = !settings_.pbr; break;
        case GLFW_KEY_R: rainTarget_ = rainTarget_ > 0.5f ? 0.0f : 1.0f; break;
        case GLFW_KEY_T: settings_.palette = (settings_.palette + 1) % kPaletteCount; break;
        case GLFW_KEY_Y: thermalFpv_ = !thermalFpv_; break;
        case GLFW_KEY_Q: nucTimer_ = 0.0f; break;  // force a NUC (the loop closes the shutter)
        case GLFW_KEY_F6: settings_.taaOn = !settings_.taaOn; break;
        case GLFW_KEY_F7: settings_.ssrOn = !settings_.ssrOn; break;
        case GLFW_KEY_F8: settings_.flareOn = !settings_.flareOn; break;
        case GLFW_KEY_F9: settings_.autoExposureOn = !settings_.autoExposureOn; break;
        case GLFW_KEY_F10: settings_.dof = settings_.dof > 0.0f ? 0.0f : 1.0f; break;
        case GLFW_KEY_L: settings_.foliageOn = !settings_.foliageOn; break;
        case GLFW_KEY_J: settings_.particlesOn = !settings_.particlesOn; break;
        case GLFW_KEY_N: settings_.cinematic = !settings_.cinematic; break;
        case GLFW_KEY_EQUAL: settings_.exposure *= 1.15f; break;
        case GLFW_KEY_MINUS: settings_.exposure /= 1.15f; break;
        case GLFW_KEY_TAB: showHud_ = !showHud_; break;
        case GLFW_KEY_F1: showHelp_ = !showHelp_; break;
        case GLFW_KEY_SPACE:   // v8: a soldier under control jumps; otherwise Space pauses
            if (controllingSoldier()) jumpEdge_ = true;
            else paused_ = !paused_;
            break;
        case GLFW_KEY_PAUSE: paused_ = !paused_; break;
        case GLFW_KEY_F12: {
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            renderFrame(w, h, true);
            saveScreenshot(w, h);
            break;
        }
        // v7 WP-G: game camera
        case GLFW_KEY_F4: togglePicker(); break;
        case GLFW_KEY_HOME: enterFreeCam(); break;
        case GLFW_KEY_END: returnToPrevious(); break;
        case GLFW_KEY_INSERT: cyclePovJump(+1); break;
        case GLFW_KEY_DELETE: cyclePovJump(-1); break;
        case GLFW_KEY_F11:
            gameMode_ = !gameMode_;
            std::printf("Game mode %s\n", gameMode_ ? "ON" : "OFF");
            break;
        case GLFW_KEY_LEFT_ALT:  // possessed soldier: crouch <-> stand (Left Ctrl is fire)
            if (const int si = possessedSoldierIndex(); si >= 0)
                if (const Soldier* sd = dynamic_cast<const Soldier*>(sa::controllables(*scene_)[si])) crouchWant_ = sd->stance() == 1 ? 0 : 1;
            break;
        // v7 WP-G end
        // v7 WP-H: ray tracing modes (raster -> hybrid -> Whitted -> path -> light) and the ray debugger
        case GLFW_KEY_SEMICOLON:
            settings_.rtMode = rt::nextRtMode(settings_.rtMode);
            std::printf("Render mode: %s (%s)\n", rt::rtModeName(settings_.rtMode), rt::rtModeCaption(settings_.rtMode));
            break;
        case GLFW_KEY_APOSTROPHE:
            renderer_->setRayDebug(!renderer_->rayDebug());
            std::printf("Ray debugger %s\n", renderer_->rayDebug() ? "ON: click a pixel" : "OFF");
            break;
        // v7 WP-H end
        default: break;
    }
    const bool possessKey = (key >= GLFW_KEY_5 && key <= GLFW_KEY_9) || key == GLFW_KEY_0 || key == GLFW_KEY_LEFT_BRACKET ||
                            key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_F3 || key == GLFW_KEY_BACKSPACE;
    if (possessKey && possessed_ != wasPossessed) {
        noteJump(before, from);
        if (possessed_ >= 0) audio_.uiCue(audio::UiCue::Possess);
    }
}

void App::onMouseButton(int button, int action) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;
    // v7 WP-I: the Frame Explorer takes the mouse while it is open (left click = probe / drag the split).
    if (wpiExplorer().isOpen()) {
        wpiExplorer().onMouseButton(button == GLFW_MOUSE_BUTTON_LEFT ? 0 : button, action == GLFW_PRESS);
        return;
    }
    // v7 WP-I end
    // v7 WP-H: ray debugger: a left click traces that pixel (CPU) and shows its ray tree; it does not start a drag.
    if (renderer_->rayDebug() && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double cx = 0.0, cy = 0.0;
        int ww = 1, wh = 1;
        cursorPos(cx, cy);
        glfwGetWindowSize(window_, &ww, &wh);
        if (renderer_->rayDebugPick(*scene_, static_cast<float>(cx / std::max(ww, 1)), static_cast<float>(cy / std::max(wh, 1))))
            std::printf("Ray tree: %zu rays\n", renderer_->rayTree()->nodes.size());
        return;
    }
    // v7 WP-H end
    // v7 WP-G: a click on the POV picker jumps; the middle button jumps into the object under the cursor.
    if (action == GLFW_PRESS && (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE)) {
        double cx = 0.0, cy = 0.0;
        int ww = 1, wh = 1;
        cursorPos(cx, cy);
        glfwGetWindowSize(window_, &ww, &wh);
        const double nx = cx / std::max(ww, 1), ny = cy / std::max(wh, 1);
        if (button == GLFW_MOUSE_BUTTON_LEFT && pickerOpen_ && pickerClick(nx, ny)) return;
        if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (cursorCaptured_) pickPovAt(0.5, 0.5);  // game mode: the crosshair
            else pickPovAt(nx, ny);
            return;
        }
    }
    // v8: a left click (press and release without dragging) selects the object under the pointer.
    if (button == GLFW_MOUSE_BUTTON_LEFT && !cursorCaptured_) {
        double cx = 0.0, cy = 0.0;
        cursorPos(cx, cy);
        if (action == GLFW_PRESS) {
            pressX_ = cx;
            pressY_ = cy;
            pressValid_ = camera_.mode != CameraMode::Possess && camera_.mode != CameraMode::Cinematic;
        } else if (action == GLFW_RELEASE && pressValid_) {
            pressValid_ = false;
            if (std::hypot(cx - pressX_, cy - pressY_) < 5.0) {
                int ww = 1, wh = 1;
                glfwGetWindowSize(window_, &ww, &wh);
                selectAt(cx / std::max(ww, 1), cy / std::max(wh, 1));
            }
        }
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
        const bool wasDragging = dragging_;
        // Left or right button held = look around (right only counts in Walk / Free).
        dragging_ = mouseDown(GLFW_MOUSE_BUTTON_LEFT) ||
                    (mouseDown(GLFW_MOUSE_BUTTON_RIGHT) &&
                     (camera_.mode == CameraMode::Walk || camera_.mode == CameraMode::Free || camera_.mode == CameraMode::Pov));
        if (dragging_ && !wasDragging) cursorPos(lastX_, lastY_);
    }
}

void App::onCursor(double x, double y) {
    // v7 WP-I: Frame Explorer cursor in framebuffer pixels (window coordinates differ on high-DPI screens).
    if (wpiExplorer().isOpen()) {
        int ww = 1, wh = 1, fw = 1, fh = 1;
        glfwGetWindowSize(window_, &ww, &wh);
        glfwGetFramebufferSize(window_, &fw, &fh);
        wpiExplorer().onCursor(static_cast<float>(x * fw / std::max(ww, 1)), static_cast<float>(y * fh / std::max(wh, 1)));
        lastX_ = x;
        lastY_ = y;
        return;
    }
    // v7 WP-I end
    const bool look = dragging_ || cursorCaptured_;  // v7: game mode looks without a button held
    if (look && controllingSoldier() && !camera_.possessSight) {   // v8: third person: the mouse swings the camera arm
        tpLookYaw_ += static_cast<float>(x - lastX_) * 0.15f;
        tpLookPitch_ -= static_cast<float>(y - lastY_) * 0.15f;
    } else if (look && camera_.mode == CameraMode::Possess) {  // mouse drag = aim delta of the possessed object
        aimYawAcc_ += static_cast<float>(x - lastX_) * 0.15f;
        aimPitchAcc_ -= static_cast<float>(y - lastY_) * 0.15f;
    } else if (look && camera_.mode == CameraMode::Pov) {  // v7: free look while riding along
        povLookYaw_ = std::fmod(povLookYaw_ + static_cast<float>(x - lastX_) * 0.15f, 360.0f);
        povLookPitch_ = clampf(povLookPitch_ - static_cast<float>(y - lastY_) * 0.15f, -75.0f, 75.0f);
    } else if (look) {
        camera_.drag(static_cast<float>(x - lastX_), static_cast<float>(y - lastY_));
    }
    lastX_ = x;
    lastY_ = y;
}

void App::onScroll(double dy) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;
    if (wpiExplorer().isOpen()) {  // v7 WP-I: wheel = magnifier zoom
        wpiExplorer().onScroll(static_cast<float>(dy));
        return;
    }
    if (settings_.view == ViewMode::Thermal && thermalFpv_) {
        // Thermal FPV zoom: WFOV 40 -> MFOV 20 -> NFOV 8 (wheel up = narrower).
        static const float kSteps[3] = {40.0f, 20.0f, 8.0f};
        int idx = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(g_fovTarget - kSteps[i]) < std::fabs(g_fovTarget - kSteps[idx])) idx = i;
        idx = std::max(0, std::min(2, idx + (dy > 0 ? 1 : dy < 0 ? -1 : 0)));
        g_fovTarget = clampf(kSteps[idx], 5.0f, 40.0f);
    } else if (camera_.mode == CameraMode::Free && flyCam_) {
        fly_.wheel(static_cast<float>(dy));  // v7: the fly camera's speed
    } else {
        camera_.zoom(static_cast<float>(dy));
    }
}

}  // namespace ts

#else  // headless-only build

namespace ts {
void App::interactiveLoop() {}
bool App::keyDown(int key) const { return script_.keyHeld(key); }
bool App::mouseDown(int button) const { return script_.buttonHeld(button); }
void App::cursorPos(double& x, double& y) const { x = y = 0.0; }
bool App::loadInputScript() { return false; }
void App::runScript(double) {}
void App::scriptAfterRender(int, int) {}
void App::onKey(int, int, int) {}
void App::onMouseButton(int, int) {}
void App::onCursor(double, double) {}
void App::onScroll(double) {}
}  // namespace ts

#endif
