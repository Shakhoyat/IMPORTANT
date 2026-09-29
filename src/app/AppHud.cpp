// Heads-up display: status text, heat scale, target marker, UAV camera feed, legend and help.
#include <cstdio>
#include <string>

#include "app/App.h"
#include "rendering/ThermalPalette.h"

namespace ts {

namespace {

const Rgba kWhite{0.92f, 0.95f, 1.0f, 0.95f};
const Rgba kDim{0.62f, 0.68f, 0.75f, 0.85f};
const Rgba kRed{1.0f, 0.25f, 0.18f, 1.0f};
const Rgba kAmber{1.0f, 0.75f, 0.15f, 1.0f};
const Rgba kGreen{0.35f, 1.0f, 0.45f, 1.0f};
const Rgba kPanel{0.0f, 0.0f, 0.0f, 0.45f};

// Same stops as thermalPalette() in the shaders.
Rgba paletteCpu(int palette, float t) {
    const Vec3 c = thermalPaletteCpu(palette, t);
    return {c.x, c.y, c.z, 1.0f};
}

Rgba modeColor(DroneMode m) {
    return m == DroneMode::Track ? kRed : (m == DroneMode::Reacquire ? kAmber : kGreen);
}

bool project(const CameraView& cam, const Vec3& p, int w, int h, float& sx, float& sy) {
    const Vec4 c = (cam.proj * cam.view) * Vec4{p.x, p.y, p.z, 1.0f};
    if (c.w <= 0.01f) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(w);
    sy = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<float>(h);
    return true;
}

}  // namespace

void App::drawTargetMarker(const CameraView& cam, int w, int h) {
    const DroneState& d = scene_->drone;
    if (!d.hasEstimate || d.mode == DroneMode::Search || camera_.mode == CameraMode::UavCamera) return;
    Hud& hud = renderer_->hud();
    float sx, sy;
    if (!project(cam, d.targetEstimate + Vec3{0, 0.25f, 0}, w, h, sx, sy)) return;
    const float dist = length(d.targetEstimate - cam.position);
    const float half = clampf(0.55f / dist * static_cast<float>(h), 10.0f, 120.0f);
    const Rgba c = modeColor(d.mode);
    hud.brackets(sx - half, sy - half, 2 * half, 2 * half, half * 0.45f, c);
    const float ts = 13.0f * static_cast<float>(h) / 900.0f;
    hud.text(sx - half, sy - half - ts - 4, d.mode == DroneMode::Track ? "TGT LOCK" : "LAST KNOWN", ts, c);
}

void App::drawUavFeed(int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float fw = 320.0f * s, fh = 240.0f * s, x = static_cast<float>(w) - fw - 20.0f * s, y = 20.0f * s;
    hud.rect(x - 4 * s, y - 4 * s, fw + 8 * s, fh + 30 * s, kPanel);
    hud.heatImage(x, y, fw, fh, renderer_->uavCameraTexture());
    hud.frame(x, y, fw, fh, kDim);
    hud.line(x + fw / 2 - 10 * s, y + fh / 2, x + fw / 2 + 10 * s, y + fh / 2, kDim);
    hud.line(x + fw / 2, y + fh / 2 - 10 * s, x + fw / 2, y + fh / 2 + 10 * s, kDim);
    if (detection_.found) {
        const auto px = [&](float u) { return x + (u * 0.5f + 0.5f) * fw; };
        const auto py = [&](float v) { return y + (1.0f - (v * 0.5f + 0.5f)) * fh; };
        const float pad = 3.0f * s;
        hud.frame(px(detection_.boxMinU) - pad, py(detection_.boxMaxV) - pad,
                  px(detection_.boxMaxU) - px(detection_.boxMinU) + 2 * pad,
                  py(detection_.boxMinV) - py(detection_.boxMaxV) + 2 * pad, kRed);
        hud.line(x + fw / 2, y + fh / 2, px(detection_.u), py(detection_.v), Rgba{1.0f, 0.3f, 0.2f, 0.6f});
    }
    char line[96];
    std::snprintf(line, sizeof(line), "UAV IR CAM %dx%d  %s  BLOBS %d", kCvWidth, kCvHeight,
                  droneModeName(scene_->drone.mode), detection_.blobCount);
    hud.text(x, y + fh + 6 * s, line, 15 * s, modeColor(scene_->drone.mode));
}

void App::drawHelp(int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const char* lines[] = {
        "MOUSE DRAG  orbit / look        WHEEL  zoom",
        "C  camera: orbit, free, chase, UAV camera   K  cinematic",
        "W A S D PgUp PgDn  move (free camera)",
        "V  view: thermal, visible, primitives, z-buffer",
        "X  edges: solid, hidden-line, x-ray wireframe",
        "Z  depth test on/off     I  indexed/non-indexed",
        "O F G P  shading: per-object, flat, gouraud, phong",
        "1 2 3 4  moon / beacon / searchlight / practical lights",
        "F5  quality LOW/HIGH/ULTRA   B  PBR/Blinn-Phong",
        "N  v1 renderer (no post)     + -  exposure",
        "R  rain on/off               T  thermal palette",
        "E  explode composites    H  hierarchy axes",
        "M  MicroMouse flood field + LFR guide line",
        "U  UAV camera feed   TAB  HUD   SPACE  pause",
        "F12  screenshot          ESC  quit",
    };
    const float lh = 22.0f * s, pw = 600.0f * s, ph = lh * 16.0f;
    const float x = (static_cast<float>(w) - pw) / 2, y = (static_cast<float>(h) - ph) / 2;
    hud.rect(x - 12 * s, y - 12 * s, pw + 24 * s, ph + 24 * s, Rgba{0, 0, 0, 0.75f});
    hud.text(x, y, "CONTROLS", 18 * s, kAmber);
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i)
        hud.text(x, y + lh * static_cast<float>(i + 1), lines[i], 15 * s, kWhite);
}

