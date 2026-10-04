// Heads-up display: status text, heat scale, target marker, UAV camera feed, legend and help.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.h"
#include "objects/Terrain.h"
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
    const float s = std::min(static_cast<float>(h) / 900.0f, static_cast<float>(w) / 1300.0f);
    const char* left[] = {
        "VIEW & CAMERA",
        "MOUSE DRAG  orbit / look      WHEEL  zoom",
        "C  camera: orbit, free, chase, UAV, walk",
        "K  cinematic director (restarts the world, ~2 min)",
        "W A S D (+SHIFT run)  walk / free camera",
        "PgUp PgDn  free camera up / down",
        "ENTER / L-CTRL in walk  open / close nearest door",
        "V  view: thermal, visible, primitives, z-buffer",
        "THERMAL: Y first-person  T palette  WHEEL zoom  Q NUC",
        "X edges   Z depth test   I indexed   O F G P shading",
        "1 2 3 4  moon / beacon / searchlight / practicals",
        "F5 quality   F6 TAA  F7 SSR  F8 flare  F9 exposure",
        "F10 DoF   L foliage   J fireflies   N v1   + - exposure",
        "R rain   E explode   H axes   M flood field   B PBR",
        "U UAV feed   TAB HUD   SPACE pause   F12 shot   ESC quit",
    };
    const char* right[] = {
        "CONTROL (possess an object)",
        "5 tank   6 helicopter   7 AA gun   8 radar",
        "9 blast doors   0 UAV (manual drone)",
        "[  ]  previous / next controllable (incl. gate)",
        "BACKSPACE  release control",
        "W S  drive / throttle     A D  steer / yaw",
        "PgUp PgDn  lift (helicopter, UAV)",
        "ARROWS or MOUSE DRAG  aim turret / gun / gimbal",
        "ENTER or L-CTRL  fire (hold = automatic)",
        "BACKSLASH  action: start / land, open / close, mode",
        "SHIFT  boost",
        "C  chase view <-> first-person sight",
        "V T Y keep working while controlling",
        "SPACE  pause",
    };
    const float lh = 21.0f * s, colW = 600.0f * s, pw = colW * 2.0f + 30.0f * s, ph = lh * 16.0f;
    const float x = (static_cast<float>(w) - pw) / 2, y = (static_cast<float>(h) - ph) / 2;
    hud.rect(x - 12 * s, y - 12 * s, pw + 24 * s, ph + 24 * s, Rgba{0, 0, 0, 0.78f});
    hud.text(x, y, "CONTROLS (F1)", 17 * s, kAmber);
    for (size_t i = 0; i < sizeof(left) / sizeof(left[0]); ++i)
        hud.text(x, y + lh * static_cast<float>(i + 1), left[i], 14 * s, i == 0 ? kAmber : kWhite);
    for (size_t i = 0; i < sizeof(right) / sizeof(right[0]); ++i)
        hud.text(x + colW + 30 * s, y + lh * static_cast<float>(i + 1), right[i], 14 * s, i == 0 ? kAmber : kWhite);
}

bool App::thermalFirstPerson() const {
    if (camera_.mode == CameraMode::Possess)  // only the manual UAV's gimbal sight uses the FLIR view
        return possessedSlot_ == static_cast<int>(Slot::Uav) && camera_.possessSight && settings_.view == ViewMode::Thermal;
    return settings_.view == ViewMode::Thermal && thermalFpv_ && camera_.mode != CameraMode::Cinematic;
}

namespace {

void ring(Hud& hud, float cx, float cy, float r, int seg, Rgba c) {
    for (int i = 0; i < seg; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(seg);
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(seg);
        hud.line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, c);
    }
}

// Number following 'key' in 'text' (e.g. "AMMO 18"), or -1.
float parseAfter(const std::string& text, const char* key) {
    const size_t p = text.find(key);
    if (p == std::string::npos) return -1.0f;
    return static_cast<float>(std::atof(text.c_str() + p + std::strlen(key)));
}

void bar(Hud& hud, float x, float y, float w, float h, float frac, const char* label, float s, Rgba c) {
    hud.rect(x, y, w, h, Rgba{0, 0, 0, 0.5f});
    hud.rect(x, y, w * clampf(frac, 0.0f, 1.0f), h, c);
    hud.frame(x, y, w, h, kDim);
    hud.text(x + w + 6 * s, y - 1 * s, label, h, kDim);
}

}  // namespace

