// Interactive mode: window loop, keyboard / mouse controls, window title and F12 screenshots (GLFW).
#include <glad/gl.h>

#include "app/App.h"

#if !defined(TS_NO_GLFW)
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
    char title[320];
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
    std::snprintf(title, sizeof(title),
                  "ThermalSense | %s %s%s | %s | %s | cam %s%s%s | UAV %s | rover %s | fx %s | %.0f FPS | %d draws | %lld tris",
                  viewModeName(settings_.view), qualityName(settings_.quality), thermal, edgeModeName(settings_.edges),
                  overrideName(settings_.shading), cameraModeName(camera_.mode), possessed_ >= 0 ? " " : "", possessedName(),
                  droneModeName(scene_->drone.mode), sa::roverHidden(*scene_) ? "HIDDEN" : "EXPOSED", fx, fps, stats_.drawCalls + cvStats_.drawCalls,
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
        // Possessed object: sample the held keys (edges and mouse look come from the callbacks).
        if (camera_.mode == CameraMode::Possess) {
            const auto down = [this](int k) { return glfwGetKey(window_, k) == GLFW_PRESS ? 1.0f : 0.0f; };
            held_.forward = down(GLFW_KEY_W) - down(GLFW_KEY_S);
            held_.turn = down(GLFW_KEY_D) - down(GLFW_KEY_A);
            held_.lift = down(GLFW_KEY_PAGE_UP) - down(GLFW_KEY_PAGE_DOWN);
            held_.aimYaw = down(GLFW_KEY_RIGHT) - down(GLFW_KEY_LEFT);
            held_.aimPitch = down(GLFW_KEY_UP) - down(GLFW_KEY_DOWN);
            held_.fire = down(GLFW_KEY_ENTER) > 0.0f || down(GLFW_KEY_KP_ENTER) > 0.0f || down(GLFW_KEY_LEFT_CONTROL) > 0.0f;
            held_.boost = down(GLFW_KEY_LEFT_SHIFT) > 0.0f || down(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
        }
        // Walk: WASD, Shift = run.
        syncWalkWorld();
        if (camera_.mode == CameraMode::Walk) {
            const auto down = [this](int k) { return glfwGetKey(window_, k) == GLFW_PRESS ? 1.0f : 0.0f; };
            const bool run = down(GLFW_KEY_LEFT_SHIFT) > 0.0f || down(GLFW_KEY_RIGHT_SHIFT) > 0.0f;
            camera_.moveWalk(down(GLFW_KEY_W) - down(GLFW_KEY_S), down(GLFW_KEY_D) - down(GLFW_KEY_A), dt, run);
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
        case GLFW_KEY_C:
            if (camera_.mode == CameraMode::Possess) toggleSight();
            else camera_.cycle();
            break;
        case GLFW_KEY_K:
            if (camera_.mode == CameraMode::Cinematic) {
                camera_.mode = CameraMode::Orbit;
                scriptClock_ = -1.0f;
                settings_.letterbox = 0.0f;
                settings_.fade = 1.0f;
            } else {
                // The sequence is scripted against a fresh world (same start as the recorded video).
                possess(-1);
                camera_.mode = CameraMode::Orbit;
                restartWorld(8.0f);
                camera_.mode = CameraMode::Cinematic;
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
        case GLFW_KEY_LEFT_BRACKET: cyclePossession(-1); break;
        case GLFW_KEY_RIGHT_BRACKET: cyclePossession(+1); break;
        case GLFW_KEY_BACKSPACE: possess(-1); break;
        case GLFW_KEY_BACKSLASH: actionEdge_ = true; break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_LEFT_CONTROL:
            firePressedEdge_ = true;
            if (camera_.mode == CameraMode::Walk) interact();
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
        case GLFW_KEY_F5: settings_.quality = static_cast<Quality>((static_cast<int>(settings_.quality) + 1) % kQualityCount); break;
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

void App::onMouseButton(int button, int) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
        const bool wasDragging = dragging_;
        // Left or right button held = look around (right only counts in Walk / Free).
        dragging_ = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                    (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
                     (camera_.mode == CameraMode::Walk || camera_.mode == CameraMode::Free));
        if (dragging_ && !wasDragging) glfwGetCursorPos(window_, &lastX_, &lastY_);
    }
}

void App::onCursor(double x, double y) {
    if (dragging_ && camera_.mode == CameraMode::Possess) {  // mouse drag = aim delta of the possessed object
        aimYawAcc_ += static_cast<float>(x - lastX_) * 0.15f;
        aimPitchAcc_ -= static_cast<float>(y - lastY_) * 0.15f;
    } else if (dragging_) {
        camera_.drag(static_cast<float>(x - lastX_), static_cast<float>(y - lastY_));
    }
    lastX_ = x;
    lastY_ = y;
}

void App::onScroll(double dy) {
    if (opt_.mode != AppOptions::Mode::Interactive) return;
    if (settings_.view == ViewMode::Thermal && thermalFpv_) {
        // Thermal FPV zoom: WFOV 40 -> MFOV 20 -> NFOV 8 (wheel up = narrower).
        static const float kSteps[3] = {40.0f, 20.0f, 8.0f};
        int idx = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(g_fovTarget - kSteps[i]) < std::fabs(g_fovTarget - kSteps[idx])) idx = i;
        idx = std::max(0, std::min(2, idx + (dy > 0 ? 1 : dy < 0 ? -1 : 0)));
        g_fovTarget = clampf(kSteps[idx], 5.0f, 40.0f);
    } else {
        camera_.zoom(static_cast<float>(dy));
    }
}

}  // namespace ts

#else  // headless-only build

namespace ts {
void App::interactiveLoop() {}
void App::onKey(int, int, int) {}
void App::onMouseButton(int, int) {}
void App::onCursor(double, double) {}
void App::onScroll(double) {}
}  // namespace ts

#endif
