// Heads-up display: status text, heat scale, target marker, UAV camera feed, legend and help.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.h"
#include "objects/BaseLayout.h"
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
    if (scene_->uavVisionActive(0)) hud.heatImage(x, y, fw, fh, renderer_->uavCameraTexture());   // v9 M5: no stale image while docked
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
    if (!scene_->uavVisionActive(0)) {   // v9 M5: docked / taking off / landing: no picture from the camera
        hud.rect(x, y, fw, fh, Rgba{0.02f, 0.03f, 0.04f, 0.92f});
        const std::string title = uavFeedTitle(0, scene_->uavDutyState(0), scene_->uavAirborne(0));
        hud.text(x + fw / 2 - hud.textWidth(title, 14 * s) / 2, y + fh / 2 - 9 * s, title, 14 * s, kDim);
    }
    char line[96];
    std::snprintf(line, sizeof(line), "UAV IR CAM %dx%d  %s  BLOBS %d", kCvWidth, kCvHeight,
                  droneModeName(scene_->drone.mode), detection_.blobCount);
    hud.text(x, y + fh + 6 * s, line, 15 * s, modeColor(scene_->drone.mode));
}

void App::drawHelp(int w, int h) {
    Hud& hud = renderer_->hud();
    hud.flush();   // v7 WP-P: the help sheet paints over every marker / label queued before it
    const float s = std::min(static_cast<float>(h) / 900.0f, static_cast<float>(w) / 1300.0f);
    // The key table is data (HudMath helpLines): the same list feeds the report appendix and the workbook.
    const std::vector<HelpLine>& lines = helpLines();
    int rows[2] = {0, 0};
    for (const HelpLine& l : lines) ++rows[l.column];
    const float lh = 21.0f * s, colW = 600.0f * s, pw = colW * 2.0f + 30.0f * s;
    const float ph = lh * static_cast<float>(std::max(rows[0], rows[1]) + 2);
    const float x = (static_cast<float>(w) - pw) / 2, y = (static_cast<float>(h) - ph) / 2;
    hud.rect(x - 12 * s, y - 12 * s, pw + 24 * s, ph + 24 * s, Rgba{0, 0, 0, 0.78f});
    char title[96];
    std::snprintf(title, sizeof(title), "CONTROLS (F1)    RUN SEED %u  (--seed %u repeats this run)", opt_.seed, opt_.seed);
    hud.text(x, y, title, 17 * s, kAmber);
    int row[2] = {0, 0};
    for (const HelpLine& l : lines) {
        const int c = l.column;
        hud.text(x + (c ? colW + 30 * s : 0.0f), y + lh * static_cast<float>(row[c] + 1), l.text, 14 * s, row[c] == 0 ? kAmber : kWhite);
        ++row[c];
    }
}

bool App::thermalFirstPerson() const {
    if (camera_.mode == CameraMode::Pov) return false;  // v7: thermal from the POV's own eyes
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
    const char* view = possessAlt_ ? "ALT CAM" : camera_.possessSight ? "SIGHT" : "CHASE";
    hud.text(px + pw - hud.textWidth(view, 13 * s) - 12 * s, py + 12 * s, view, 13 * s, kDim);
    hud.text(px + 12 * s, py + 38 * s, status, 15 * s, kWhite);
    hud.text(px + 12 * s, py + 62 * s, c.controls(), 13 * s, kDim);
    hud.text(px + 12 * s, py + 80 * s, controllingSoldier() ? "ESC / BACKSPACE release   C or V first / third person   [ ] next"
                                                             : "BACKSPACE release   C chase / sight   [ ] next", 12 * s, kDim);

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
        // v6: the scene's clock (the v5 HUD showed a fixed fake time)
        const DayCycle* clock = sa::dayCycle(*scene_);
        const int secs = clock ? static_cast<int>(clock->hour() * 3600.0f) % 86400
                               : (2 * 3600 + 14 * 60 + 37 + static_cast<int>(std::max(t, 0.0f))) % 86400;
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
        hud.rect(x - 8 * s, y - 6 * s, 226 * s, lh * 5 + 8 * s, back);
        // v6 sensor line: temporal noise and the frames this UAV lost
        if (!sensors_.empty()) {
            std::snprintf(buf, sizeof(buf), "NETD %.1f DN  DROP %d", sensors_[0].config().netdDn, framesDropped_[0]);
            hud.text(x, y + lh * 4, buf, 15 * s, dim);
        } else {
            hud.text(x, y + lh * 4, "IDEAL SENSOR", 15 * s, dim);
        }
        std::snprintf(buf, sizeof(buf), "IR   %s", pal[((settings_.palette % 4) + 4) % 4]);
        hud.text(x, y, buf, 15 * s, fg);
        std::snprintf(buf, sizeof(buf), "%s  FOV %.1f DEG", fl, fov);
        hud.text(x, y + lh, buf, 15 * s, fg);
        if (settings_.nucShutter > 0.05f && std::fmod(t, 0.5f) < 0.3f) hud.text(x, y + lh * 2, "NUC", 15 * s, kAmber);
        hud.text(x, y + lh * 3, "REC", 15 * s, fg);
        if (std::fmod(t, 1.0f) < 0.5f)
            hud.rect(x + 46 * s, y + lh * 3 + 3 * s, 10 * s, 10 * s, Rgba{1.0f, 0.15f, 0.1f, 1.0f});
    }

    // Squad status box: this UAV's role and how long it has held the target; the partner UAV's lock below it.
    {
        const Squad& sq = scene_->squad();
        const float x = 40 * s, y0 = 240 * s;  // under the left telemetry column
        const float bw = 230 * s, bh = 24 * s;
        for (int i = 0; i < Squad::kCount; ++i) {
            const SquadRole role = sq.roles[i];
            const bool engaged = role == SquadRole::Lead || role == SquadRole::Wing;
            const Rgba c = role == SquadRole::Lead ? Rgba{1.0f, 0.38f, 0.22f, 1.0f}
                           : role == SquadRole::Wing ? kAmber : (role == SquadRole::Search ? Rgba{0.95f, 0.85f, 0.45f, 1.0f} : kGreen);
            const float y = y0 + (bh + 6 * s) * static_cast<float>(i);
            hud.rect(x, y, bw, bh, back);
            hud.frame(x, y, bw, bh, c);
            std::snprintf(buf, sizeof(buf), engaged ? "UAV-%d %s  LOCK %.1fs" : "UAV-%d %s", i + 1, squadRoleName(role), sq.lock[i]);
            hud.text(x + 8 * s, y + 5 * s, buf, 14 * s, c);
        }
    }

    // Track boxes of ALL confirmed tracks (the squad's shared picture), projected with this camera.
    {
        const Squad& sq = scene_->squad();
        const Mat4 vp = cam.proj * cam.view;
        const float lockMax = std::max(sq.lock[0], sq.lock[1]);
        int shown = 0;
        for (const Track& tr : sq.tracker().tracks()) {
            if (tr.state == TrackState::Tentative) continue;
            const bool target = tr.id == sq.targetTrack();
            const bool locked = target && lockMax > 0.5f;
            const bool coasting = tr.state == TrackState::Coasting;
            const MarkerProjection m = projectMarker(vp, cam.position, tr.position() + Vec3{0, 0.25f, 0}, w, h);
            if (!m.onScreen) continue;
            ++shown;
            const float half = markerHalfSize(m.dist, h, 0.8f, thermalFovDeg_);
            const Rgba c = locked ? kRed : (coasting ? kAmber : Rgba{1.0f, 0.55f, 0.35f, 1.0f});
            hud.brackets(m.x - half, m.y - half, 2 * half, 2 * half, half * 0.35f, c);
            std::snprintf(buf, sizeof(buf), "%s %d", locked ? "LOCK" : (coasting ? "LOST" : "TRK"), tr.id);
            hud.text(m.x - half, m.y - half - 18 * s, buf, 14 * s, c);
            std::snprintf(buf, sizeof(buf), "RNG %.0f M%s", m.dist, tr.triangulated ? "  2-VIEW" : "");
            hud.text(m.x - half, m.y + half + 5 * s, buf, 13 * s, c);
            if (locked) {
                ring(hud, m.x, m.y, half * 1.3f, 36, Rgba{kRed.r, kRed.g, kRed.b, 0.55f + 0.4f * std::sin(t * 9.0f)});
                hud.line(m.x - half * 1.3f - 10 * s, m.y, m.x - half * 1.3f, m.y, c);
                hud.line(m.x + half * 1.3f, m.y, m.x + half * 1.3f + 10 * s, m.y, c);
            }
        }
        // Single-target fallback of the v4 tracker when the squad has no confirmed track yet.
        if (shown == 0 && detection_.found) {
            const float k = std::tan(radians(kCvFovYDeg * 0.5f)) / std::tan(radians(thermalFovDeg_ * 0.5f));
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
        }
    }

    drawPersonBoxes(w, h);
    hud.text(40 * s, H - 46 * s, "THERMALSENSE  UAV-1  LWIR 8-14um", 13 * s, fg);
    hud.text(40 * s, H - 28 * s, "Y: EXTERNAL VIEW   T: PALETTE   WHEEL: ZOOM", 13 * s, dim);
}