void App::drawCombatFeed(int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w);
    float y = (showFeed_ && camera_.mode != CameraMode::UavCamera ? 306.0f : 24.0f) * s;
    char line[64];
    const int kills = sa::kills(*scene_);
    if (kills >= 0) {
        std::snprintf(line, sizeof(line), "KILLS %d / %d", kills, static_cast<int>(sa::roverPositions(*scene_).size()));
        hud.text(W - hud.textWidth(line, 15 * s) - 24 * s, y, line, 15 * s, kAmber);
        y += 22 * s;
    }
    const float now = scene_->time();
    for (auto it = feed_.rbegin(); it != feed_.rend(); ++it) {
        const float age = now - it->time;
        const float a = clampf(1.0f - (age - 5.5f) / 2.5f, 0.0f, 1.0f);
        const bool kill = it->text == "TARGET DESTROYED";
        const Rgba c = kill ? Rgba{kRed.r, kRed.g, kRed.b, a} : Rgba{kWhite.r, kWhite.g, kWhite.b, 0.9f * a};
        hud.text(W - hud.textWidth(it->text, 13 * s) - 24 * s, y, it->text, 13 * s, c);
        y += 18 * s;
    }
}

// Possessed object: name / status / controls panel, sight reticles, tapes, ammo and reload bars.
void App::drawPossessHud(const CameraView& cam, int w, int h) {
    (void)cam;
    const std::vector<Controllable*> list = sa::controllables(*scene_);
    if (possessed_ < 0 || possessed_ >= static_cast<int>(list.size()) || !list[possessed_]) return;
    const Controllable& c = *list[possessed_];
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h), cx = W * 0.5f, cy = H * 0.5f;
    const std::string status = c.status();
    const CameraHint hint = c.camera(camera_.possessSight);
    const Rgba fg{0.55f, 1.0f, 0.6f, 0.95f}, dim{0.55f, 1.0f, 0.6f, 0.55f};
    char buf[96];

    // Control panel (bottom centre).
    const float pw = std::max(560.0f * s, hud.textWidth(c.controls(), 13 * s) + 30 * s);
    const float px = cx - pw / 2, py = H - 128 * s;
    hud.rect(px, py, pw, 100 * s, kPanel);
    hud.frame(px, py, pw, 100 * s, dim);
    hud.text(px + 12 * s, py + 8 * s, c.name(), 20 * s, kAmber);
    const char* view = camera_.possessSight ? "SIGHT" : "CHASE";
    hud.text(px + pw - hud.textWidth(view, 13 * s) - 12 * s, py + 12 * s, view, 13 * s, kDim);
    hud.text(px + 12 * s, py + 38 * s, status, 15 * s, kWhite);
    hud.text(px + 12 * s, py + 62 * s, c.controls(), 13 * s, kDim);
    hud.text(px + 12 * s, py + 80 * s, "BACKSPACE release   C chase / sight   [ ] next", 12 * s, kDim);

    // Ammo / reload bars parsed from the status line ("AMMO 18", "RELOAD 40%").
    const float ammo = parseAfter(status, "AMMO "), reload = parseAfter(status, "RELOAD ");
    float by = py - 20 * s;
    if (ammo >= 0.0f) {
        ammoMax_ = std::max(ammoMax_, ammo);
        std::snprintf(buf, sizeof(buf), "AMMO %.0f", ammo);
        bar(hud, px, by, 220 * s, 10 * s, ammoMax_ > 0.0f ? ammo / ammoMax_ : 0.0f, buf, 12 * s, ammo < 4.0f ? kRed : fg);
        by -= 18 * s;
    }
    if (reload >= 0.0f) {
        const float frac = reload > 1.0f ? reload / 100.0f : reload;
        bar(hud, px, by, 220 * s, 10 * s, frac, frac >= 0.99f ? "READY" : "RELOAD", 12 * s, frac >= 0.99f ? fg : kAmber);
    }

    if (!camera_.possessSight || !hint.sight) return;
    const int slot = possessedSlot_;
    if (slot == static_cast<int>(Slot::Tank)) {
        // Gunner's sight: optic vignette, cross with range stadia (hundreds of metres), lead marks.
        const float rr = H * 0.46f;
        hud.rect(0, 0, cx - rr, H, Rgba{0, 0, 0, 0.6f});
        hud.rect(cx + rr, 0, cx - rr, H, Rgba{0, 0, 0, 0.6f});
        ring(hud, cx, cy, rr, 72, dim);
        hud.line(cx - rr, cy, cx - 24 * s, cy, fg);
        hud.line(cx + 24 * s, cy, cx + rr, cy, fg);
        hud.line(cx, cy - rr * 0.5f, cx, cy - 24 * s, fg);
        hud.line(cx, cy + 24 * s, cx, cy + rr, fg);
        hud.rect(cx - 1.5f * s, cy - 1.5f * s, 3 * s, 3 * s, fg);
        for (int i = 1; i <= 6; ++i) {
            const float y = cy + 24 * s + 30 * s * static_cast<float>(i);
            const float half = (i % 2 ? 10.0f : 18.0f) * s;
            hud.line(cx - half, y, cx + half, y, fg);
            if (i % 2 == 0) {
                std::snprintf(buf, sizeof(buf), "%d", i / 2 + 1);
                hud.text(cx + 24 * s, y - 7 * s, buf, 13 * s, fg);
            }
        }
        for (int i = 1; i <= 4; ++i) {
            const float x = 60.0f * s * static_cast<float>(i);
            hud.line(cx - x, cy - 5 * s, cx - x, cy + 5 * s, dim);
            hud.line(cx + x, cy - 5 * s, cx + x, cy + 5 * s, dim);
        }
        hud.text(cx + rr - 170 * s, cy + rr - 30 * s, "GUNNER SIGHT  x8", 14 * s, fg);
    } else if (slot == static_cast<int>(Slot::AaGun)) {
        // Lead-ring reticle: two rings, tick marks, centre dot.
        ring(hud, cx, cy, 44 * s, 48, fg);
        ring(hud, cx, cy, 96 * s, 64, dim);
        for (int i = 0; i < 4; ++i) {
            const float a = 1.5707963f * static_cast<float>(i);
            hud.line(cx + std::cos(a) * 96 * s, cy + std::sin(a) * 96 * s, cx + std::cos(a) * 124 * s, cy + std::sin(a) * 124 * s, fg);
            hud.line(cx + std::cos(a) * 44 * s, cy + std::sin(a) * 44 * s, cx + std::cos(a) * 30 * s, cy + std::sin(a) * 30 * s, fg);
        }
        hud.rect(cx - 1.5f * s, cy - 1.5f * s, 3 * s, 3 * s, fg);
        hud.text(cx + 100 * s, cy + 100 * s, "LEAD", 13 * s, fg);
    } else if (slot == static_cast<int>(Slot::Helicopter)) {
        // Horizon line from the view pitch, altitude tape (left), speed tape (right), heading.
        const Vec3 d = normalize(hint.target - hint.eye);
        const float pitchDeg = degrees(std::asin(clampf(d.y, -1.0f, 1.0f)));
        const float ppd = H / hint.fovDeg;
        const float hy = cy + pitchDeg * ppd;
        hud.line(cx - 220 * s, hy, cx - 50 * s, hy, fg);
        hud.line(cx + 50 * s, hy, cx + 220 * s, hy, fg);
        for (int k = -2; k <= 2; ++k) {
            if (k == 0) continue;
            const float y = hy - static_cast<float>(k) * 10.0f * ppd;
            hud.line(cx - 40 * s, y, cx + 40 * s, y, dim);
            std::snprintf(buf, sizeof(buf), "%d", k * 10);
            hud.text(cx + 46 * s, y - 7 * s, buf, 12 * s, dim);
        }
        hud.line(cx - 18 * s, cy, cx - 6 * s, cy, fg);
        hud.line(cx + 6 * s, cy, cx + 18 * s, cy, fg);
        hud.line(cx, cy - 6 * s, cx, cy - 18 * s, fg);
        static Vec3 prev{};
        static float prevT = -1.0f, speed = 0.0f;
        const float t = scene_->time(), alt = c.position().y;
        if (prevT >= 0.0f && t > prevT + 1e-4f) speed += (length(c.position() - prev) / (t - prevT) - speed) * 0.1f;
        if (t != prevT) {
            prev = c.position();
            prevT = t;
        }
        const auto tape = [&](float x, float value, float step, const char* label, bool leftSide) {
            const float top = cy - 150 * s, bot = cy + 150 * s, ppu = (bot - top) / (step * 8.0f);
            hud.line(x, top, x, bot, dim);
            for (int k = -4; k <= 4; ++k) {
                const float v = std::round(value / step) * step + static_cast<float>(k) * step;
                const float y = cy - (v - value) * ppu;
                if (y < top || y > bot) continue;
                hud.line(x, y, x + (leftSide ? -8 : 8) * s, y, fg);
                std::snprintf(buf, sizeof(buf), "%.0f", v);
                hud.text(leftSide ? x - 14 * s - hud.textWidth(buf, 12 * s) : x + 12 * s, y - 6 * s, buf, 12 * s, dim);
            }
            std::snprintf(buf, sizeof(buf), "%s %.0f", label, value);
            hud.rect(leftSide ? x - 86 * s : x + 4 * s, cy - 10 * s, 82 * s, 20 * s, Rgba{0, 0, 0, 0.55f});
            hud.frame(leftSide ? x - 86 * s : x + 4 * s, cy - 10 * s, 82 * s, 20 * s, fg);
            hud.text(leftSide ? x - 82 * s : x + 8 * s, cy - 8 * s, buf, 13 * s, fg);
        };
        tape(cx - 280 * s, alt, 5.0f, "ALT", true);
        tape(cx + 280 * s, speed, 5.0f, "SPD", false);
        const float hdg = std::fmod(degrees(std::atan2(d.x, d.z)) + 360.0f, 360.0f);
        std::snprintf(buf, sizeof(buf), "HDG %03.0f", hdg);
        hud.text(cx - hud.textWidth(buf, 14 * s) / 2, 40 * s, buf, 14 * s, fg);
    } else {  // generic sight: open cross
        hud.line(cx - 30 * s, cy, cx - 8 * s, cy, fg);
        hud.line(cx + 8 * s, cy, cx + 30 * s, cy, fg);
        hud.line(cx, cy - 30 * s, cx, cy - 8 * s, fg);
        hud.line(cx, cy + 8 * s, cx, cy + 30 * s, fg);
    }
}

