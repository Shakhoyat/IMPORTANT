// v9 M5: the UAV as a game: chase / FPV / gimbal views (V), the spring-arm chase camera with collision, the game-style
// flight HUD (heading tape, pitch ladder, altitude and speed tapes, reticle, lock ring, rounds), daytime enemies
// (--day-ops, Shift+F2) and the play_uav_* capture figures. The math lives in scene/UavView (unit-tested).
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/App.h"
#include "objects/Terrain.h"
#include "rendering/ImageWriter.h"
#include "rendering/RenderTarget.h"

namespace ts {

namespace {

const Rgba kHudFg{0.55f, 1.0f, 0.78f, 0.95f};
const Rgba kHudDim{0.55f, 1.0f, 0.78f, 0.55f};
const Rgba kHudBack{0.0f, 0.0f, 0.0f, 0.35f};
const Rgba kHudLock{1.0f, 0.25f, 0.18f, 1.0f};
const Rgba kHudWarn{1.0f, 0.75f, 0.15f, 1.0f};

// Rotates (x, y) about (cx, cy) by 'a' radians (screen pixels, y down).
void rotateAbout(float cx, float cy, float a, float& x, float& y) {
    const float dx = x - cx, dy = y - cy, c = std::cos(a), s = std::sin(a);
    x = cx + dx * c - dy * s;
    y = cy + dx * s + dy * c;
}

void ringLine(Hud& hud, float cx, float cy, float r, float a0, float a1, int seg, Rgba c) {
    for (int i = 0; i < seg; ++i) {
        const float t0 = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(seg);
        const float t1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / static_cast<float>(seg);
        hud.line(cx + std::cos(t0) * r, cy + std::sin(t0) * r, cx + std::cos(t1) * r, cy + std::sin(t1) * r, c);
    }
}

}  // namespace

// ------------------------------------------------------------------------------------------------ view state

bool App::currentUav(int& uav) const {
    uav = -1;
    if (camera_.mode == CameraMode::Possess && possessed_ >= 0 && possessedSlot_ == static_cast<int>(Slot::Uav)) uav = 0;
    else if (camera_.mode == CameraMode::Pov && povKey_ != 0 && povHold_ < 0.0f && povSrc_.kind == PovKind::Uav)
        uav = povSrc_.uav;
    return uav >= 0 && uav < kUavCount;
}

bool App::uavGameView() const {
    int uav = -1;
    if (!currentUav(uav)) return false;
    if (camera_.mode == CameraMode::Possess) return !camera_.possessSight && !possessAlt_;
    return povView_ == PovView::Chase;
}

UavView App::uavView() const {
    int uav = -1;
    if (!currentUav(uav)) return UavView::Chase;
    const bool gimbal = camera_.mode == CameraMode::Possess ? camera_.possessSight : povView_ == PovView::First;
    if (gimbal) return UavView::Gimbal;
    return uavFpv_ ? UavView::Fpv : UavView::Chase;
}

void App::cycleUavView() {
    int uav = -1;
    if (!currentUav(uav)) return;
    const RigPose from = shownPose_;
    const bool alt = camera_.mode == CameraMode::Possess ? possessAlt_ : povView_ == PovView::Alt;
    const UavView next = alt ? UavView::Chase : nextUavView(uavView());
    const bool possess = camera_.mode == CameraMode::Possess;
    if (possess) possessAlt_ = false;
    if (next == UavView::Gimbal) {
        uavFpv_ = false;
        if (possess) {
            camera_.possessSight = true;   // the manual UAV's sight is the thermal gimbal camera (tracker HUD)
            if (settings_.view != ViewMode::Thermal) {
                viewBeforeGimbal_ = settings_.view;
                settings_.view = ViewMode::Thermal;
                gimbalSwitchedView_ = true;
            }
        } else {
            povView_ = PovView::First;
        }
    } else {
        uavFpv_ = next == UavView::Fpv;
        if (possess) camera_.possessSight = false;
        else povView_ = PovView::Chase;
        if (gimbalSwitchedView_) {
            settings_.view = viewBeforeGimbal_;
            gimbalSwitchedView_ = false;
        }
        uavChase_.invalidate();
        uavPoseValid_ = false;
    }
    povLookYaw_ = povLookPitch_ = 0.0f;
    beginJump(from);
    std::printf("UAV-%d view: %s\n", uav + 1, uavViewName(next));
}

void App::setDayOps(bool on) {
    opt_.dayOps = on;
    if (scene_) scene_->setDayOps(on);
    std::printf("Day ops %s: %s\n", on ? "ON" : "OFF",
                on ? "rover waves by day too, the UAVs scramble" : "daytime waves end, the UAVs return to their pads by day");
}

RigPose App::computeUavPose(int uav, float dt) {
    if (uavFpv_) return uavFpvPose(scene_->uav(uav).root->world);
    const DroneState& d = scene_->droneState(uav);
    UavChaseInput in;
    in.position = d.position;
    in.velocity = d.velocity;
    in.yawDeg = d.yawDeg;
    // Collision: static solids and nearby vehicles (the arm probe of the v8 third-person camera), without the drone's
    // own parts, plus the ground / floors / walls of the walk space.
    const float reach = uavChaseParams_.distance + uavChaseParams_.height + 2.0f;
    gatherNearSolids(d.position, reach);
    tpBoxes_.erase(std::remove_if(tpBoxes_.begin(), tpBoxes_.end(),
                                  [&](const NearBox& b) { return length(b.c - d.position) < 0.6f; }),
                   tpBoxes_.end());
    tpFeetY_ = d.position.y;
    const ArmCast cast = [this](const Vec3& o, const Vec3& dir, float len, float r) { return armCast(o, dir, len, r); };
    return uavChase_.update(in, dt, uavChaseParams_, cast);
}

void App::updateUavView(float dt) {
    int uav = -1;
    if (!currentUav(uav)) {
        uavViewKey_ = -1;
        uavFpv_ = false;
        uavChase_.invalidate();
        uavPoseValid_ = false;
        if (gimbalSwitchedView_) {   // left the UAV while on its thermal sight: the view it had before comes back
            settings_.view = viewBeforeGimbal_;
            gimbalSwitchedView_ = false;
        }
        return;
    }
    if (uav != uavViewKey_) {   // a new UAV: the chase camera starts at rest behind it
        uavViewKey_ = uav;
        uavChase_.invalidate();
    }
    if (!uavGameView()) {
        uavChase_.invalidate();
        uavPoseValid_ = false;
        return;
    }
    uavPose_ = computeUavPose(uav, dt);
    uavPoseValid_ = true;
}

RigPose App::uavGamePose() {
    int uav = -1;
    if (!currentUav(uav)) return shownPose_;
    if (!uavPoseValid_ || uav != uavViewKey_) {
        uavViewKey_ = uav;
        uavPose_ = computeUavPose(uav, 0.0f);
        uavPoseValid_ = true;
    }
    return uavPose_;
}

// ------------------------------------------------------------------------------------------------ HUD

void App::drawUavGameHud(const CameraView& cam, int w, int h) {
    int uav = -1;
    if (!currentUav(uav)) return;
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    const float W = static_cast<float>(w), H = static_cast<float>(h), cx = W * 0.5f, cy = H * 0.5f;
    const DroneState& d = scene_->droneState(uav);
    const Squad& sq = scene_->squad();
    const float t = scene_->time();
    char buf[160];

    // Attitude of the view (the ladder lines up with the real horizon in every view) and of the airframe.
    const RigPose view = RigPose::fromView(cam.view, cam.position, fovFromProjection(cam.proj), cam.nearZ);
    float camHdg = 0.0f, camPitch = 0.0f, camRoll = 0.0f;
    poseAttitude(view, camHdg, camPitch, camRoll);
    const float heading = std::fmod(d.yawDeg + 720.0f, 360.0f);
    const float agl = d.position.y - terrainHeight(d.position.x, d.position.z);
    const float speed = std::hypot(d.velocity.x, d.velocity.z);

    // Pitch ladder (centre), rotated against the view's roll: horizon line with a gap, rungs every 10 deg.
    {
        const float roll = radians(-camRoll);
        for (const LadderRung& r : pitchLadder(camPitch, view.fovDeg, H, 10, H * 0.30f)) {
            const float y = cy - r.offset;
            const bool horizon = r.deg == 0;
            const float half = (horizon ? 210.0f : 70.0f) * s, gap = (horizon ? 60.0f : 26.0f) * s;
            const Rgba c = horizon ? kHudFg : kHudDim;
            for (int side = -1; side <= 1; side += 2) {
                float x0 = cx + side * gap, y0 = y, x1 = cx + side * half, y1 = y;
                if (r.deg < 0) {   // below the horizon: dashed
                    for (int k = 0; k < 3; ++k) {
                        float ax = x0 + (x1 - x0) * (k / 3.0f), ay = y0, bx = x0 + (x1 - x0) * ((k + 0.6f) / 3.0f), by = y1;
                        rotateAbout(cx, cy, roll, ax, ay);
                        rotateAbout(cx, cy, roll, bx, by);
                        hud.line(ax, ay, bx, by, c);
                    }
                } else {
                    rotateAbout(cx, cy, roll, x0, y0);
                    rotateAbout(cx, cy, roll, x1, y1);
                    hud.line(x0, y0, x1, y1, c);
                }
                if (!horizon) {   // end tick towards the horizon + the value
                    float tx = cx + side * half, ty = y, ex = tx, ey = y + (r.deg > 0 ? 8.0f : -8.0f) * s;
                    rotateAbout(cx, cy, roll, tx, ty);
                    rotateAbout(cx, cy, roll, ex, ey);
                    hud.line(tx, ty, ex, ey, c);
                    std::snprintf(buf, sizeof(buf), "%d", r.deg);
                    float lx = cx + side * (half + 22.0f * s), ly = y;
                    rotateAbout(cx, cy, roll, lx, ly);
                    hud.text(lx - hud.textWidth(buf, 12 * s) * 0.5f, ly - 6 * s, buf, 12 * s, c);
                }
            }
        }
    }

    // Heading tape (top): 90 degrees across 520 px, the airframe heading under the caret.
    {
        const float tw = 520.0f * s, ty = 100.0f * s;   // under the wave / score panel
        hud.rect(cx - tw * 0.5f - 8 * s, ty - 18 * s, tw + 16 * s, 40 * s, kHudBack);
        for (const CompassTick& k : compassTicks(heading, 90.0f, tw, 5)) {
            const float x = cx + k.x;
            const bool ten = k.deg % 10 == 0;
            hud.line(x, ty + 18 * s, x, ty + (k.major ? 6.0f : ten ? 10.0f : 14.0f) * s, k.major ? kHudFg : kHudDim);
            if (k.major) hud.text(x - hud.textWidth(k.label, 14 * s) * 0.5f, ty - 14 * s, k.label, 14 * s, kHudFg);
            else if (k.deg % 30 == 0) {
                std::snprintf(buf, sizeof(buf), "%02d", k.deg / 10);
                hud.text(x - hud.textWidth(buf, 11 * s) * 0.5f, ty - 11 * s, buf, 11 * s, kHudDim);
            }
        }
        hud.tri(cx, ty + 20 * s, cx - 6 * s, ty + 30 * s, cx + 6 * s, ty + 30 * s, kHudFg);
        std::snprintf(buf, sizeof(buf), "%03.0f", heading);
        hud.rect(cx - 26 * s, ty + 32 * s, 52 * s, 20 * s, kHudBack);
        hud.frame(cx - 26 * s, ty + 32 * s, 52 * s, 20 * s, kHudFg);
        hud.text(cx - hud.textWidth(buf, 15 * s) * 0.5f, ty + 35 * s, buf, 15 * s, kHudFg);
    }

    // Altitude (left) and ground-speed (right) tapes with boxed read-outs.
    const auto tape = [&](float x, float value, float step, float ppu, const char* label, const char* unit, bool left) {
        const float half = 150.0f * s;
        hud.rect(left ? x - 64 * s : x, cy - half - 26 * s, 64 * s, 2 * half + 48 * s, kHudBack);   // labels on the backing too
        hud.line(x, cy - half, x, cy + half, kHudDim);
        for (const TapeMark& m : tapeMarks(value, step, 5, ppu, half)) {
            const float y = cy - m.offset;
            hud.line(x, y, x + (left ? -1.0f : 1.0f) * (m.major ? 12.0f : 6.0f) * s, y, m.major ? kHudFg : kHudDim);
            if (m.major) {
                std::snprintf(buf, sizeof(buf), "%.0f", m.value);
                hud.text(left ? x - 18 * s - hud.textWidth(buf, 12 * s) : x + 18 * s, y - 6 * s, buf, 12 * s, kHudDim);
            }
        }
        std::snprintf(buf, sizeof(buf), "%5.1f", value);
        const float bx = left ? x - 76 * s : x + 6 * s;
        hud.rect(bx, cy - 12 * s, 70 * s, 24 * s, Rgba{0, 0, 0, 0.7f});
        hud.frame(bx, cy - 12 * s, 70 * s, 24 * s, kHudFg);
        hud.text(bx + 6 * s, cy - 8 * s, buf, 16 * s, kHudFg);
        hud.text(left ? x - 64 * s : x + 4 * s, cy - half - 22 * s, label, 13 * s, kHudFg);
        hud.text(left ? x - 64 * s : x + 4 * s, cy + half + 6 * s, unit, 12 * s, kHudDim);
    };
    tape(cx - 330.0f * s, agl, 1.0f, 16.0f * s, "ALT AGL", "M", true);
    tape(cx + 330.0f * s, speed, 1.0f, 16.0f * s, "GND SPD", "M/S", false);
    std::snprintf(buf, sizeof(buf), "VS %+.1f", d.velocity.y);
    hud.text(cx - 330.0f * s - 64 * s, cy + 190 * s, buf, 13 * s, kHudDim);

    // Boresight (FPV) / flight-path marker, and the reticle on the gimbal's aim point (where ENTER sends a round).
    const Mat4 vp = cam.proj * cam.view;
    if (uavFpv_) {
        hud.line(cx - 22 * s, cy, cx - 8 * s, cy, kHudFg);
        hud.line(cx + 8 * s, cy, cx + 22 * s, cy, kHudFg);
        hud.line(cx, cy + 8 * s, cx, cy + 16 * s, kHudFg);
    }
    if (speed > 0.4f) {   // flight-path marker: where the drone is going in 2 s
        const MarkerProjection m = projectMarker(vp, cam.position, d.position + d.velocity * 2.0f, w, h);
        if (m.onScreen) {
            ringLine(hud, m.x, m.y, 7 * s, 0.0f, 6.2831853f, 16, kHudFg);
            hud.line(m.x - 15 * s, m.y, m.x - 7 * s, m.y, kHudFg);
            hud.line(m.x + 7 * s, m.y, m.x + 15 * s, m.y, kHudFg);
            hud.line(m.x, m.y - 7 * s, m.x, m.y - 13 * s, kHudFg);
        }
    }
    Vec3 aim;
    if (scene_->uavAirborne(uav) && scene_->designatorPoint(aim, uav)) {
        const MarkerProjection m = projectMarker(vp, cam.position, aim, w, h);
        if (m.onScreen) {
            const float r = 16.0f * s;
            ringLine(hud, m.x, m.y, r, 0.0f, 6.2831853f, 28, kHudFg);
            for (int k = 0; k < 4; ++k) {
                const float a = 1.5707963f * static_cast<float>(k);
                hud.line(m.x + std::cos(a) * r * 0.45f, m.y + std::sin(a) * r * 0.45f, m.x + std::cos(a) * r * 1.5f,
                         m.y + std::sin(a) * r * 1.5f, kHudFg);
            }
            std::snprintf(buf, sizeof(buf), "AIM %.0f M", m.dist);
            hud.text(m.x + r * 1.6f, m.y + r * 0.6f, buf, 12 * s, kHudDim);
        }
    }

    // Tracks of the squad's shared picture; the target gets the lock ring (progress = this UAV's lock time).
    {
        const float need = kSquadLeadLock;
        for (const Track& tr : sq.tracker().tracks()) {
            if (tr.state == TrackState::Tentative) continue;
            const bool target = tr.id == sq.targetTrack();
            const MarkerProjection m = projectMarker(vp, cam.position, tr.position() + Vec3{0.0f, 0.2f, 0.0f}, w, h);
            if (!m.onScreen) {
                if (!target) continue;
                const EdgeArrow a = edgeArrow(m.x, m.y, m.front, w, h, 70.0f * s);
                hud.tri(a.x, a.y, a.x - a.dirX * 18 * s - a.dirY * 9 * s, a.y - a.dirY * 18 * s + a.dirX * 9 * s,
                        a.x - a.dirX * 18 * s + a.dirY * 9 * s, a.y - a.dirY * 18 * s - a.dirX * 9 * s, kHudLock);
                continue;
            }
            if (!target) {
                const float r = 8.0f * s;
                hud.line(m.x, m.y - r, m.x + r, m.y, kHudWarn);
                hud.line(m.x + r, m.y, m.x, m.y + r, kHudWarn);
                hud.line(m.x, m.y + r, m.x - r, m.y, kHudWarn);
                hud.line(m.x - r, m.y, m.x, m.y - r, kHudWarn);
                continue;
            }
            const float prog = lockProgress(sq.lock[uav], need);
            const bool locked = prog >= 1.0f;
            const float r = std::max(26.0f * s, markerHalfSize(m.dist, h, 0.8f, view.fovDeg) * 1.4f);
            const Rgba c = locked ? Rgba{kHudLock.r, kHudLock.g, kHudLock.b, 0.65f + 0.35f * std::sin(t * 10.0f)} : kHudWarn;
            ringLine(hud, m.x, m.y, r, 0.0f, 6.2831853f, 40, Rgba{c.r, c.g, c.b, 0.35f});
            if (prog > 0.0f) hud.arc(m.x, m.y, r, r + 4 * s, -1.5707963f, -1.5707963f + 6.2831853f * prog, c);
            for (int k = 0; k < 4; ++k) {
                const float a = 0.7853982f + 1.5707963f * static_cast<float>(k);
                hud.line(m.x + std::cos(a) * r * 0.7f, m.y + std::sin(a) * r * 0.7f, m.x + std::cos(a) * r * 0.95f,
                         m.y + std::sin(a) * r * 0.95f, c);
            }
            std::snprintf(buf, sizeof(buf), "%s  TRK %d  %.0f M", locked ? "LOCK" : "LOCKING", tr.id, m.dist);
            hud.text(m.x - hud.textWidth(buf, 13 * s) * 0.5f, m.y - r - 22 * s, buf, 13 * s, c);
        }
    }

    // Rounds on the rails (bottom right): filled = ready, hollow = reloading.
    {
        const int ready = scene_->roundsReady(uav);
        const float x0 = W - 520.0f * s, y0 = H - 120.0f * s;   // left of the clock / mini-map
        hud.rect(x0 - 10 * s, y0 - 30 * s, 270 * s, 84 * s, kHudBack);
        std::snprintf(buf, sizeof(buf), "MSL  %d / %d", ready, kUavMissiles);
        hud.text(x0, y0 - 24 * s, buf, 16 * s, ready > 0 ? kHudFg : kHudWarn);
        for (int i = 0; i < kUavMissiles; ++i) {
            const float x = x0 + static_cast<float>(i) * 60.0f * s, y = y0 + 6 * s;
            const Rgba c = i < ready ? kHudFg : kHudDim;
            if (i < ready) hud.rect(x, y, 40 * s, 10 * s, c);
            else hud.frame(x, y, 40 * s, 10 * s, c);
            hud.tri(x + 40 * s, y, x + 52 * s, y + 5 * s, x + 40 * s, y + 10 * s, c);
        }
        hud.text(x0, y0 + 26 * s, camera_.mode == CameraMode::Possess ? "ENTER  LAUNCH" : "AUTONOMOUS STRIKES", 12 * s, kHudDim);
    }

    // Status (bottom left): UAV, duty, view, day ops, warnings, keys.
    {
        const float x0 = 30.0f * s, y0 = H - 222.0f * s;   // above the status rows of the default HUD
        hud.rect(x0 - 10 * s, y0 - 8 * s, 520 * s, 120 * s, kHudBack);
        std::snprintf(buf, sizeof(buf), "UAV-%d  %s  //  %s VIEW", uav + 1, scene_->uavDutyState(uav), uavViewName(uavView()));
        hud.text(x0, y0, buf, 17 * s, kHudFg);
        std::snprintf(buf, sizeof(buf), "ROLE %s   LOCK %.1f S   ROTORS %3.0f %%%s", squadRoleName(sq.roles[uav]), sq.lock[uav],
                      100.0f * scene_->uavRotorSpool(uav), scene_->dayOps() ? "   DAY OPS" : "");
        hud.text(x0, y0 + 26 * s, buf, 13 * s, kHudDim);
        const char* warn = nullptr;
        if (scene_->uavRotorSpool(uav) < 1.0f) warn = "ROTORS SPOOLING";
        else if (scene_->uavAirborne(uav) && agl < 1.5f) warn = "LOW ALTITUDE";
        if (warn && std::fmod(t, 0.8f) < 0.55f) hud.text(x0, y0 + 48 * s, warn, 15 * s, kHudWarn);
        hud.text(x0, y0 + 74 * s,
                 camera_.mode == CameraMode::Possess ? "W/S FLY  A/D YAW  PGUP/PGDN CLIMB  ARROWS GIMBAL  ENTER LAUNCH"
                                                     : "RIDING ALONG  -  W A S D TAKES CONTROL (UAV-1)",
                 12 * s, kHudDim);
        hud.text(x0, y0 + 92 * s, "V  CHASE / FPV / GIMBAL   SHIFT+F2  DAY OPS   BACKSPACE  RELEASE", 12 * s, kHudDim);
    }
}

// ------------------------------------------------------------------------------------------------ captures
// --capture DIR --only play_uav_*: the docked UAVs by day, the rotor blur (spin-up and full speed), day-ops scramble
// with the chase camera, and at night the chase / FPV / gimbal views and the HUD with a lock on a rover.
int App::captureUav() {
    const std::string dir = opt_.outPath.empty() ? "captures" : opt_.outPath;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int width = opt_.sizeGiven ? opt_.width : 1280, height = opt_.sizeGiven ? opt_.height : 720;
    constexpr float kDt = 1.0f / 60.0f;
    settings_ = RenderSettings{};
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    std::printf("uav capture: %dx%d, quality %s\n", width, height, qualityName(settings_.quality));
    showHud_ = true;
    debugHud_ = false;   // v9 M2: the decluttered game HUD, as the interactive game starts (F3 debug blocks off)
    showFeed_ = true;
    playCapture_ = true;
    RenderTarget target;
    if (!target.create(width, height, 0, false)) return 1;
    int written = 0, failed = 0;
    auto wanted = [&](const char* name) { return opt_.only.find("," + std::string(name) + ",") != std::string::npos; };
    auto run = [&](float seconds) {
        for (float t = 0.0f; t < seconds; t += kDt) {
            step(kDt);
            updateGameCamera(kDt);
            target.bind();
            renderFrame(width, height, false);
        }
    };
    auto shot = [&](const char* name, bool hud = true) {
        if (!wanted(name)) return;
        renderer_->resetHistory();
        const int frames = qualityProfile(settings_.quality).taa ? 8 : 1;
        for (int f = 0; f < frames; ++f) {
            target.bind();
            renderFrame(width, height, hud);
        }
        std::vector<uint8_t> rgb;
        target.read(rgb);
        const std::string path = dir + "/" + name + ".png";
        const bool ok = writePng(path, width, height, rgb);
        const DroneState& d0 = scene_->droneState(0);
        float nx = 0.0f, ny = 0.0f;
        const bool front = poseNdc(shownPose_, static_cast<float>(width) / static_cast<float>(height), d0.position, nx, ny);
        std::printf("%s %s (t = %.1f s, hour %.2f, UAV-1 %s at %.2f %.2f %.2f spool %.2f, UAV-2 %s, view %s, drone ndc %s %.2f %.2f, "
                    "arm %.2f/%.2f, rovers %d, tracks %zu, lock %.1f/%.1f)\n",
                    ok ? "wrote" : "FAILED", path.c_str(), scene_->time(), scene_->dayCycle().hour(), scene_->uavDutyState(0),
                    d0.position.x, d0.position.y, d0.position.z, scene_->uavRotorSpool(0), scene_->uavDutyState(1),
                    uavViewName(uavView()), front ? "front" : "behind", nx, ny, uavChase_.arm(), uavChase_.wantedArm(),
                    scene_->aliveIntruders(), scene_->squad().tracker().tracks().size(), scene_->squad().lock[0],
                    scene_->squad().lock[1]);
        ok ? ++written : ++failed;
    };
    auto freeView = [&](const Vec3& eye, const Vec3& at, float fov) {
        camera_ = Camera{};
        camera_.mode = CameraMode::Free;
        camera_.freePos = eye;
        const Vec3 dv = normalize(at - eye);
        camera_.freeYawDeg = degrees(std::atan2(dv.x, dv.z));
        camera_.freePitchDeg = degrees(std::asin(dv.y));
        camera_.fovDeg = fov;
        rig_.cancel();
    };
    auto world = [&](float hour, bool dayOps, float warm) {
        opt_.hour = hour;
        opt_.timeGiven = true;
        opt_.autoCycle = false;
        opt_.dayOps = dayOps;
        restartWorld(warm, false);
        uavChase_.invalidate();
        uavPoseValid_ = false;
        uavFpv_ = false;
    };

    // ---- by day: both UAVs docked on their pads over the bunker (rotors still, no vision), then day ops scramble
    world(10.5f, false, 3.0f);
    const Vec3 pad0 = scene_->squad().pad(0).touchdown, pad1 = scene_->squad().pad(1).touchdown;
    const Vec3 mid = (pad0 + pad1) * 0.5f;
    freeView(mid + Vec3{1.2f, 1.9f, 3.6f}, mid + Vec3{0.0f, -0.1f, 0.0f}, 50.0f);
    run(0.3f);
    shot("play_uav_docked_day");
    // Close-up of UAV-1 on its pad, rotors still (blades visible).
    freeView(pad0 + Vec3{0.55f, 0.35f, 0.75f}, pad0, 45.0f);
    run(0.2f);
    shot("play_uav_rotor_still", false);
    setDayOps(true);   // the UAVs scramble: UAV-1 spools up at once, UAV-2 2.5 s later
    run(0.85f);        // spool ~0.42 -> 17 rev/s: half blade, half blur disc
    shot("play_uav_rotor_spinup", false);
    run(1.4f);         // full speed: only the blur disc
    freeView(scene_->droneState(0).position + Vec3{0.55f, 0.30f, 0.75f}, scene_->droneState(0).position, 45.0f);
    run(kDt);
    shot("play_uav_rotor_blur", false);
    // Take UAV-1 (key 0): the chase camera; fly out over the camp with rovers on the field by day.
    possessSlot(Slot::Uav);
    run(0.2f);
    held_.lift = 1.0f;
    run(1.5f);
    held_.lift = 0.0f;
    held_.forward = 1.0f;
    run(2.5f);
    held_.turn = 1.0f;
    run(0.8f);
    shot("play_uav_dayops_chase");
    held_ = ControlInput{};
    possess(-1);

    // ---- night: chase, FPV, gimbal and the HUD with a lock
    world(23.0f, false, 4.0f);
    possessSlot(Slot::Uav);
    run(0.3f);
    held_.forward = 1.0f;
    run(2.0f);
    held_.turn = 1.0f;    // D: a right turn, the chase camera banks into it
    run(0.9f);
    shot("play_uav_chase");
    held_.turn = 0.0f;
    run(1.0f);
    shot("play_uav_chase_steady");
    held_.forward = 0.0f;
    cycleUavView();   // FPV
    run(0.8f);
    shot("play_uav_fpv");
    cycleUavView();   // gimbal (thermal sight of the tracker)
    run(0.6f);
    shot("play_uav_gimbal");
    cycleUavView();   // back to the chase camera
    run(0.8f);
    // Wait for the squad to hold a target, keeping UAV-1 pointed at it a few metres off (gimbal assist on).
    for (float t = 0.0f; t < 90.0f; t += kDt) {
        const Track* tr = scene_->squad().tracker().find(scene_->squad().targetTrack());
        if (tr) {
            DroneState& d = scene_->drone;
            const Vec3 to = tr->position() - d.position;
            const float want = degrees(std::atan2(to.x, to.z));
            float diff = want - d.yawDeg;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            held_.turn = clampf(-diff / 30.0f, -1.0f, 1.0f);   // D lowers the yaw
            const float dist = std::hypot(to.x, to.z);
            held_.forward = clampf((dist - 15.0f) / 4.0f, -1.0f, 1.0f);
            float tx = 0.0f, ty = 0.0f;   // the target well inside the chase view, the lock building or held
            const bool inView = poseNdc(shownPose_, static_cast<float>(width) / static_cast<float>(height),
                                        tr->position(), tx, ty) && std::fabs(tx) < 0.7f && std::fabs(ty) < 0.7f;
            if (inView && scene_->squad().lock[0] >= 0.5f * kSquadLeadLock && std::fabs(diff) < 15.0f) break;
        }
        step(kDt);
        updateGameCamera(kDt);
        target.bind();
        renderFrame(width, height, false);
    }
    held_ = ControlInput{};
    shot("play_uav_hud");
    possess(-1);
    playCapture_ = false;
    std::printf("uav capture: %d written, %d failed\n", written, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace ts