// ---- v5 game HUD ---------------------------------------------------------------------------------------------------------
namespace {

const Rgba kGold{1.0f, 0.82f, 0.25f, 1.0f};
const Rgba kSiren{1.0f, 0.18f, 0.12f, 1.0f};
const Rgba kEnemy{1.0f, 0.22f, 0.16f, 1.0f};
const Rgba kTentative{0.6f, 0.62f, 0.66f, 0.9f};

Rgba withAlpha(Rgba c, float a) { return {c.r, c.g, c.b, c.a * a}; }

// Text with a dark drop shadow so it stays readable on bright fireballs and snow-white thermal ground.
void label(Hud& hud, float x, float y, const std::string& t, float px, Rgba c) {
    hud.text(x + std::max(1.0f, px * 0.07f), y + std::max(1.0f, px * 0.07f), t, px, Rgba{0, 0, 0, 0.75f * c.a});
    hud.text(x, y, t, px, c);
}
void labelC(Hud& hud, float cx, float y, const std::string& t, float px, Rgba c) {
    label(hud, cx - hud.textWidth(t, px) * 0.5f, y, t, px, c);
}

void diamond(Hud& hud, float x, float y, float r, Rgba c) {
    hud.line(x, y - r, x + r, y, c);
    hud.line(x + r, y, x, y + r, c);
    hud.line(x, y + r, x - r, y, c);
    hud.line(x - r, y, x, y - r, c);
}

void arrowHead(Hud& hud, const EdgeArrow& a, float size, Rgba c) {
    const float px = -a.dirY, py = a.dirX;
    const float bx = a.x - a.dirX * size, by = a.y - a.dirY * size;
    hud.line(a.x, a.y, bx + px * size * 0.5f, by + py * size * 0.5f, c);
    hud.line(a.x, a.y, bx - px * size * 0.5f, by - py * size * 0.5f, c);
    hud.line(bx + px * size * 0.5f, by + py * size * 0.5f, bx - px * size * 0.5f, by - py * size * 0.5f, c);
}

Rgba roleColor(SquadRole r) {
    switch (r) {
        case SquadRole::Lead: return Rgba{1.0f, 0.38f, 0.22f, 1.0f};
        case SquadRole::Wing: return Rgba{1.0f, 0.75f, 0.15f, 1.0f};
        case SquadRole::Search: return Rgba{0.95f, 0.85f, 0.45f, 0.95f};
        case SquadRole::Manual: return Rgba{0.5f, 0.85f, 1.0f, 1.0f};
        default: return Rgba{0.35f, 1.0f, 0.5f, 0.95f};
    }
}

// Health (0..1) of the alive rover standing within 1.5 m (ground distance) of a track, or -1. Display only.
float healthNearTrack(const Scene& sc, const Vec3& p) {
    float best = 1.5f, health = -1.0f;
    for (const auto& in : sc.intruders()) {
        if (in->life != Intruder::Life::Alive) continue;
        const float dx = in->state.position.x - p.x, dz = in->state.position.z - p.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d < best) {
            best = d;
            health = clampf(in->health / kRoverHealth, 0.0f, 1.0f);
        }
    }
    return health;
}

float maxLock(const Squad& sq) { return std::max(sq.lock[0], sq.lock[1]); }
constexpr float kLockShown = 0.5f;  // s of held lock before the LOCK ring appears

void healthBar(Hud& hud, float x, float y, float w, float h, float frac) {
    float r, g, b;
    integrityColor(frac * 100.0f, r, g, b);
    hud.rect(x, y, w, h, Rgba{0, 0, 0, 0.6f});
    hud.rect(x, y, w * clampf(frac, 0.0f, 1.0f), h, Rgba{r, g, b, 0.95f});
    hud.frame(x, y, w, h, Rgba{1, 1, 1, 0.55f});
}

// Red brackets, "ID n", range, health bar, LOCK ring and an edge arrow for every confirmed track.
void drawEnemyMarkers(Hud& hud, const Scene& sc, const CameraView& cam, int w, int h) {
    const float s = static_cast<float>(h) / 900.0f;
    const Mat4 vp = cam.proj * cam.view;
    const Squad& sq = sc.squad();
    const float t = sc.time();
    std::vector<Vec3> drawn;  // declutter: one full marker per target area
    for (const Track& tr : sq.tracker().tracks()) {
        if (tr.state == TrackState::Tentative) continue;
        const bool coasting = tr.state == TrackState::Coasting;
        const bool isTarget = tr.id == sq.targetTrack();
        const Vec3 pos = tr.position() + Vec3{0, 0.3f, 0};
        const MarkerProjection m = projectMarker(vp, cam.position, pos, w, h);
        if (m.dist < 2.0f) continue;   // v7 WP-P: the camera rides the tracked rover itself (bumper cam / driving)
        bool near = false;
        for (const Vec3& d : drawn) near = near || length(Vec3{d.x - pos.x, 0, d.z - pos.z}) < 1.5f;
        if (coasting && !isTarget) {
            // Lost from view (behind cover): only a small dim diamond where it was last seen, no label / arrow.
            if (m.onScreen && !near) {
                const float r = 4.0f * s;
                const Rgba dimC{1.0f, 0.7f, 0.2f, 0.45f};
                hud.line(m.x - r, m.y, m.x, m.y - r, dimC);
                hud.line(m.x, m.y - r, m.x + r, m.y, dimC);
                hud.line(m.x + r, m.y, m.x, m.y + r, dimC);
                hud.line(m.x, m.y + r, m.x - r, m.y, dimC);
            }
            continue;
        }
        if (near && !isTarget) continue;
        drawn.push_back(pos);
        const Rgba c = coasting ? Rgba{1.0f, 0.7f, 0.2f, 0.8f} : kEnemy;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "ID %d", tr.id);
        if (!m.onScreen) {
            const EdgeArrow a = edgeArrow(m.x, m.y, m.front, w, h, 26.0f * s);
            arrowHead(hud, a, 15.0f * s, c);
            char far[48];
            std::snprintf(far, sizeof(far), "ID %d  %.0f M", tr.id, m.dist);
            const float px = 12.0f * s, tw = hud.textWidth(far, px);
            const float lx = clampf(a.x - a.dirX * (22.0f * s + tw * 0.5f) - tw * 0.5f, 6.0f * s, static_cast<float>(w) - tw - 6.0f * s);
            const float ly = clampf(a.y - a.dirY * 22.0f * s - px * 0.5f, 6.0f * s, static_cast<float>(h) - px - 6.0f * s);
            label(hud, lx, ly, far, px, c);
            continue;
        }
        const float half = markerHalfSize(m.dist, h, 0.55f);
        hud.brackets(m.x - half, m.y - half, 2 * half, 2 * half, half * 0.45f, c);
        const float px = 13.0f * s;
        std::snprintf(buf, sizeof(buf), coasting ? "ID %d ?  %.0f M" : "ID %d  %.0f M", tr.id, m.dist);
        label(hud, m.x - half, m.y - half - px - 3 * s, buf, px, c);
        const float hp = healthNearTrack(sc, tr.position());
        if (hp >= 0.0f) healthBar(hud, m.x - half, m.y + half + 4 * s, 2 * half, std::max(4.0f * s, 3.0f), hp);
        if (tr.id == sq.targetTrack() && maxLock(sq) > kLockShown) {
            const float pulse = 0.65f + 0.35f * std::sin(t * 9.0f);
            ring(hud, m.x, m.y, half * 1.45f, 40, Rgba{kEnemy.r, kEnemy.g, kEnemy.b, pulse});
            ring(hud, m.x, m.y, half * 1.45f - 2.0f, 40, Rgba{kEnemy.r, kEnemy.g, kEnemy.b, pulse * 0.5f});
            label(hud, m.x - hud.textWidth("LOCK", px) * 0.5f, m.y + half * 1.45f + 5 * s + (hp >= 0.0f ? 8 * s : 0.0f), "LOCK", px,
                  Rgba{1.0f, 0.35f, 0.25f, pulse});
        }
    }
    // Predicted lead points (designators) while rounds are inbound.
    const StrikeState& st = sq.strike();
    if (st.phase == StrikePhase::Inbound) {
        for (int i = 0; i < Squad::kCount; ++i) {
            if (!st.launched[i] || !sq.designatorValid(i)) continue;
            const MarkerProjection m = projectMarker(vp, cam.position, sq.designatorPoint(i) + Vec3{0, 0.2f, 0}, w, h);
            if (!m.onScreen) continue;
            diamond(hud, m.x, m.y, 8.0f * s, kAmber);
            diamond(hud, m.x, m.y, 3.0f * s, kAmber);
        }
    }
}