// First-person thermal (FLIR-style) overlay: white symbology, compass tape, gimbal scales, telemetry, target box.
void App::drawThermalFpvHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h), cx = W * 0.5f, cy = H * 0.5f;
    const DroneState& d = scene_->drone;
    const Rgba fg{0.95f, 0.97f, 1.0f, 0.9f}, dim{0.95f, 0.97f, 1.0f, 0.55f}, back{0, 0, 0, 0.30f};
    char buf[128];
    const float t = static_cast<float>(scene_->time());

    // Frame brackets and centre reticle (open cross, centre dot).
    hud.brackets(14 * s, 14 * s, W - 28 * s, H - 28 * s, 34 * s, dim);
    const float gap = 12 * s, arm = 30 * s;
    hud.line(cx - gap - arm, cy, cx - gap, cy, fg);
    hud.line(cx + gap, cy, cx + gap + arm, cy, fg);
    hud.line(cx, cy - gap - arm, cx, cy - gap, fg);
    hud.line(cx, cy + gap, cx, cy + gap + arm, fg);
    hud.rect(cx - 1, cy - 1, 2, 2, fg);

    // Heading from the camera view matrix: forward = -row 2 of the rotation.
    const Vec3 fwd{-cam.view.at(2, 0), -cam.view.at(2, 1), -cam.view.at(2, 2)};
    float hdg = degrees(std::atan2(fwd.x, fwd.z));
    hdg = std::fmod(hdg + 360.0f, 360.0f);

    // Compass tape (top): 60 degrees visible.
    {
        const float tw = 520 * s, ty = 34 * s, span = 60.0f, ppd = tw / span;
        hud.line(cx - tw / 2, ty + 14 * s, cx + tw / 2, ty + 14 * s, dim);
        for (int deg = static_cast<int>(std::floor((hdg - span / 2) / 10.0f)) * 10; deg <= hdg + span / 2; deg += 10) {
            const float x = cx + (static_cast<float>(deg) - hdg) * ppd;
            if (x < cx - tw / 2 || x > cx + tw / 2) continue;
            const int dn = ((deg % 360) + 360) % 360;
            const bool major = dn % 30 == 0;
            hud.line(x, ty + 14 * s, x, ty + (major ? 6 : 9) * s, fg);
            if (dn % 90 == 0) {
                const char* n = dn == 0 ? "N" : (dn == 90 ? "E" : (dn == 180 ? "S" : "W"));
                hud.text(x - hud.textWidth(n, 15 * s) / 2, ty - 12 * s, n, 15 * s, fg);
            } else if (major) {
                std::snprintf(buf, sizeof(buf), "%03d", dn);
                hud.text(x - hud.textWidth(buf, 12 * s) / 2, ty - 8 * s, buf, 12 * s, dim);
            }
        }
        hud.line(cx, ty + 15 * s, cx - 5 * s, ty + 22 * s, fg);
        hud.line(cx, ty + 15 * s, cx + 5 * s, ty + 22 * s, fg);
        std::snprintf(buf, sizeof(buf), "%03.0f", hdg);
        const float bw = 44 * s;
        hud.rect(cx - bw / 2, ty + 26 * s, bw, 20 * s, back);
        hud.frame(cx - bw / 2, ty + 26 * s, bw, 20 * s, fg);
        hud.text(cx - hud.textWidth(buf, 15 * s) / 2, ty + 29 * s, buf, 15 * s, fg);
    }

    // Gimbal elevation ladder (right), -90..+10 deg; the camera looks down so depression is negative.
    {
        const float lx = W - 110 * s, top = H * 0.25f, bot = H * 0.75f;
        const auto yOf = [&](float deg) { return top + (10.0f - deg) / 100.0f * (bot - top); };
        hud.line(lx, top, lx, bot, dim);
        for (int deg = -90; deg <= 10; deg += 10) {
            const bool major = deg % 30 == 0;
            hud.line(lx, yOf(static_cast<float>(deg)), lx - (major ? 10 : 5) * s, yOf(static_cast<float>(deg)), fg);
            if (major) {
                std::snprintf(buf, sizeof(buf), "%d", deg);
                hud.text(lx - 16 * s - hud.textWidth(buf, 12 * s), yOf(static_cast<float>(deg)) - 6 * s, buf, 12 * s, dim);
            }
        }
        const float el = clampf(-d.gimbalPitchDeg, -90.0f, 10.0f), my = yOf(el);
        hud.line(lx, my, lx + 10 * s, my - 6 * s, fg);
        hud.line(lx, my, lx + 10 * s, my + 6 * s, fg);
        hud.line(lx + 10 * s, my - 6 * s, lx + 10 * s, my + 6 * s, fg);
        std::snprintf(buf, sizeof(buf), "%+.1f", el);
        hud.text(lx + 14 * s, my - 7 * s, buf, 14 * s, fg);
        hud.text(lx - 20 * s, top - 22 * s, "GIMBAL EL", 12 * s, dim);
    }
    // Gimbal azimuth relative to the airframe (bottom), -180..180.
    {
        const float bw = 360 * s, by = H - 78 * s, ppd = bw / 360.0f;
        const float x0 = cx - bw / 2;
        hud.line(x0, by, x0 + bw, by, dim);
        for (int deg = -180; deg <= 180; deg += 30) {
            const float x = x0 + (static_cast<float>(deg) + 180.0f) * ppd;
            hud.line(x, by, x, by - (deg % 90 == 0 ? 8 : 4) * s, fg);
            if (deg % 90 == 0) {
                std::snprintf(buf, sizeof(buf), "%d", deg);
                hud.text(x - hud.textWidth(buf, 11 * s) / 2, by + 4 * s, buf, 11 * s, dim);
            }
        }
        float az = std::fmod(d.gimbalYawDeg + 180.0f, 360.0f);
        if (az < 0) az += 360.0f;
        az -= 180.0f;
        const float mx = x0 + (az + 180.0f) * ppd;
        hud.line(mx, by, mx - 5 * s, by - 9 * s, fg);
        hud.line(mx, by, mx + 5 * s, by - 9 * s, fg);
        hud.line(mx - 5 * s, by - 9 * s, mx + 5 * s, by - 9 * s, fg);
        std::snprintf(buf, sizeof(buf), "GIMBAL AZ %+.1f", az);
        hud.text(cx - hud.textWidth(buf, 12 * s) / 2, by - 26 * s, buf, 12 * s, dim);
    }

    // Left telemetry column.
    {
        static Vec3 prevPos{};
        static float prevT = -1.0f, gs = 0.0f;
        if (prevT >= 0.0f && t > prevT + 1e-4f) {
            const float dx = d.position.x - prevPos.x, dz = d.position.z - prevPos.z;
            const float inst = std::sqrt(dx * dx + dz * dz) / (t - prevT);
            gs += (inst - gs) * 0.15f;
        } else if (t < prevT) {
            gs = 0.0f;
        }
        if (t != prevT) { prevPos = d.position; prevT = t; }
        const float agl = d.position.y - terrainHeight(d.position.x, d.position.z);
        const int secs = (2 * 3600 + 14 * 60 + 37 + static_cast<int>(std::max(t, 0.0f))) % 86400;
        char rows[6][64];
        std::snprintf(rows[0], 64, "ALT  %6.1f M AGL", agl);
        std::snprintf(rows[1], 64, "GS   %6.1f M/S", gs);
        std::snprintf(rows[2], 64, "HDG  %6.0f DEG", hdg);
        std::snprintf(rows[3], 64, "MGRS 45Q VG %05d %05d",
                      (static_cast<int>(50000.0f + d.position.x * 10.0f) % 100000 + 100000) % 100000,
                      (static_cast<int>(50000.0f - d.position.z * 10.0f) % 100000 + 100000) % 100000);
        std::snprintf(rows[4], 64, "%02d:%02d:%02dL", secs / 3600, secs / 60 % 60, secs % 60);
        const float x = 40 * s, y = 100 * s, lh = 22 * s;
        hud.rect(x - 8 * s, y - 6 * s, 262 * s, lh * 5 + 8 * s, back);
        for (int i = 0; i < 5; ++i) hud.text(x, y + lh * static_cast<float>(i), rows[i], 15 * s, i == 3 ? dim : fg);
    }

    // Right sensor column.
    {
        const char* pal[] = {"IRON", "WHOT", "RNBW", "BHOT"};
        const float fov = thermalFovDeg_;
        const char* fl = fov >= 30.0f ? "WFOV" : (fov >= 14.0f ? "MFOV" : "NFOV");
        const float x = W - 240 * s, y = 100 * s, lh = 22 * s;
        hud.rect(x - 8 * s, y - 6 * s, 226 * s, lh * 4 + 8 * s, back);
        std::snprintf(buf, sizeof(buf), "IR   %s", pal[((settings_.palette % 4) + 4) % 4]);
        hud.text(x, y, buf, 15 * s, fg);
        std::snprintf(buf, sizeof(buf), "%s  FOV %.1f DEG", fl, fov);
        hud.text(x, y + lh, buf, 15 * s, fg);
        if (settings_.nucShutter > 0.05f && std::fmod(t, 0.5f) < 0.3f) hud.text(x, y + lh * 2, "NUC", 15 * s, kAmber);
        hud.text(x, y + lh * 3, "REC", 15 * s, fg);
        if (std::fmod(t, 1.0f) < 0.5f)
            hud.rect(x + 46 * s, y + lh * 3 + 3 * s, 10 * s, 10 * s, Rgba{1.0f, 0.15f, 0.1f, 1.0f});
    }

    // Tracking status box.
    {
        const char* label = d.mode == DroneMode::Track ? "TRK" : (d.mode == DroneMode::Reacquire ? "REACQ" : "SRCH");
        const Rgba c = modeColor(d.mode);
        const float bw = 84 * s, bh = 24 * s, x = W - 240 * s, y = 100 * s + 22 * s * 4 + 12 * s;
        hud.rect(x, y, bw, bh, back);
        hud.frame(x, y, bw, bh, c);
        hud.text(x + (bw - hud.textWidth(label, 15 * s)) / 2, y + 4 * s, label, 15 * s, c);
    }

    // Target box: CV image coordinates rescaled to the display FOV.
    if (detection_.found) {
        const float k = std::tan(radians(kCvFovYDeg * 0.5f)) / std::tan(radians(thermalFovDeg_ * 0.5f));
        // Display half-height in px = H/2 maps to tan(fov/2); the CV image is 4:3 with the same vertical FOV.
        const auto sx = [&](float u) { return cx + u * k * cy * (4.0f / 3.0f); };
        const auto sy = [&](float v) { return cy - v * k * cy; };
        float x0 = sx(detection_.boxMinU), x1 = sx(detection_.boxMaxU);
        float y0 = sy(detection_.boxMaxV), y1 = sy(detection_.boxMinV);
        const float pad = 6 * s;
        x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
        const float minSz = 36 * s;
        if (x1 - x0 < minSz) { const float m = (x0 + x1) * 0.5f; x0 = m - minSz / 2; x1 = m + minSz / 2; }
        if (y1 - y0 < minSz) { const float m = (y0 + y1) * 0.5f; y0 = m - minSz / 2; y1 = m + minSz / 2; }
        const Rgba c = modeColor(d.mode);
        hud.brackets(x0, y0, x1 - x0, y1 - y0, std::min(x1 - x0, y1 - y0) * 0.3f, c);
        hud.text(x0, y0 - 18 * s, "TGT", 14 * s, c);
        if (d.hasEstimate) {
            std::snprintf(buf, sizeof(buf), "RNG %.0f M", length(d.targetEstimate - cam.position));
            hud.text(x0, y1 + 5 * s, buf, 13 * s, c);
        }
    }

    hud.text(40 * s, H - 46 * s, "THERMALSENSE  UAV-1  LWIR 8-14um", 13 * s, fg);
    hud.text(40 * s, H - 28 * s, "Y: EXTERNAL VIEW   T: PALETTE   WHEEL: ZOOM", 13 * s, dim);
}