void App::drawHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    hud.begin(w, h);
    hud.setPalette(settings_.palette);
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    char line[160];

    hud.text(22 * s, 18 * s, "THERMALSENSE", 26 * s, kWhite);
    hud.text(22 * s, 48 * s, "AUTONOMOUS UAV PATROL  //  F1 HELP", 13 * s, kDim);
    std::snprintf(line, sizeof(line), "VIEW %-10s EDGES %-11s SHADING %s", viewModeName(settings_.view),
                  edgeModeName(settings_.edges), overrideName(settings_.shading));
    hud.text(22 * s, 74 * s, line, 14 * s, kWhite);
    std::snprintf(line, sizeof(line), "CAM %-11s Z-TEST %-4s DRAW %s   L0 %s  L1 %s  L2 %s", cameraModeName(camera_.mode),
                  settings_.depthTest ? "ON" : "OFF", settings_.indexed ? "INDEXED" : "NON-INDEXED",
                  settings_.moonOn ? "ON" : "--", settings_.beaconOn ? "ON" : "--", settings_.spotOn ? "ON" : "--");
    hud.text(22 * s, 94 * s, line, 14 * s, kDim);

    if (settings_.view == ViewMode::Thermal) {
        hud.brackets(12 * s, 12 * s, W - 24 * s, H - 24 * s, 40 * s, Rgba{1, 1, 1, 0.35f});
        const float bx = W - 44 * s, by = H * 0.35f, bh = H * 0.30f;
        for (int i = 0; i < 64; ++i) {
            const float t0 = static_cast<float>(i) / 64.0f;
            hud.rect(bx, by + bh * (1.0f - t0 - 1.0f / 64.0f), 14 * s, bh / 64.0f + 0.5f, paletteCpu(settings_.palette, t0));
        }
        hud.frame(bx, by, 14 * s, bh, kDim);
        hud.text(bx - 12 * s, by - 20 * s, "HOT", 13 * s, kWhite);
        hud.text(bx - 60 * s, by - 40 * s, paletteName(settings_.palette), 12 * s, kDim);
        hud.text(bx - 16 * s, by + bh + 6 * s, "COLD", 13 * s, kWhite);
    }
    if (settings_.view == ViewMode::Diagnostic && settings_.showLegend) {
        const float size = static_cast<float>(h / 7);
        const float lx = W - Renderer::kLegendCount * size - 20, ly = H - 40 - size;
        const char* names[Renderer::kLegendCount] = {"CUBE", "SPHERE", "CYLINDER", "CONE"};
        const PrimitiveType types[Renderer::kLegendCount] = {PrimitiveType::Cube, PrimitiveType::Sphere,
                                                             PrimitiveType::Cylinder, PrimitiveType::Cone};
        hud.text(lx, ly - 24 * s, "PRIMITIVE LIBRARY  (E = EXPLODE)", 14 * s, kWhite);
        for (int i = 0; i < Renderer::kLegendCount; ++i) {
            const Vec3 c = primitiveColor(types[i]);
            hud.text(lx + size * static_cast<float>(i) + 8 * s, ly + size + 6 * s, names[i], 15 * s, Rgba{c.x, c.y, c.z, 1});
        }
    }
    drawTargetMarker(cam, w, h);
    if (showFeed_ && camera_.mode != CameraMode::UavCamera) drawUavFeed(w, h);

    const DroneState& d = scene_->drone;
    const RoverState& r = scene_->rover;
    std::snprintf(line, sizeof(line), "UAV   %-9s alt %.1f m  gimbal %+4.0f/%3.0f deg  est (%.1f, %.1f)",
                  droneModeName(d.mode), d.position.y, d.gimbalYawDeg, d.gimbalPitchDeg, d.targetEstimate.x,
                  d.targetEstimate.z);
    hud.text(22 * s, H - 70 * s, line, 14 * s, modeColor(d.mode));
    std::string ir;
    for (bool b : r.sensors) ir += b ? '#' : '.';
    std::snprintf(line, sizeof(line), "ROVER %-9s %.2f m/s  IR [%s]  pos (%.1f, %.1f)", r.hidden ? "HIDDEN" : "EXPOSED",
                  0.5f * (r.leftSpeed + r.rightSpeed), ir.c_str(), r.position.x, r.position.z);
    hud.text(22 * s, H - 48 * s, line, 14 * s, r.hidden ? kGreen : kAmber);
    const std::string rate = fps_ > 0.0 ? std::to_string(static_cast<int>(fps_ + 0.5)) + " FPS" : "OFFLINE RENDER";
    std::snprintf(line, sizeof(line), "%s   %d DRAW CALLS   %lld TRIANGLES", rate.c_str(),
                  stats_.drawCalls + cvStats_.drawCalls, stats_.triangles + cvStats_.triangles);
    hud.text(22 * s, H - 26 * s, line, 13 * s, kDim);
    if (paused_) hud.text(W / 2 - 40 * s, 20 * s, "PAUSED", 20 * s, kAmber);
    if (showHelp_) drawHelp(w, h);
    hud.end();
}

}  // namespace ts