// "UAV-1 LEAD" tags above both drones, thin lines to the target while engaging.
void drawUavTags(Hud& hud, const Scene& sc, const CameraView& cam, int w, int h) {
    const float s = static_cast<float>(h) / 900.0f;
    const Mat4 vp = cam.proj * cam.view;
    const Squad& sq = sc.squad();
    const Track* target = sq.targetTrack() >= 0 ? sq.tracker().find(sq.targetTrack()) : nullptr;
    MarkerProjection tm;
    if (target) tm = projectMarker(vp, cam.position, target->position() + Vec3{0, 0.3f, 0}, w, h);
    float prevX = -1e9f, prevY = -1e9f;
    for (int i = 0; i < Squad::kCount; ++i) {
        const DroneState& d = sc.droneState(i);
        const MarkerProjection m = projectMarker(vp, cam.position, d.position + Vec3{0, 0.45f, 0}, w, h);
        if (!m.front || m.dist < 1.5f || (d.manual && m.dist < 6.0f)) continue;   // v9 M5: not over the chased drone
        const SquadRole role = sq.roles[i];
        const Rgba c = roleColor(role);
        const bool engaged = (role == SquadRole::Lead || role == SquadRole::Wing) && target;
        if (engaged && tm.front && (m.onScreen || tm.onScreen)) hud.line(m.x, m.y, tm.x, tm.y, withAlpha(c, 0.55f));
        if (!m.onScreen) continue;
        char buf[32];
        const char* duty = sc.uavDutyState(i);   // v9 M5: off patrol the duty state (DOCKED, TAKE-OFF, ...) is the tag
        std::snprintf(buf, sizeof(buf), "UAV-%d %s", i + 1, std::strcmp(duty, "PATROL") == 0 ? squadRoleName(role) : duty);
        const float px = clampf(14.0f * s * (14.0f / std::max(m.dist, 14.0f)) * 1.0f + 6.0f * s, 11.0f * s, 15.0f * s);
        float ty = m.y - 30.0f * s;
        if (m.onScreen && std::fabs(m.x - prevX) < 130.0f * s && std::fabs(ty - prevY) < px * 1.4f) ty -= px * 1.5f;  // keep two close tags apart
        prevX = m.x;
        prevY = ty;
        labelC(hud, m.x, ty, buf, px, c);
        hud.line(m.x, ty + px + 3 * s, m.x, m.y - 9 * s, withAlpha(c, 0.7f));
        hud.brackets(m.x - 8 * s, m.y - 8 * s, 16 * s, 16 * s, 5 * s, withAlpha(c, 0.9f));
    }
}

// Wave / score / kills / enemies, base integrity bar, strike phase. y0 = panel top in pixels.
void drawTopBar(Hud& hud, const Scene& sc, int w, int h, float y0) {
    const float s = static_cast<float>(h) / 900.0f;
    const MissionState& ms = sc.mission().state();
    const Squad& sq = sc.squad();
    char b1[32], b2[32], b3[48], b4[32];
    std::snprintf(b1, sizeof(b1), "WAVE %d", ms.wave);
    std::snprintf(b2, sizeof(b2), "SCORE %04d", ms.score);
    std::snprintf(b3, sizeof(b3), "KILLS %d (COMBINED %d)", ms.kills, ms.combinedKills);
    std::snprintf(b4, sizeof(b4), "ENEMIES %d", sc.aliveIntruders());
    const float p1 = 24.0f * s, p2 = 17.0f * s, gap = 30.0f * s;
    const float tw = hud.textWidth(b1, p1) + hud.textWidth(b2, p1) + hud.textWidth(b3, p2) + hud.textWidth(b4, p2) + 3 * gap;
    const float pw = std::max(tw + 40.0f * s, 700.0f * s), ph = 66.0f * s;
    const float x0 = (static_cast<float>(w) - pw) * 0.5f;
    hud.rect(x0, y0, pw, ph, Rgba{0.0f, 0.0f, 0.0f, 0.5f});
    hud.frame(x0, y0, pw, ph, Rgba{1, 1, 1, 0.25f});
    float x = x0 + (pw - tw) * 0.5f;
    const float r1 = y0 + 6.0f * s;
    label(hud, x, r1, b1, p1, kWhite);
    x += hud.textWidth(b1, p1) + gap;
    label(hud, x, r1, b2, p1, kGold);
    x += hud.textWidth(b2, p1) + gap;
    label(hud, x, r1 + 5 * s, b3, p2, kWhite);
    x += hud.textWidth(b3, p2) + gap;
    label(hud, x, r1 + 5 * s, b4, p2, sc.aliveIntruders() > 0 ? kEnemy : kDim);

    // Row 2: integrity bar (left), strike phase (right).
    const float r2 = y0 + 40.0f * s, bx = x0 + 18.0f * s, bw = 230.0f * s, bh = 14.0f * s;
    label(hud, bx, r2 + 1 * s, "BASE INTEGRITY", 12.0f * s, kDim);
    const float barX = bx + hud.textWidth("BASE INTEGRITY", 12.0f * s) + 10.0f * s;
    float cr, cg, cb;
    integrityColor(ms.integrity, cr, cg, cb);
    hud.rect(barX, r2, bw, bh, Rgba{0, 0, 0, 0.6f});
    hud.rect(barX, r2, bw * clampf(ms.integrity / 100.0f, 0.0f, 1.0f), bh, Rgba{cr, cg, cb, 0.95f});
    hud.frame(barX, r2, bw, bh, Rgba{1, 1, 1, 0.6f});
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%.0f%%", ms.integrity);
    label(hud, barX + bw + 8 * s, r2 + 1 * s, pct, 13.0f * s, Rgba{cr, cg, cb, 1.0f});

    const StrikeState& st = sq.strike();
    char sb[64];
    Rgba sc2 = kGreen;
    const float t = sc.time();
    if (st.phase == StrikePhase::Planned || st.phase == StrikePhase::Inbound) {
        std::snprintf(sb, sizeof(sb), "%s  %s", st.combined ? "COMBINED STRIKE" : "STRIKE",
                      st.phase == StrikePhase::Planned ? "PLANNED" : "INBOUND");
        const float blink = st.phase == StrikePhase::Inbound ? 0.75f + 0.25f * std::sin(t * 10.0f) : 1.0f;
        sc2 = Rgba{1.0f, 0.75f, 0.15f, blink};
    } else if (st.phase == StrikePhase::Tracking && st.track >= 0) {
        std::snprintf(sb, sizeof(sb), "TRACKING ID %d", st.track);
        sc2 = Rgba{1.0f, 0.45f, 0.3f, 1.0f};
    } else if (ms.betweenWaves && ms.wave > 0) {
        std::snprintf(sb, sizeof(sb), "NEXT WAVE IN %d S", static_cast<int>(std::ceil(std::max(ms.breakTimer, 0.0f))));
        sc2 = kDim;
    } else {
        std::snprintf(sb, sizeof(sb), "SQUAD ON PATROL");
    }
    label(hud, x0 + pw - 18.0f * s - hud.textWidth(sb, 15.0f * s), r2, sb, 15.0f * s, sc2);
}