void App::drawHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    hud.begin(w, h);
    hud.setPalette(settings_.palette);
    if (thermalFirstPerson()) {
        drawThermalFpvHud(cam, w, h);
        drawCombatFeed(w, h);
        if (paused_) hud.text(w * 0.5f - 40 * h / 900.0f, 110.0f * h / 900.0f, "PAUSED", 20 * h / 900.0f, kAmber);
        if (showHelp_) drawHelp(w, h);
        hud.end();
        return;
    }
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    char line[160];

    hud.text(22 * s, 18 * s, "THERMALSENSE", 26 * s, kWhite);
    hud.text(22 * s, 48 * s, "MILITARY BASE  //  F1 HELP  //  5-0 CONTROL", 13 * s, kDim);
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
    std::snprintf(line, sizeof(line), "UAV   %-9s alt %.1f m  gimbal %+4.0f/%3.0f deg  est (%.1f, %.1f)",
                  droneModeName(d.mode), d.position.y, d.gimbalYawDeg, d.gimbalPitchDeg, d.targetEstimate.x,
                  d.targetEstimate.z);
    hud.text(22 * s, H - 70 * s, line, 14 * s, modeColor(d.mode));
    const Vec3 rp = sa::primaryRover(*scene_);
    const bool hidden = sa::roverHidden(*scene_);
    std::snprintf(line, sizeof(line), "ROVER %-9s pos (%.1f, %.1f)   %d ON SITE", hidden ? "HIDDEN" : "EXPOSED", rp.x, rp.z,
                  static_cast<int>(sa::roverPositions(*scene_).size()));
    hud.text(22 * s, H - 48 * s, line, 14 * s, hidden ? kGreen : kAmber);
    const std::string rate = fps_ > 0.0 ? std::to_string(static_cast<int>(fps_ + 0.5)) + " FPS" : "OFFLINE RENDER";
    std::snprintf(line, sizeof(line), "%s   %d DRAW CALLS   %lld TRIANGLES", rate.c_str(),
                  stats_.drawCalls + cvStats_.drawCalls, stats_.triangles + cvStats_.triangles);
    hud.text(22 * s, H - 26 * s, line, 13 * s, kDim);
    if (camera_.mode == CameraMode::Possess) drawPossessHud(cam, w, h);
    drawCombatFeed(w, h);
    if (paused_) hud.text(W / 2 - 40 * s, 20 * s, "PAUSED", 20 * s, kAmber);
    if (showHelp_) drawHelp(w, h);
    hud.end();
}

}  // namespace ts