// Tactical radar: compound circle, camp assets, UAV icons with heading, track dots, strike lines, sweep.
void drawMiniMap(Hud& hud, const Scene& sc, const CameraView& cam, int w, int h, float inset) {
    const float s = static_cast<float>(h) / 900.0f;
    MiniMapGeom g = miniMapGeometry(w, h);
    g.cy -= inset;
    const Squad& sq = sc.squad();
    const float t = sc.time();
    const Rgba line{0.45f, 1.0f, 0.6f, 0.55f}, faint{0.45f, 1.0f, 0.6f, 0.2f};
    hud.rect(g.cx - g.radius - 6 * s, g.cy - g.radius - 20 * s, 2 * g.radius + 12 * s, 2 * g.radius + 26 * s, Rgba{0, 0.05f, 0.02f, 0.5f});
    label(hud, g.cx - g.radius, g.cy - g.radius - 17 * s, "TACTICAL", 12.0f * s, Rgba{0.55f, 1.0f, 0.65f, 0.9f});
    ring(hud, g.cx, g.cy, g.radius, 72, line);
    ring(hud, g.cx, g.cy, g.radius * layout::kPerimeterRadius / g.worldRadius, 72, Rgba{0.45f, 1.0f, 0.6f, 0.35f});
    ring(hud, g.cx, g.cy, g.radius * 0.5f, 48, faint);
    hud.line(g.cx - g.radius, g.cy, g.cx + g.radius, g.cy, faint);
    hud.line(g.cx, g.cy - g.radius, g.cx, g.cy + g.radius, faint);
    // Sweep line with a fading tail.
    const float a0 = std::fmod(t * 1.6f, 6.2831853f);
    for (int k = 0; k < 14; ++k) {
        const float a = a0 - 0.045f * static_cast<float>(k);
        hud.line(g.cx, g.cy, g.cx + std::cos(a) * g.radius, g.cy + std::sin(a) * g.radius,
                 Rgba{0.45f, 1.0f, 0.6f, 0.42f * (1.0f - static_cast<float>(k) / 14.0f)});
    }
    // Camp assets.
    for (const Vec3& o : Mission::objectives()) {
        float x, y;
        miniMapToScreen(g, o.x, o.z, x, y);
        hud.rect(x - 3 * s, y - 3 * s, 6 * s, 6 * s, Rgba{1.0f, 0.78f, 0.2f, 0.95f});
        hud.frame(x - 4.5f * s, y - 4.5f * s, 9 * s, 9 * s, Rgba{1.0f, 0.78f, 0.2f, 0.5f});
    }
    // Strike lines: UAV -> target.
    const Track* target = sq.targetTrack() >= 0 ? sq.tracker().find(sq.targetTrack()) : nullptr;
    const StrikeState& st = sq.strike();
    const bool live = st.phase == StrikePhase::Planned || st.phase == StrikePhase::Inbound;
    if (target) {
        float tx, ty;
        miniMapToScreen(g, target->x[0], target->x[1], tx, ty);
        for (int i = 0; i < Squad::kCount; ++i) {
            if (sq.roles[i] != SquadRole::Lead && sq.roles[i] != SquadRole::Wing) continue;
            float ux, uy;
            miniMapToScreen(g, sc.droneState(i).position.x, sc.droneState(i).position.z, ux, uy);
            hud.line(ux, uy, tx, ty, live ? Rgba{1.0f, 0.75f, 0.15f, 0.9f} : Rgba{1.0f, 0.3f, 0.2f, 0.5f});
        }
    }
    // Track dots.
    for (const Track& tr : sq.tracker().tracks()) {
        float x, y;
        miniMapToScreen(g, tr.x[0], tr.x[1], x, y);
        if (tr.state == TrackState::Tentative) {
            hud.rect(x - 1.5f * s, y - 1.5f * s, 3 * s, 3 * s, kTentative);
        } else if (tr.state == TrackState::Coasting) {
            hud.frame(x - 3 * s, y - 3 * s, 6 * s, 6 * s, Rgba{1.0f, 0.65f, 0.2f, 0.9f});
        } else {
            hud.rect(x - 3 * s, y - 3 * s, 6 * s, 6 * s, kEnemy);
        }
        if (target && tr.id == target->id) ring(hud, x, y, 8 * s, 16, Rgba{1.0f, 0.3f, 0.2f, 0.9f});
    }
    // UAV icons: triangle pointing along the flight direction.
    for (int i = 0; i < Squad::kCount; ++i) {
        const DroneState& d = sc.droneState(i);
        float x, y, dx, dy;
        miniMapToScreen(g, d.position.x, d.position.z, x, y);
        const float sp = std::sqrt(d.velocity.x * d.velocity.x + d.velocity.z * d.velocity.z);
        if (sp > 0.3f) {
            dx = d.velocity.x / sp;
            dy = d.velocity.z / sp;
        } else {
            miniMapHeading(radians(d.yawDeg), dx, dy);
        }
        const Rgba c = roleColor(sq.roles[i]);
        const float r = 7.0f * s, px = -dy, py = dx;
        hud.line(x + dx * r, y + dy * r, x - dx * r * 0.7f + px * r * 0.65f, y - dy * r * 0.7f + py * r * 0.65f, c);
        hud.line(x + dx * r, y + dy * r, x - dx * r * 0.7f - px * r * 0.65f, y - dy * r * 0.7f - py * r * 0.65f, c);
        hud.line(x - dx * r * 0.7f + px * r * 0.65f, y - dy * r * 0.7f + py * r * 0.65f, x - dx * r * 0.7f - px * r * 0.65f,
                 y - dy * r * 0.7f - py * r * 0.65f, c);
        char n[4];
        std::snprintf(n, sizeof(n), "%d", i + 1);
        label(hud, x + 6 * s, y - 14 * s, n, 11.0f * s, c);
    }
    // The viewer.
    {
        float x, y;
        miniMapToScreen(g, cam.position.x, cam.position.z, x, y);
        const Vec3 f{-cam.view.at(2, 0), 0.0f, -cam.view.at(2, 2)};
        const float fl = std::sqrt(f.x * f.x + f.z * f.z);
        if (fl > 0.05f) {
            const float dx = f.x / fl, dy = f.z / fl;
            hud.line(x, y, x + dx * 10 * s, y + dy * 10 * s, Rgba{1, 1, 1, 0.8f});
            hud.line(x + dx * 10 * s, y + dy * 10 * s, x + dx * 6 * s - dy * 3 * s, y + dy * 6 * s + dx * 3 * s, Rgba{1, 1, 1, 0.8f});
            hud.line(x + dx * 10 * s, y + dy * 10 * s, x + dx * 6 * s + dy * 3 * s, y + dy * 6 * s - dx * 3 * s, Rgba{1, 1, 1, 0.8f});
        }
        hud.rect(x - 1.5f * s, y - 1.5f * s, 3 * s, 3 * s, Rgba{1, 1, 1, 0.9f});
    }
}

// Banners driven by the game events: wave, spotted, score pop-ups, hit marker, breach, base lost.
void drawBanners(Hud& hud, const Scene& sc, const HudEvents& ev, const CameraView& cam, int w, int h, float inset) {
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    const float t = sc.time();
    const Mat4 vp = cam.proj * cam.view;
    // Screen-edge flash of a breach.
    const float flash = ev.breachFlash(t);
    if (flash > 0.0f) {
        for (int i = 0; i < 14; ++i) {
            const float k = static_cast<float>(i), a = 0.5f * flash * (1.0f - k / 14.0f) * (1.0f - k / 14.0f);
            const float e = 7.0f * s * k, d = 7.0f * s;
            const Rgba c{1.0f, 0.1f, 0.05f, a};
            hud.rect(0, e, W, d, c);
            hud.rect(0, H - e - d, W, d, c);
            hud.rect(0, e, d, H - 2 * e, c);
            hud.rect(W - e - d, e, d, H - 2 * e, c);
        }
    }
    const float dim = ev.baseLostDim(t);
    if (dim > 0.0f) hud.rect(0, 0, W, H, Rgba{0.0f, 0.0f, 0.0f, dim});
    for (const Banner& b : ev.banners()) {
        const float age = t - b.start;
        if (age < 0.0f) continue;
        const float u = b.duration;
        switch (b.kind) {
            case BannerKind::Wave: {
                const float a = bannerAlpha(age, u, 0.2f, 0.6f);
                char buf[40];
                std::snprintf(buf, sizeof(buf), "WAVE %d INCOMING", b.value);
                const float px = 54.0f * s, y = H * 0.2f + inset * 0.5f;
                const float tw = hud.textWidth(buf, px);
                hud.rect(W * 0.5f - tw * 0.5f - 40 * s, y - 12 * s, tw + 80 * s, px + 24 * s, Rgba{0.25f, 0.0f, 0.0f, 0.45f * a});
                hud.rect(W * 0.5f - tw * 0.5f - 40 * s, y - 12 * s, tw + 80 * s, 3 * s, Rgba{1.0f, 0.18f, 0.12f, a});
                hud.rect(W * 0.5f - tw * 0.5f - 40 * s, y + px + 9 * s, tw + 80 * s, 3 * s, Rgba{1.0f, 0.18f, 0.12f, a});
                const float siren = 0.8f + 0.2f * std::sin(age * 12.0f);
                labelC(hud, W * 0.5f, y, buf, px, withAlpha(kSiren, a * siren));
                break;
            }
            case BannerKind::Spotted: {
                const float a = bannerAlpha(age, u, 0.1f, 0.5f);
                labelC(hud, W * 0.5f, H * 0.2f + inset * 0.5f + 84.0f * s, "ENEMY SPOTTED", 24.0f * s, withAlpha(kAmber, a));
                break;
            }
            case BannerKind::Strike: {
                const float a = bannerAlpha(age, u, 0.1f, 0.5f);
                labelC(hud, W * 0.5f, H * 0.2f + inset * 0.5f + 116.0f * s, "COMBINED STRIKE", 24.0f * s, withAlpha(kAmber, a));
                break;
            }
            case BannerKind::Kill: {
                const float a = bannerAlpha(age, u, 0.08f, 0.7f);
                char buf[40];
                if (b.combined) std::snprintf(buf, sizeof(buf), "COMBINED STRIKE +%d", b.value);
                else std::snprintf(buf, sizeof(buf), "+%d", b.value);
                float x = W * 0.5f, y = H * 0.42f;
                if (b.hasPosition) {
                    const MarkerProjection m = projectMarker(vp, cam.position, b.position + Vec3{0, 1.0f, 0}, w, h);
                    if (m.onScreen) {
                        x = clampf(m.x, W * 0.12f, W * 0.88f);
                        y = clampf(m.y, H * 0.2f, H * 0.85f);
                    }
                }
                y -= bannerRise(age, u) * 70.0f * s;
                labelC(hud, x, y, buf, (b.combined ? 34.0f : 40.0f) * s, withAlpha(kGold, a));
                break;
            }
            case BannerKind::Hit: {
                const float a = 1.0f - age / u;
                float x = W * 0.5f, y = H * 0.5f;
                bool anchored = false;
                if (b.hasPosition) {
                    const MarkerProjection m = projectMarker(vp, cam.position, b.position + Vec3{0, 0.3f, 0}, w, h);
                    if (m.onScreen) {
                        x = m.x;
                        y = m.y;
                        anchored = true;
                    }
                }
                const float r = (anchored ? 12.0f : 16.0f) * s * (1.0f + 0.6f * age / u);
                const Rgba c{1, 1, 1, a};
                for (int k = -1; k <= 1; k += 2) {  // doubled lines = thicker X
                    const float o = static_cast<float>(k) * 0.7f;
                    hud.line(x - r + o, y - r, x - r * 0.35f + o, y - r * 0.35f, c);
                    hud.line(x + r + o, y + r, x + r * 0.35f + o, y + r * 0.35f, c);
                    hud.line(x - r + o, y + r, x - r * 0.35f + o, y + r * 0.35f, c);
                    hud.line(x + r + o, y - r, x + r * 0.35f + o, y - r * 0.35f, c);
                }
                break;
            }
            case BannerKind::Breach: {
                const float a = bannerAlpha(age, u, 0.08f, 0.6f);
                char buf[40];
                std::snprintf(buf, sizeof(buf), "BREACH -%d%%", b.value);
                labelC(hud, W * 0.5f, H * 0.64f, buf, 48.0f * s, withAlpha(kSiren, a));  // below the other banners
                break;
            }
            case BannerKind::BaseLost:
                break;  // drawn below (two phases)
            case BannerKind::Dusk:
            case BannerKind::NightFalls:
            case BannerKind::Dawn:
            case BannerKind::DayBreak: {  // v6 phase banner: headline + what changes in the camp
                const float a = bannerAlpha(age, u, 0.4f, 0.9f);
                const Rgba c = b.kind == BannerKind::Dusk         ? Rgba{1.0f, 0.62f, 0.30f, 1.0f}
                               : b.kind == BannerKind::NightFalls ? Rgba{0.55f, 0.70f, 1.0f, 1.0f}
                               : b.kind == BannerKind::Dawn       ? Rgba{1.0f, 0.72f, 0.70f, 1.0f}
                                                                  : Rgba{1.0f, 0.92f, 0.55f, 1.0f};
                const float px = 44.0f * s, y = H * 0.27f + inset * 0.5f;
                const float tw = hud.textWidth(phaseBannerText(b.kind), px);
                hud.rect(W * 0.5f - tw * 0.5f - 60 * s, y + px + 8 * s, tw + 120 * s, 2 * s, withAlpha(c, 0.8f * a));
                labelC(hud, W * 0.5f, y, phaseBannerText(b.kind), px, withAlpha(c, a));
                labelC(hud, W * 0.5f, y + px + 16 * s, phaseBannerDetail(b.kind), 17.0f * s, withAlpha(kWhite, 0.85f * a));
                break;
            }
            case BannerKind::Shift: {  // under the phase banner: soldiers walking to their new posts
                const float a = bannerAlpha(age, u, 0.2f, 0.6f);
                char buf[48];
                if (b.value > 0) std::snprintf(buf, sizeof(buf), "SHIFT CHANGE: %d SOLDIERS MOVING", b.value);
                else std::snprintf(buf, sizeof(buf), "SHIFT CHANGE");
                labelC(hud, W * 0.5f, H * 0.43f + inset * 0.5f, buf, 20.0f * s, withAlpha(kWhite, 0.9f * a));
                break;
            }
            case BannerKind::Withdraw: {
                const float a = bannerAlpha(age, u, 0.1f, 0.6f);
                char buf[48];
                if (b.value > 1) std::snprintf(buf, sizeof(buf), "%d INTRUDERS WITHDRAW", b.value);
                else std::snprintf(buf, sizeof(buf), "INTRUDER WITHDRAWS");
                labelC(hud, W * 0.5f, H * 0.38f + inset * 0.5f, buf, 24.0f * s, withAlpha(Rgba{1.0f, 0.72f, 0.70f, 1.0f}, a));
                break;
            }
        }
    }
    if (const char* txt = ev.baseLostText(t)) {
        const float a = std::min(1.0f, dim / 0.4f);
        const bool first = std::string(txt) == "BASE LOST";
        labelC(hud, W * 0.5f, H * 0.42f, txt, (first ? 84.0f : 52.0f) * s, withAlpha(first ? kSiren : kWhite, a));
    }
}

// Filled disc (sun) or crescent (moon) from horizontal slices.
void skyDisc(Hud& hud, float cx, float cy, float r, Rgba c, float cut) {
    const int n = std::max(6, static_cast<int>(r * 1.5f));
    for (int i = 0; i < n; ++i) {
        const float y = -r + (2.0f * r) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const float half = std::sqrt(std::max(0.0f, r * r - y * y));
        float x0 = cx - half, x1 = cx + half;
        if (cut > 0.0f) {  // crescent: remove a disc shifted right by 'cut' r
            const float ox = cx + cut * r;
            const float h2 = std::sqrt(std::max(0.0f, r * r - y * y));
            x1 = std::min(x1, ox - h2);
            if (x1 <= x0) continue;
        }
        hud.rect(x0, cy + y - r / static_cast<float>(n), x1 - x0, 2.0f * r / static_cast<float>(n) + 0.5f, c);
    }
}

}  // namespace

void App::drawClock(int w, int h, float x, float y, float s) {
    (void)w;
    (void)h;
    const DayCycle* clock = sa::dayCycle(*scene_);
    if (!clock) return;
    Hud& hud = renderer_->hud();
    const float pw = 192 * s, ph = 84 * s;
    hud.rect(x, y, pw, ph, Rgba{0.0f, 0.0f, 0.0f, 0.5f});
    hud.frame(x, y, pw, ph, Rgba{1, 1, 1, 0.25f});
    const float el = clock->sunElevationDeg();
    const float gx = x + 24 * s, gy = y + 24 * s, r = 12 * s;
    switch (skyGlyph(el)) {
        case SkyGlyph::Sun:
            skyDisc(hud, gx, gy, r * 0.75f, Rgba{1.0f, 0.86f, 0.35f, 1.0f}, 0.0f);
            for (int k = 0; k < 8; ++k) {
                const float a = 0.785398f * static_cast<float>(k);
                hud.line(gx + std::cos(a) * r * 0.95f, gy + std::sin(a) * r * 0.95f, gx + std::cos(a) * r * 1.35f,
                         gy + std::sin(a) * r * 1.35f, Rgba{1.0f, 0.86f, 0.35f, 1.0f});
            }
            break;
        case SkyGlyph::LowSun:  // half a sun on the horizon line
            skyDisc(hud, gx, gy, r * 0.8f, Rgba{1.0f, 0.55f, 0.25f, 1.0f}, 0.0f);
            hud.rect(gx - r * 1.3f, gy, r * 2.6f, r, Rgba{0.0f, 0.0f, 0.0f, 0.85f});
            hud.line(gx - r * 1.3f, gy, gx + r * 1.3f, gy, Rgba{1.0f, 0.7f, 0.4f, 1.0f});
            break;
        case SkyGlyph::Moon:
            skyDisc(hud, gx, gy, r * 0.85f, Rgba{0.80f, 0.86f, 1.0f, 1.0f}, 0.75f);
            break;
    }
    const std::string hhmm = formatClock(clock->hour());
    hud.text(x + 46 * s, y + 8 * s, hhmm, 26 * s, kWhite);
    char line[64];
    // v7 WP-P: phase and mode stacked (one line ran past the panel's right edge: "NIGHT  MANUAL").
    const float px = x + 46 * s + hud.textWidth(hhmm, 26 * s) + 8 * s;
    hud.text(px, y + 9 * s, clock->phaseName(), 12 * s, kWhite);
    hud.text(px, y + 23 * s, clock->autoRun() ? "AUTO" : "MANUAL", 11 * s, kDim);
    std::snprintf(line, sizeof(line), "SUN %+.0f DEG   SEED %u", el, opt_.seed);
    hud.text(x + 10 * s, y + 44 * s, line, 12 * s, kDim);
    // Garrison line: who is on duty (status text of the soldiers, WP-B), or the sensor state without soldiers.
    if (const Garrison* g = sa::garrison(*scene_)) {
        int off = 0;
        for (int i = 0; i < g->count(); ++i)
            if (g->soldier(i) && g->soldier(i)->status().find("OFF") != std::string::npos) ++off;
        std::snprintf(line, sizeof(line), "GARRISON %d ON DUTY  %d OFF", g->count() - off, off);
    } else {
        std::snprintf(line, sizeof(line), "NO GARRISON");
    }
    hud.text(x + 10 * s, y + 62 * s, line, 12 * s, kDim);
}

void App::drawPersonBoxes(int w, int h) {
    // Only where the screen IS UAV 1's gimbal camera (first-person thermal, or a director shot through it): the
    // person blobs of its CV frame are boxed in green - seen, classified, never handed to the tracker as targets.
    bool gimbal = thermalFirstPerson();
    if (camera_.mode == CameraMode::Cinematic)
        gimbal = directorShot(*scene_, camera_.directorTime, camera_.directorCut).uavCamera;
    if (!gimbal) return;
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const Rgba c{0.35f, 1.0f, 0.45f, 0.9f};
    for (const Measurement2D& m : uavBlobs_[0]) {
        if (m.cls != BlobClass::Person) continue;
        ScreenBox b = cvBoxToScreen(m.minX, m.minY, m.maxX, m.maxY, kCvWidth, kCvHeight, kCvFovYDeg, thermalFovDeg_, w, h);
        const float pad = 4 * s, minSz = 22 * s;
        b.x0 -= pad;
        b.y0 -= pad;
        b.x1 += pad;
        b.y1 += pad;
        if (b.x1 - b.x0 < minSz) { const float mx = (b.x0 + b.x1) * 0.5f; b.x0 = mx - minSz / 2; b.x1 = mx + minSz / 2; }
        if (b.y1 - b.y0 < minSz) { const float my = (b.y0 + b.y1) * 0.5f; b.y0 = my - minSz / 2; b.y1 = my + minSz / 2; }
        hud.brackets(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, std::min(b.x1 - b.x0, b.y1 - b.y0) * 0.3f, c);
        hud.text(b.x0, b.y0 - 16 * s, "PERS", 13 * s, c);
    }
}

void App::drawGameHud(const CameraView& cam, int w, int h, bool fpv, float inset) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    if (!fpv) {
        drawEnemyMarkers(hud, *scene_, cam, w, h);
        drawUavTags(hud, *scene_, cam, w, h);
        hud.flush();   // v7 WP-P: world markers stay behind the HUD panels (the Hud batches lines / text per kind)
    }
    drawTopBar(hud, *scene_, w, h, (fpv ? 92.0f * s : 12.0f * s) + inset);
    drawMiniMap(hud, *scene_, cam, w, h, inset);
    // v6 clock above the mini-map
    const MiniMapGeom g = miniMapGeometry(w, h);
    drawClock(w, h, g.cx - g.radius - 6 * s, g.cy - inset - g.radius - 96 * s, s);
    drawBanners(hud, *scene_, hudEvents_, cam, w, h, inset);
}

void App::drawHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    hud.begin(w, h);
    hud.setPalette(settings_.palette);
    if (thermalFirstPerson()) {
        drawThermalFpvHud(cam, w, h);
        drawGameHud(cam, w, h, true);
        drawCombatFeed(w, h);
        if (paused_) hud.text(w * 0.5f - 40 * h / 900.0f, 110.0f * h / 900.0f, "PAUSED", 20 * h / 900.0f, kAmber);
        if (pickerOpen_) drawPicker(w, h);
        if (showHelp_) drawHelp(w, h);
        hud.end();
        return;
    }
    if (gameMode_) {  // v7 F11: game HUD only (no diagnostics)
        drawGameModeHud(cam, w, h);
        if (uavGameView()) drawUavGameHud(cam, w, h);   // v9 M5
        drawPlayHud(cam, w, h);   // v8: selection / control prompts, third-person crosshair
        if (paused_) hud.text(w * 0.5f - 40 * h / 900.0f, 160.0f * h / 900.0f, "PAUSED", 20 * h / 900.0f, kAmber);
        if (pickerOpen_) drawPicker(w, h);
        if (showHelp_) drawHelp(w, h);
        hud.end();
        return;
    }
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    char line[160];

    if (settings_.view == ViewMode::Thermal) {
        hud.brackets(12 * s, 12 * s, W - 24 * s, H - 24 * s, 40 * s, Rgba{1, 1, 1, 0.35f});
        const float bx = W - 44 * s, by = H * 0.25f, bh = H * 0.28f;  // v6: clear of the clock panel
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
    drawGameHud(cam, w, h, false);
    // (after drawGameHud: its world markers are flushed first, so this corner block paints over them)
    // v7 WP-P: soft backing behind the corner text blocks so the dim lines stay readable over a bright day sky.
    // (Two steps at the top: the title rows end left of the centred wave / score panel, the settings rows below it.)
    hud.rect(10 * s, 10 * s, 425 * s, 56 * s, Rgba{0.0f, 0.0f, 0.0f, 0.30f});
    hud.rect(10 * s, 66 * s, 485 * s, 48 * s, Rgba{0.0f, 0.0f, 0.0f, 0.30f});
    hud.rect(10 * s, H - 76 * s, 520 * s, 70 * s, Rgba{0.0f, 0.0f, 0.0f, 0.30f});
    hud.text(22 * s, 18 * s, "THERMALSENSE", 26 * s, kWhite);
    // v7 WP-P: the key hint stays left of the centred wave / score panel (it starts at ~450 s on a 16:9 screen).
    hud.text(30 * s + hud.textWidth("THERMALSENSE", 26 * s), 30 * s, "MILITARY BASE", 13 * s, kDim);
    hud.text(22 * s, 48 * s, "F1 HELP  5-0 CONTROL  F2 TIME  F3 SOLDIER  F4 POV", 13 * s, kDim);
    std::snprintf(line, sizeof(line), "VIEW %-10s EDGES %-11s SHADING %s", viewModeName(settings_.view),
                  edgeModeName(settings_.edges), overrideName(settings_.shading));
    hud.text(22 * s, 74 * s, line, 14 * s, kWhite);
    std::snprintf(line, sizeof(line), "CAM %-11s Z-TEST %-4s DRAW %s   L0 %s  L1 %s  L2 %s", cameraModeName(camera_.mode),
                  settings_.depthTest ? "ON" : "OFF", settings_.indexed ? "INDEXED" : "NON-INDEXED",
                  settings_.moonOn ? "ON" : "--", settings_.beaconOn ? "ON" : "--", settings_.spotOn ? "ON" : "--");
    hud.text(22 * s, 94 * s, line, 14 * s, kDim);
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
    // v7 WP-Q: frame rate, mean frame time, preset (AUTO = chosen by the governor), GL draw calls; frame-time graph.
    char rate[64] = "OFFLINE RENDER";
    double meanMs = 0.0;
    int samples = 0;
    for (float ms : frameMs_)
        if (ms > 0.0f) {
            meanMs += ms;
            ++samples;
        }
    if (samples > 0) meanMs /= samples;
    if (fps_ > 0.0)
        std::snprintf(rate, sizeof(rate), "%d FPS  %.1f MS  %s%s", static_cast<int>(fps_ + 0.5), meanMs,
                      qualityName(settings_.quality), qualityAuto_ ? " AUTO" : "");
    std::snprintf(line, sizeof(line), "%s   %d DRAW CALLS   %lld TRIANGLES", rate, stats_.drawCalls + cvStats_.drawCalls,
                  stats_.triangles + cvStats_.triangles);
    hud.text(22 * s, H - 26 * s, line, 13 * s, kDim);
    if (fps_ > 0.0) {   // last 120 frames; bars green <= 16.7 ms, amber <= 33.3 ms, red above; scale 0..50 ms
        const float gx = 22 * s + hud.textWidth(line, 13 * s) + 18 * s, gh = 22 * s, gy = H - 30 * s, bw = 1.5f * s;
        hud.rect(gx - 2 * s, gy - 2 * s, kFrameHistory * bw + 4 * s, gh + 4 * s, Rgba{0.0f, 0.0f, 0.0f, 0.35f});
        for (int i = 0; i < kFrameHistory; ++i) {
            const float ms = frameMs_[(frameHead_ + i) % kFrameHistory];
            if (ms <= 0.0f) continue;
            const float bh = gh * std::min(ms, 50.0f) / 50.0f;
            const Rgba c = ms <= 16.8f ? kGreen : (ms <= 33.4f ? kAmber : Rgba{1.0f, 0.3f, 0.25f, 1.0f});
            hud.rect(gx + i * bw, gy + gh - bh, bw, bh, c);
        }
        for (float ref : {16.7f, 33.3f})
            hud.line(gx, gy + gh - gh * ref / 50.0f, gx + kFrameHistory * bw, gy + gh - gh * ref / 50.0f,
                     Rgba{1.0f, 1.0f, 1.0f, 0.3f});
    }
    if (uavGameView()) drawUavGameHud(cam, w, h);   // v9 M5: UAV chase / FPV flight HUD
    else if (camera_.mode == CameraMode::Possess) drawPossessHud(cam, w, h);
    else drawPovPanel(w, h, H - 196 * s);   // v7: riding along / driving a rover / free fly camera
    if (alert_ > 0.02f) drawAlertVignette(w, h, alert_);
    drawCombatFeed(w, h);
    drawPlayHud(cam, w, h);   // v8: selection / control prompts, third-person crosshair
    if (paused_) hud.text(W / 2 - 40 * s, 20 * s, "PAUSED", 20 * s, kAmber);
    if (pickerOpen_) drawPicker(w, h);
    if (showHelp_) drawHelp(w, h);
    hud.end();
}

// ---- v7 game camera HUD (WP-G) ----------------------------------------------------------------------------------------
namespace {

Rgba povColor(PovKind k) {
    if (povHostile(k)) return kEnemy;
    if (povProjectile(k)) return kGold;
    if (k == PovKind::Uav) return Rgba{0.5f, 0.85f, 1.0f, 1.0f};
    return Rgba{0.35f, 1.0f, 0.5f, 1.0f};
}

// Small kind icon: coloured tag box with the three-letter code.
void povIcon(Hud& hud, float x, float y, float s, PovKind k, float alpha = 1.0f) {
    const Rgba c = povColor(k);
    hud.rect(x, y, 40 * s, 18 * s, Rgba{c.r, c.g, c.b, 0.85f * alpha});
    const char* tag = povKindTag(k);
    hud.text(x + (40 * s - hud.textWidth(tag, 13 * s)) * 0.5f, y + 2 * s, tag, 13 * s, Rgba{0.02f, 0.03f, 0.04f, alpha});
}

}  // namespace

void App::drawPicker(int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const int n = static_cast<int>(pickerList_.size());
    const PickerLayout l = pickerLayout(w, h, n, pickerSel_);
    const float ph = l.headerH + l.rowH * static_cast<float>(std::max(l.rows, 1));
    // v7 WP-P: the Hud batches rects, lines and glyphs separately, so world markers / tags queued earlier would draw
    // over this panel; flush them first so the panel (nearly opaque) covers them.
    hud.flush();
    hud.rect(l.x - 12 * s, l.y - 12 * s, l.w + 24 * s, ph + 24 * s, Rgba{0.01f, 0.012f, 0.016f, 0.88f});
    hud.frame(l.x - 12 * s, l.y - 12 * s, l.w + 24 * s, ph + 24 * s, Rgba{1.0f, 0.75f, 0.15f, 0.5f});
    char buf[128];
    std::snprintf(buf, sizeof(buf), "JUMP TO POV   %d MOVING OBJECTS", n);
    label(hud, l.x, l.y, buf, 19 * s, kAmber);
    label(hud, l.x, l.y + 25 * s, "UP DOWN select   ENTER / CLICK jump   F4 / ESC close", 12 * s, kDim);
    const float cName = l.x + 50 * s, cType = l.x + l.w * 0.50f, cDist = l.x + l.w * 0.74f, cSpd = l.x + l.w * 0.87f;
    const float hy = l.y + l.headerH - 18 * s;
    hud.text(cName, hy, "NAME", 11 * s, kDim);
    hud.text(cType, hy, "TYPE", 11 * s, kDim);
    hud.text(cDist, hy, "DIST", 11 * s, kDim);
    hud.text(cSpd, hy, "SPEED", 11 * s, kDim);
    PovSource cur;
    const uint32_t curKey = currentSource(cur) ? cur.key : 0;
    for (int r = 0; r < l.rows; ++r) {
        const int i = l.firstRow + r;
        if (i >= n) break;
        const PovSource& p = pickerList_[i];
        const float y = l.y + l.headerH + l.rowH * static_cast<float>(r);
        if (i == pickerSel_) {
            hud.rect(l.x - 6 * s, y - 2 * s, l.w + 12 * s, l.rowH - 2 * s, Rgba{1.0f, 0.75f, 0.15f, 0.22f});
            hud.frame(l.x - 6 * s, y - 2 * s, l.w + 12 * s, l.rowH - 2 * s, Rgba{1.0f, 0.75f, 0.15f, 0.8f});
        }
        povIcon(hud, l.x, y + 1 * s, s, p.kind);
        const Rgba tc = i == pickerSel_ ? kWhite : Rgba{0.85f, 0.88f, 0.92f, 0.9f};
        hud.text(cName, y + 2 * s, p.name + (p.key == curKey ? "  <" : ""), 14 * s, tc);
        hud.text(cType, y + 3 * s, povKindName(p.kind), 12 * s, kDim);
        std::snprintf(buf, sizeof(buf), "%.0f M", p.distance);
        hud.text(cDist, y + 3 * s, buf, 13 * s, tc);
        std::snprintf(buf, sizeof(buf), "%.1f", p.speed);
        hud.text(cSpd, y + 3 * s, buf, 13 * s, p.speed > 0.2f ? tc : kDim);
    }
    if (l.firstRow > 0) hud.text(l.x + l.w - 30 * s, l.y + l.headerH - 18 * s, "^ MORE", 11 * s, kDim);
    if (l.firstRow + l.rows < n) hud.text(l.x + l.w - 30 * s, l.y + ph + 1 * s, "v MORE", 11 * s, kDim);
    // The selected object in the world: brackets and a leader line from the panel.
    if (pickerSel_ >= 0 && pickerSel_ < n && shownValid_) {
        const PovSource& p = pickerList_[pickerSel_];
        const Mat4 vp = perspective(radians(shownPose_.fovDeg + fovKick_), lastAspect_, shownPose_.nearZ, 600.0f) * shownPose_.view();
        const MarkerProjection m = projectMarker(vp, shownPose_.eye, p.centre, w, h);
        const bool underPanel = m.x > l.x - 12 * s && m.x < l.x + l.w + 12 * s && m.y > l.y - 12 * s && m.y < l.y + ph + 12 * s;
        if (m.onScreen && !underPanel) {
            const float half = std::max(14.0f * s, markerHalfSize(m.dist, h, p.radius * 2.0f, shownPose_.fovDeg));
            const Rgba c = povColor(p.kind);
            hud.brackets(m.x - half, m.y - half, 2 * half, 2 * half, half * 0.45f, c);
            label(hud, m.x - half, m.y - half - 18 * s, p.name, 13 * s, c);
        }
    }
}

void App::drawPovPanel(int w, int h, float y) {
    (void)w;
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float x = 22 * s;
    char l1[160], l2[200];
    PovSource src;
    const bool has = currentSource(src);
    Rgba head = kAmber;
    if (camera_.mode == CameraMode::Free && flyCam_) {
        std::snprintf(l1, sizeof(l1), "FREE CAMERA   %.0f M/S", fly_.speed);
        std::snprintf(l2, sizeof(l2), "W A S D  PGUP PGDN fly   SHIFT x3   WHEEL speed   DRAG look   END back   F4 objects");
    } else if (possessedRover_ >= 0 && has) {
        std::snprintf(l1, sizeof(l1), "DRIVING %s   %.1f M/S", src.name.c_str(), src.speed);
        std::snprintf(l2, sizeof(l2), "W S throttle   A D steer   SHIFT boost   C view   BACKSPACE let it drive itself   (the UAVs hunt you)");
        head = kEnemy;
    } else if (camera_.mode == CameraMode::Pov && has) {
        if (povHold_ >= 0.0f) {
            std::snprintf(l1, sizeof(l1), "%s   %s", src.name.c_str(), povProjectile(src.kind) ? "IMPACT" : "DESTROYED");
            std::snprintf(l2, sizeof(l2), "END back   F4 objects   INS DEL next");
        } else {
            std::snprintf(l1, sizeof(l1), "POV  %s   %s   %.1f M/S", src.name.c_str(), povViewName(povView_), src.speed);
            std::snprintf(l2, sizeof(l2), "%s   C view   INS DEL next   F4 objects   HOME free cam   END back",
                          src.canControl ? "W A S D / ARROWS take control" : "SPECTATING (autonomous)");
        }
    } else {
        return;
    }
    const float pw = std::max(hud.textWidth(l1, 18 * s), hud.textWidth(l2, 12 * s)) + 70 * s;
    hud.rect(x - 8 * s, y - 6 * s, pw, 52 * s, kPanel);
    if (has) povIcon(hud, x, y + 1 * s, s, src.kind);
    label(hud, x + (has ? 48 * s : 0.0f), y, l1, 18 * s, head);
    hud.text(x, y + 27 * s, l2, 12 * s, kDim);
}

void App::drawAlertVignette(int w, int h, float a) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f, W = static_cast<float>(w), H = static_cast<float>(h);
    const float pulse = 0.7f + 0.3f * std::sin(scene_->time() * 9.0f);
    const int bands = 10;
    const float bw = 9.0f * s;
    for (int i = 0; i < bands; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(bands);
        const Rgba c{0.9f, 0.05f, 0.03f, clampf(a, 0.0f, 1.0f) * pulse * 0.42f * (1.0f - t) * (1.0f - t)};
        const float o = bw * static_cast<float>(i);
        hud.rect(o, o, W - 2 * o, bw, c);
        hud.rect(o, H - o - bw, W - 2 * o, bw, c);
        hud.rect(o, o + bw, bw, H - 2 * o - 2 * bw, c);
        hud.rect(W - o - bw, o + bw, bw, H - 2 * o - 2 * bw, c);
    }
    if (a > 0.45f) labelC(hud, W * 0.5f, H * 0.5f + 150 * s, "LASER DESIGNATED - MOVE!", 20 * s, Rgba{1.0f, 0.25f, 0.18f, pulse});
}

void App::drawGameModeHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f, W = static_cast<float>(w), H = static_cast<float>(h);
    const float cx = W * 0.5f, cy = H * 0.5f, inset = letterbox_ * H;
    drawGameHud(cam, w, h, false, inset);

    // Compass strip under the top bar: 120 degrees, ticks every 15, objective markers.
    const Vec3 fwd{-cam.view.at(2, 0), -cam.view.at(2, 1), -cam.view.at(2, 2)};
    const float hdg = headingDeg(fwd), span = 120.0f, sw = 560.0f * s, sy = inset + 12.0f * s + 66.0f * s + 14.0f * s;
    hud.rect(cx - sw * 0.5f, sy, sw, 26 * s, Rgba{0, 0, 0, 0.35f});
    for (const CompassTick& t : compassTicks(hdg, span, sw, 15)) {
        const float x = cx + t.x;
        hud.line(x, sy + 26 * s, x, sy + (t.major ? 15.0f : 20.0f) * s, t.major ? kWhite : kDim);
        if (t.major) labelC(hud, x, sy + 1 * s, t.label, 12 * s, t.deg == 0 ? kAmber : kWhite);
    }
    hud.line(cx, sy + 26 * s, cx - 6 * s, sy + 34 * s, kAmber);
    hud.line(cx, sy + 26 * s, cx + 6 * s, sy + 34 * s, kAmber);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%03.0f", hdg);
    labelC(hud, cx, sy + 36 * s, buf, 13 * s, kAmber);
    auto marker = [&](const Vec3& p, Rgba c, const char* text) {
        bool inside = false;
        const float off = compassOffset(hdg, headingDeg(p - cam.position), span, sw, &inside);
        const Rgba cc = inside ? c : withAlpha(c, 0.45f);
        diamond(hud, cx + off, sy + 13 * s, 5 * s, cc);
        if (text && inside) labelC(hud, cx + off, sy - 14 * s, text, 11 * s, cc);
    };
    marker({0.0f, 0.0f, 0.0f}, Rgba{0.35f, 1.0f, 0.5f, 1.0f}, "BASE");
    PovSource src;
    const bool has = currentSource(src);
    for (const auto& in : scene_->intruders())
        if (in->life == Intruder::Life::Alive && !(has && src.kind == PovKind::Rover && src.position.x == in->state.position.x))
            marker(in->state.position, kEnemy, nullptr);
    if (possessedRover_ >= 0 && possessedRover_ < static_cast<int>(scene_->intruders().size()))
        marker(scene_->intruders()[possessedRover_]->objective, kGold, "OBJECTIVE");

    // Crosshair whenever the view is someone's eyes or sight; its gap opens with recoil and speed.
    int soldier = -1;
    const bool fp = firstPersonSoldier(soldier) || (camera_.mode == CameraMode::Possess && (camera_.possessSight || possessAlt_)) ||
                    (camera_.mode == CameraMode::Pov && povHold_ < 0.0f && povView_ != PovView::Chase) || camera_.mode == CameraMode::Walk;
    if (fp) {
        const float move = has ? clampf(src.speed / 4.0f, 0.0f, 1.0f) : 0.0f;
        const float gap = (5.0f + 9.0f * recoil_ + 5.0f * move) * s, arm = 9.0f * s;
        const Rgba c{0.95f, 0.97f, 1.0f, 0.9f}, sh{0, 0, 0, 0.6f};
        for (int pass = 0; pass < 2; ++pass) {
            const Rgba cc = pass ? c : sh;
            const float o = pass ? 0.0f : 1.0f;
            hud.line(cx - gap - arm + o, cy + o, cx - gap + o, cy + o, cc);
            hud.line(cx + gap + o, cy + o, cx + gap + arm + o, cy + o, cc);
            hud.line(cx + o, cy - gap - arm + o, cx + o, cy - gap + o, cc);
            hud.line(cx + o, cy + gap + o, cx + o, cy + gap + arm + o, cc);
        }
        hud.rect(cx - 1, cy - 1, 2, 2, c);
    }
    const float hm = hitMarkerAlpha(scene_->time() - hitMarkerT_);
    if (hm > 0.0f) {
        const Rgba c{1.0f, 0.3f, 0.2f, hm};
        const float a = 7.0f * s, b = 16.0f * s;
        hud.line(cx - a, cy - a, cx - b, cy - b, c);
        hud.line(cx + a, cy - a, cx + b, cy - b, c);
        hud.line(cx - a, cy + a, cx - b, cy + b, c);
        hud.line(cx + a, cy + a, cx + b, cy + b, c);
    }

    // What the camera is (bottom left), the controlled object's status line (bottom centre).
    drawPovPanel(w, h, H - inset - 64 * s);
    if (camera_.mode == CameraMode::Possess && possessed_ >= 0) {
        const std::vector<Controllable*> list = sa::controllables(*scene_);
        if (possessed_ < static_cast<int>(list.size()) && list[possessed_]) {
            std::snprintf(buf, sizeof(buf), "CONTROL  %s", list[possessed_]->name());
            label(hud, 22 * s, H - inset - 64 * s, buf, 18 * s, kAmber);
            hud.text(22 * s, H - inset - 37 * s, list[possessed_]->status(), 12 * s, kDim);
        }
    }
    if (alert_ > 0.02f) drawAlertVignette(w, h, alert_);
    label(hud, W - 190 * s, H - inset - 22 * s, "F11 GAME MODE   F4 POV LIST", 11 * s, kDim);
}

}  // namespace ts
