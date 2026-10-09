// v7 game camera (WP-G): ride along with any moving object (POV picker, Insert / Delete, middle click), take
// control from any POV (possession, the intruder rover's wheel), free fly camera (Home / End), the spring-blended
// jumps and the game-feel state (FOV kick, letterbox, hit marker, alert), the possessed soldier's rifle and the
// first-person viewmodel (rifle + hands in front of the eyes), and the game_* capture figures.
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app/App.h"
#include "objects/PropKit.h"
#include "rendering/ImageWriter.h"
#include "rendering/RenderTarget.h"

namespace ts {

namespace {

constexpr float kHoldAfterEnd = 2.5f;   // s the camera holds on an impact / a destroyed rover before going back

// v7 WP-P first-person carbine. Rifle frame: origin at the receiver centre, +Z towards the muzzle, +Y up; +X is the
// side the player sees. kGunEye: the frame origin in eye space (right, up, -forward); kMuzzleZ: muzzle tip along +Z.
const Vec3 kGunEye{0.140f, -0.152f, -0.40f};
constexpr float kMuzzleZ = 0.775f;
constexpr float kSprintFovKick = 6.0f;   // v9 M3: degrees added at full sprint

PovView defaultView(PovKind k) { return povProjectile(k) ? PovView::Chase : PovView::First; }

// Top speed per kind for the FOV kick (m/s).
float topSpeed(PovKind k) {
    switch (k) {
        case PovKind::Soldier: return 4.5f;
        case PovKind::Rover: return kRoverBoostSpeed;
        case PovKind::Uav: return 8.0f;
        case PovKind::Helicopter: return 18.0f;
        default: return 10.0f;
    }
}

// World matrices of a subtree under a new parent matrix (the viewmodel moves whole groups).
void reworld(SceneNode* n, const Mat4& world) {
    n->world = world;
    for (auto& c : n->children) reworld(c.get(), world * c->local.matrix());
}

}  // namespace

// ------------------------------------------------------------------------------------------------ view composition

RigPose App::targetPose(float aspect) {
    if (camera_.mode == CameraMode::Pov) {
        if (povHold_ >= 0.0f) return povHoldPose_;
        RigPose p;
        if (povPose(*scene_, povSrc_, povView_, p, povLookYaw_, povLookPitch_)) return p;
        return shownPose_;
    }
    if (camera_.mode == CameraMode::Possess && possessed_ >= 0) {
        int soldier = -1;
        if (camera_.possessSight && firstPersonSoldier(soldier)) {
            RigPose eye = soldierEyePose(*scene_, soldier);
            // v9 M3: the player's own soldier: a stable eye (drawn feet + crouch-blended eye height + landing dip,
            // Soldier::camera) plus a subtle head bob from the gait phase and the actual ground speed (headBob_);
            // mouse look stays direct (the heading / pitch of the tick, no smoothing).
            if (soldier == possessedSoldierIndex())
                if (const Soldier* s = dynamic_cast<const Soldier*>(sa::controllables(*scene_)[soldier])) {
                    const CameraHint h = s->camera(true);
                    eye = RigPose::look(h.eye, h.target, h.fovDeg);
                    eye.nearZ = 0.06f;
                    if (headBob_ && !s->airborne()) {
                        const Vec3 b = headBobOffset(s->gaitPhase(), s->motor().groundSpeed());
                        eye.eye = eye.eye + eye.right() * b.x + Vec3{0.0f, b.y, 0.0f};
                    }
                }
            eye.fovDeg = lerpf(eye.fovDeg, 44.0f, aimBlend_);   // v8: right mouse = iron-sight zoom
            return eye;
        }
        if (!camera_.possessSight && tpPoseValid_ && possessedSoldierIndex() >= 0) return tpPose_;   // v8 spring arm
        PovSource src;
        if (possessAlt_ && currentSource(src)) {
            RigPose p;
            if (povPose(*scene_, src, PovView::Alt, p)) return p;
        }
    }
    const CameraView v = camera_.compute(*scene_, aspect);
    return RigPose::fromView(v.view, v.position, fovFromProjection(v.proj), v.nearZ);
}

CameraView App::composeView(float aspect) {
    if (viewOverride_) {   // v9 M1: the interactive loop draws a pose interpolated between two simulation ticks
        const RigPose& p = *viewOverride_;
        shownPose_ = p;
        shownValid_ = true;
        CameraView v;
        v.position = p.eye;
        v.view = p.view();
        v.nearZ = p.nearZ;
        v.farZ = tickFar_;
        v.proj = perspective(radians(clampf(p.fovDeg, 1.0f, 170.0f)), aspect, v.nearZ, v.farZ);
        return v;
    }
    int soldier = -1;
    const bool custom = camera_.mode == CameraMode::Pov || (camera_.mode == CameraMode::Possess && possessAlt_) ||
                        (camera_.mode == CameraMode::Possess && camera_.possessSight && firstPersonSoldier(soldier)) ||
                        (camera_.mode == CameraMode::Possess && !camera_.possessSight && tpPoseValid_ && possessedSoldierIndex() >= 0);
    if (!custom && !rig_.active() && std::fabs(fovKick_) < 0.01f) {
        // v6 views pass through untouched (captures stay bit-identical).
        const CameraView v = camera_.compute(*scene_, aspect);
        shownPose_ = RigPose::fromView(v.view, v.position, fovFromProjection(v.proj), v.nearZ);
        shownValid_ = true;
        return v;
    }
    const RigPose p = rig_.evaluate(targetPose(aspect));
    shownPose_ = p;
    shownValid_ = true;
    CameraView v;
    v.position = p.eye;
    v.view = p.view();
    v.nearZ = p.nearZ;
    v.proj = perspective(radians(clampf(p.fovDeg + fovKick_, 8.0f, 115.0f)), aspect, v.nearZ, v.farZ);
    return v;
}

void App::beginJump(const RigPose& from) {
    if (!shownValid_) return;
    rig_.start(from, targetPose(lastAspect_));
}

// ------------------------------------------------------------------------------------------------ sources

bool App::currentSource(PovSource& out) const {
    if (camera_.mode == CameraMode::Pov && povKey_ != 0) {
        out = povSrc_;
        return true;
    }
    if (possessed_ < 0 && possessedRover_ < 0) return false;
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    for (const PovSource& s : list)
        if ((possessed_ >= 0 && s.controllable == possessed_) || (possessedRover_ >= 0 && s.intruder == possessedRover_)) {
            out = s;
            return true;
        }
    return false;
}

int App::possessedSoldierIndex() const {
    const std::vector<Controllable*> c = sa::controllables(*scene_);
    if (possessed_ < 0 || possessed_ >= static_cast<int>(c.size())) return -1;
    return dynamic_cast<const Soldier*>(c[possessed_]) ? possessed_ : -1;
}

bool App::firstPersonSoldier(int& index) const {
    index = -1;
    if (camera_.mode == CameraMode::Possess && camera_.possessSight) index = possessedSoldierIndex();
    else if (camera_.mode == CameraMode::Pov && povHold_ < 0.0f && povSrc_.kind == PovKind::Soldier && povView_ == PovView::First)
        index = povSrc_.controllable;
    return index >= 0;
}

// ------------------------------------------------------------------------------------------------ jumps

App::ViewMemo App::memo() const {
    ViewMemo m;
    m.valid = true;
    m.cam = camera_;
    m.possessed = possessed_;
    m.sight = camera_.possessSight;
    m.alt = possessAlt_;
    const bool pov = camera_.mode == CameraMode::Pov || (camera_.mode == CameraMode::Possess && modeBeforePossess_ == CameraMode::Pov);
    m.povKey = pov && povHold_ < 0.0f ? povKey_ : 0;
    m.povView = povView_;
    m.rover = possessedRover_;
    return m;
}

void App::restoreMemo(const ViewMemo& m) {
    const RigPose from = shownPose_;
    releaseRover();
    possess(-1);
    flyCam_ = false;
    povHold_ = -1.0f;
    bool done = false;
    if (m.povKey != 0) {
        done = jumpToPov(m.povKey, static_cast<int>(m.povView), false);
        if (done && m.rover >= 0 && povSrc_.intruder == m.rover) possessedRover_ = m.rover;
    }
    if (m.possessed >= 0) {
        possess(m.possessed);
        camera_.possessSight = m.sight;
        possessAlt_ = m.alt;
        done = possessed_ == m.possessed;
    }
    if (!done) {
        Camera c = m.cam;
        c.possessed = -1;
        if (c.mode == CameraMode::Possess || c.mode == CameraMode::Pov) c.mode = CameraMode::Orbit;
        c.walkSpace = camera_.walkSpace;
        camera_ = c;
    }
    beginJump(from);
}

void App::noteJump(const ViewMemo& before, const RigPose& from) {
    prevView_ = before;
    beginJump(from);
}

bool App::jumpToPov(uint32_t key, int view, bool interactive) {
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    const int i = findPov(list, key);
    if (i < 0) return false;
    const RigPose from = shownPose_;
    const ViewMemo before = memo();
    releaseRover();
    possess(-1);
    flyCam_ = false;
    if (interactive && (camera_.mode != CameraMode::Pov || povKey_ != key)) prevView_ = before;
    camera_.mode = CameraMode::Pov;
    povKey_ = key;
    povSrc_ = list[i];
    povView_ = view < 0 ? defaultView(list[i].kind) : static_cast<PovView>(view);
    povLookYaw_ = povLookPitch_ = 0.0f;
    povHold_ = -1.0f;
    povRound_ = ProjectileFollow{};
    if (list[i].projectile >= 0) {
        const Projectile& p = scene_->combat().projectiles()[list[i].projectile];
        povRound_.key = key;
        povRound_.kind = static_cast<int>(p.kind);
        povRound_.owner = p.owner;
        povRound_.position = p.position;
        povRound_.velocity = p.velocity;
    }
    if (interactive) {
        beginJump(from);
        audio_.uiCue(audio::UiCue::PovJump);
    }
    std::printf("POV: %s (%s, %s)\n", povSrc_.name.c_str(), povKindName(povSrc_.kind), povViewName(povView_));
    return true;
}

void App::cyclePovJump(int dir) {
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    uint32_t key = 0;
    PovSource cur;
    if (currentSource(cur)) key = cur.key;
    const int i = cyclePov(list, key, dir);
    if (i >= 0) jumpToPov(list[i].key);
}

void App::pickPovAt(double nx, double ny) {
    if (!shownValid_) return;
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    const Mat4 proj = perspective(radians(shownPose_.fovDeg + fovKick_), lastAspect_, shownPose_.nearZ, 600.0f);
    Vec3 o, d;
    pixelRay(inverse(proj * shownPose_.view()), static_cast<float>(nx) * static_cast<float>(lastW_),
             static_cast<float>(ny) * static_cast<float>(lastH_), lastW_, lastH_, o, d);
    const int i = pickPov(list, o, d);
    if (i >= 0) jumpToPov(list[i].key);
}

void App::enterFreeCam() {
    const RigPose from = shownPose_;
    prevView_ = memo();
    releaseRover();
    possess(-1);
    const Vec3 f = from.forward();
    camera_.mode = CameraMode::Free;
    camera_.freePos = from.eye;
    camera_.freeYawDeg = degrees(std::atan2(f.x, f.z));
    camera_.freePitchDeg = clampf(degrees(std::asin(clampf(f.y, -1.0f, 1.0f))), -85.0f, 85.0f);
    camera_.fovDeg = 50.0f;
    flyCam_ = true;
    fly_.velocity = {};
    povHold_ = -1.0f;
    beginJump(from);   // roll / field of view settle smoothly
    audio_.uiCue(audio::UiCue::FreeCam);
}

void App::returnToPrevious() {
    if (!prevView_.valid) return;
    const ViewMemo target = prevView_;
    prevView_ = memo();   // End again swaps back
    restoreMemo(target);
    audio_.uiCue(audio::UiCue::PovJump);
}

void App::takeControl() {
    if (camera_.mode != CameraMode::Pov || povHold_ >= 0.0f || !povSrc_.canControl) return;
    const RigPose from = shownPose_;
    if (povSrc_.controllable >= 0) {
        const PovView v = povView_;
        possess(povSrc_.controllable);
        if (possessed_ != povSrc_.controllable) return;
        camera_.possessSight = v != PovView::Chase;
        possessAlt_ = v == PovView::Alt && povSrc_.hasAlt;
        if (possessAlt_) camera_.possessSight = false;
        std::printf("Control: %s\n", possessedName());
    } else if (povSrc_.intruder >= 0) {
        possessedRover_ = povSrc_.intruder;
        std::printf("Control: %s (planner paused, the UAVs still hunt it)\n", povSrc_.name.c_str());
    }
    beginJump(from);
    audio_.uiCue(audio::UiCue::Possess);
}

void App::releaseRover() {
    if (possessedRover_ < 0) return;
    possessedRover_ = -1;
    if (scene_) scene_->setPlayerIntruder(-1, nullptr);
    held_ = ControlInput{};
}

void App::cycleView() {
    if (camera_.mode != CameraMode::Pov || povHold_ >= 0.0f) return;
    const RigPose from = shownPose_;
    int v = (static_cast<int>(povView_) + 1) % 3;
    if (v == static_cast<int>(PovView::Alt) && !povSrc_.hasAlt) v = 0;
    povView_ = static_cast<PovView>(v);
    povLookYaw_ = povLookPitch_ = 0.0f;
    beginJump(from);
}

// ------------------------------------------------------------------------------------------------ picker

void App::refreshPicker() {
    std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    speedMeter_.update(list, scene_->time());
    sortByDistance(list);
    const int at = findPov(list, pickerKey_);
    pickerSel_ = at >= 0 ? at : std::clamp(pickerSel_, 0, std::max(0, static_cast<int>(list.size()) - 1));
    if (!list.empty()) pickerKey_ = list[pickerSel_].key;
    pickerList_ = std::move(list);
}

void App::togglePicker() {
    pickerOpen_ = !pickerOpen_;
    audio_.uiCue(pickerOpen_ ? audio::UiCue::PickerOpen : audio::UiCue::PickerClose);
    if (!pickerOpen_) return;
    PovSource cur;
    pickerKey_ = currentSource(cur) ? cur.key : 0;
    pickerSel_ = 0;
    refreshPicker();
}

void App::pickerMove(int delta) {
    if (pickerList_.empty()) return;
    const int was = pickerSel_;
    pickerSel_ = std::clamp(pickerSel_ + delta, 0, static_cast<int>(pickerList_.size()) - 1);
    if (pickerSel_ != was) audio_.uiCue(audio::UiCue::PickerMove);
    pickerKey_ = pickerList_[pickerSel_].key;
}

void App::pickerConfirm() {
    if (pickerList_.empty() || pickerSel_ < 0 || pickerSel_ >= static_cast<int>(pickerList_.size())) return;
    const uint32_t key = pickerList_[pickerSel_].key;
    pickerOpen_ = false;
    jumpToPov(key);
}

bool App::pickerClick(double nx, double ny) {
    if (!pickerOpen_) return false;
    const PickerLayout l = pickerLayout(lastW_, lastH_, static_cast<int>(pickerList_.size()), pickerSel_);
    const int i = pickerRowAt(l, static_cast<int>(pickerList_.size()), static_cast<float>(nx) * static_cast<float>(lastW_),
                              static_cast<float>(ny) * static_cast<float>(lastH_));
    if (i < 0) return false;
    pickerSel_ = i;
    pickerKey_ = pickerList_[i].key;
    pickerConfirm();
    return true;
}

// ------------------------------------------------------------------------------------------------ per frame

void App::updateGameCamera(float dt) {
    rig_.advance(dt);
    const float now = scene_->time();

    // Riding along: follow the source; a round that hit or a destroyed rover leaves the camera holding on the spot.
    if (camera_.mode == CameraMode::Pov && povKey_ != 0) {
        if (povHold_ >= 0.0f) {
            povHold_ += dt;
            // Slow pull back from the impact point while it burns.
            const Vec3 back = normalize(povHoldPose_.eye - povSrc_.position);
            povHoldPose_.eye = povHoldPose_.eye + back * (0.6f * dt);
            povHoldPose_.rot = quatLook(povSrc_.position - povHoldPose_.eye);
            if (povHold_ > kHoldAfterEnd) {
                if (prevView_.valid) {
                    returnToPrevious();
                } else {
                    const RigPose from = shownPose_;
                    camera_.mode = CameraMode::Orbit;
                    camera_.target = povSrc_.position;
                    camera_.distance = 14.0f;
                    beginJump(from);
                }
                povHold_ = -1.0f;
            }
        } else {
            bool alive = false;
            if (povProjectile(povSrc_.kind)) {
                const int idx = povRound_.track(scene_->combat().projectiles(), scene_->combat().time(), dt);
                if (idx >= 0) {
                    alive = true;
                    povSrc_.projectile = idx;
                    povSrc_.position = povRound_.position;
                    povSrc_.velocity = povRound_.velocity;
                    povSrc_.speed = length(povRound_.velocity);
                    povKey_ = povRound_.key;
                } else {
                    povSrc_.position = povRound_.impact;
                    povSrc_.projectile = -1;   // gone: its last position / velocity stand in
                }
            } else {
                std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
                speedMeter_.update(list, now);
                const int i = findPov(list, povKey_);
                if (i >= 0) {
                    povSrc_ = list[i];
                    alive = true;
                } else if (povSrc_.intruder >= 0) {
                    const auto& ins = scene_->intruders();
                    if (povSrc_.intruder < static_cast<int>(ins.size()) && ins[povSrc_.intruder]->life == Intruder::Life::Wreck)
                        povSrc_.position = ins[povSrc_.intruder]->wreckPosition;
                }
            }
            if (!alive) {   // impact / destroyed: hold, looking at the spot from a little behind and above
                RigPose cur = shownValid_ ? shownPose_ : RigPose{};
                povPose(*scene_, povSrc_, povView_, cur, povLookYaw_, povLookPitch_);   // where the camera is now
                Vec3 eye = cur.eye;
                const Vec3 to = povSrc_.position - eye;
                if (length(to) < 4.0f) eye = povSrc_.position - normalize(to.x == 0.0f && to.z == 0.0f ? Vec3{0, 0, 1} : to) * 4.0f;
                eye.y = std::max(eye.y, povSrc_.position.y + 1.5f);
                povHoldPose_ = RigPose::look(eye, povSrc_.position, cur.fovDeg > 1.0f ? cur.fovDeg : 60.0f);
                povHold_ = 0.0f;
                releaseRover();
                beginJump(cur);
                std::printf("POV: %s ended, holding\n", povSrc_.name.c_str());
            }
        }
    }
    if (possessedRover_ >= 0) {
        const auto& ins = scene_->intruders();
        if (possessedRover_ >= static_cast<int>(ins.size()) || ins[possessedRover_]->life != Intruder::Life::Alive) releaseRover();
    }

    // Possessed soldier: Left Alt asked for crouch / stand; the stance cycles by the action edge until it matches.
    const int soldierIdx = possessedSoldierIndex();
    if (soldierIdx >= 0 && crouchWant_ >= 0) {
        const Soldier* s = dynamic_cast<const Soldier*>(sa::controllables(*scene_)[soldierIdx]);
        if (!s || s->stance() == crouchWant_) crouchWant_ = -1;
        else actionEdge_ = true;
    } else if (soldierIdx < 0) {
        crouchWant_ = -1;
    }

    // Game feel: FOV kick on boost / sprint, letterbox during jumps, hit marker, alert vignette.
    PovSource src;
    const bool hasSrc = currentSource(src);
    float kick = 0.0f;
    if (camera_.mode == CameraMode::Free && flyCam_) {
        kick = fovKickTarget(held_.boost, length(fly_.velocity), fly_.speed * FlyCam::kBoost, 10.0f);
    } else if ((possessed_ >= 0 || possessedRover_ >= 0) && hasSrc) {
        float speed = src.speed;
        if (soldierIdx >= 0)
            if (const Soldier* s = dynamic_cast<const Soldier*>(sa::controllables(*scene_)[soldierIdx])) speed = std::fabs(s->speed());
        kick = fovKickTarget(held_.boost, speed, topSpeed(src.kind));
    }
    // v9 M3: the player's soldier sprinting: +6 deg at full sprint speed in every mode (sprint blend x ground speed /
    // sprint speed), eased in and out.
    const Soldier* sprinter = soldierIdx >= 0 ? dynamic_cast<const Soldier*>(sa::controllables(*scene_)[soldierIdx]) : nullptr;
    if (sprinter && camera_.mode == CameraMode::Possess)
        kick = kSprintFovKick * sprinter->motor().sprintBlend() *
               clampf(sprinter->motor().groundSpeed() / sprinter->motor().params.runSpeed, 0.0f, 1.0f);
    fovKick_ = easeTowards(fovKick_, gameMode_ || sprinter ? kick : 0.0f, dt, 0.18f);
    if (std::fabs(fovKick_) < 0.005f && !gameMode_) fovKick_ = 0.0f;
    letterbox_ = gameMode_ ? jumpLetterbox(rig_.progress()) : 0.0f;
    if ((possessed_ >= 0 || possessedRover_ >= 0) && held_.fire) lastShotT_ = now;
    for (const GameEvent& e : scene_->events())
        if ((e.type == GameEventType::EnemyHit || e.type == GameEventType::EnemyDestroyed) && now - lastShotT_ < 4.0f && hitMarkerT_ != now) {
            hitMarkerT_ = now;
            audio_.uiCue(audio::UiCue::HitMarker);   // the player's own round hit
        }
    float alert = 0.0f;
    if (hasSrc && src.kind == PovKind::Rover) alert = designationAlert(*scene_, src.position);
    const float alertWas = alert_;
    alert_ = easeTowards(alert_, alert, dt, 0.15f);
    if (alertWas < 0.3f && alert_ >= 0.3f) audio_.uiCue(audio::UiCue::Alert);   // the designation vignette starts

    // v8: the third-person spring arm / landing shake / aim zoom of a possessed soldier (interactive play and the
    // play_* captures; the scripted v6 captures keep their v6 chase hints).
    if (opt_.mode == AppOptions::Mode::Interactive || playCapture_) updateOpenWorld(dt);
    else tpPoseValid_ = false;
    soldierWeapon(dt);
    if (pickerOpen_) refreshPicker();
}

void App::soldierWeapon(float dt) {
    fireCooldown_ -= dt;
    recoil_ = easeTowards(recoil_, 0.0f, dt, 0.07f);
    if (paused_ || pickerOpen_) return;
    int idx = -1;
    const bool firstPerson = firstPersonSoldier(idx);
    if (!firstPerson) idx = possessedSoldierIndex();   // v8: third person fires too (towards the crosshair)
    if (idx < 0 || camera_.mode != CameraMode::Possess || !held_.fire || fireCooldown_ > 0.0f) return;
    Combat* combat = sa::combat(*scene_);
    if (!combat || !shownValid_) return;
    const RigPose& eye = shownPose_;
    static unsigned rng = 0x9e3779b9u;
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return static_cast<float>((rng >> 8) & 0xFFFF) / 65535.0f - 0.5f; };
    Vec3 muzzle, aim;
    if (firstPerson) {
        // Muzzle at the viewmodel's barrel tip; the round goes where the crosshair points (converges at 40 m).
        muzzle = eye.eye + eye.forward() * (kMuzzleZ - kGunEye.z) + eye.right() * kGunEye.x + eye.up() * kGunEye.y;
        aim = eye.eye + eye.forward() * 40.0f + eye.right() * (rnd() * 0.35f) + eye.up() * (rnd() * 0.35f);
    } else {
        // Third person: from the held rifle's muzzle (or the chest) to where the crosshair ray meets the world;
        // hip fire spreads more than aimed fire.
        const Soldier* sd = dynamic_cast<const Soldier*>(sa::controllables(*scene_)[idx]);
        if (!sd) return;
        const SceneNode* rifle = sd->rig().rifle && sd->rig().rifle->visible ? sd->rig().rifle : nullptr;
        muzzle = rifle ? transformPoint(rifle->world, {0.0f, 0.0f, 0.62f}) : sd->position() + Vec3{0.0f, 1.35f, 0.0f};
        const float spread = aiming_ ? 0.25f : 0.9f;
        aim = aimPoint(eye, 150.0f) + eye.right() * (rnd() * spread) + eye.up() * (rnd() * spread);
    }
    const Vec3 dir = normalize(aim - muzzle);
    combat->fireTracer(muzzle, dir, Combat::kTracerSpeed, idx + 1);
    combat->muzzleFlash(muzzle, dir, 0.16f);
    audio_.gunshot(muzzle);   // v7 WP-L: rifle report (spatial + echoes); the tracer's impact plays a surface tick
    fireCooldown_ = 0.11f;
    recoil_ = 1.0f;
    lastShotT_ = scene_->time();
}

// ------------------------------------------------------------------------------------------------ viewmodel
// v7 WP-P: a detailed first-person carbine built from primitives (receiver, rail, handguard, barrel + muzzle brake,
// magazine, grip, stock, red-dot optic with lenses) held by gloved hands. The scene's leaf list is fixed at load, so
// the parts are the soldier's own leaves that the first-person view hides anyway (head, arms, both rifles), re-shaped
// for this frame only and restored by restoreViewmodel.
namespace {

enum VmMat { kAnod, kPoly, kSteel, kEdge, kRubber, kLens, kGlove, kSleeve, kVmMats };

Material vmMaterial(int m) {
    switch (m) {   // plain (non-procedural) surfaces: world-space patterns would swim on a part that moves with the eye
        case kAnod: return makeMaterial({0.17f, 0.175f, 0.18f}, 0.45f, Surface::Plain, 0.42f, 0.6f);       // anodised alloy
        case kPoly: return makeMaterial({0.38f, 0.32f, 0.235f}, 0.45f, Surface::Plain, 0.62f, 0.0f);       // FDE polymer
        case kSteel: return makeMaterial({0.11f, 0.11f, 0.115f}, 0.5f, Surface::Plain, 0.32f, 0.8f);       // blued steel
        case kEdge: return makeMaterial({0.40f, 0.41f, 0.42f}, 0.45f, Surface::Plain, 0.25f, 0.85f);       // worn edges
        case kRubber: return makeMaterial({0.045f, 0.045f, 0.045f}, 0.45f, Surface::Plain, 0.85f, 0.0f);
        case kLens: return makeMaterial({0.04f, 0.09f, 0.10f}, 0.3f, Surface::Glass, 0.04f, 0.0f, 0.04f);  // coated glass
        case kGlove: return makeMaterial({0.075f, 0.078f, 0.075f}, 0.70f, Surface::Plain, 0.72f, 0.0f);    // black gloves
        default: return makeMaterial({0.27f, 0.29f, 0.21f}, 0.62f, Surface::Plain, 0.88f, 0.0f);         // olive sleeve
    }
}

struct VmPart {
    PrimitiveType prim;
    Vec3 size, rotDeg, pos;
    int mat;
};

// Segment a -> b as a cylinder of diameter d (forearms, fingers).
VmPart vmLimb(const Vec3& a, const Vec3& b, float d, int mat) {
    return {PrimitiveType::Cylinder, {d, d, length(b - a)}, alignZ(b - a), (a + b) * 0.5f, mat};
}

// Parts in priority order (the soldier has enough hidden leaves for all of them; extras are dropped from the end).
const std::vector<VmPart>& viewmodelParts() {
    using P = PrimitiveType;
    static const std::vector<VmPart> parts = [] {
        std::vector<VmPart> v = {
            {P::Cube, {0.050f, 0.062f, 0.29f}, {}, {0.0f, 0.0f, 0.0f}, kAnod},             // upper receiver
            {P::Cube, {0.044f, 0.050f, 0.17f}, {}, {0.0f, -0.052f, -0.005f}, kAnod},       // lower receiver / magwell
            {P::Cylinder, {0.062f, 0.066f, 0.32f}, {}, {0.0f, -0.003f, 0.31f}, kPoly},     // handguard
            {P::Cylinder, {0.019f, 0.019f, 0.25f}, {}, {0.0f, 0.0f, 0.585f}, kSteel},      // barrel
            {P::Cylinder, {0.031f, 0.031f, 0.065f}, {}, {0.0f, 0.0f, 0.737f}, kSteel},     // muzzle brake
            {P::Cube, {0.030f, 0.165f, 0.072f}, {-14, 0, 0}, {0.0f, -0.150f, 0.045f}, kPoly},  // magazine
            {P::Cube, {0.030f, 0.105f, 0.044f}, {20, 0, 0}, {0.0f, -0.112f, -0.098f}, kPoly},  // pistol grip
            {P::Cylinder, {0.030f, 0.030f, 0.15f}, {}, {0.0f, -0.008f, -0.215f}, kAnod},   // buffer tube
            {P::Cube, {0.042f, 0.080f, 0.15f}, {}, {0.0f, -0.030f, -0.33f}, kPoly},        // stock
            {P::Cylinder, {0.038f, 0.038f, 0.10f}, {}, {0.0f, 0.080f, 0.03f}, kSteel},     // optic tube
            {P::Cube, {0.030f, 0.026f, 0.06f}, {}, {0.0f, 0.054f, 0.03f}, kAnod},          // optic mount
            {P::Cylinder, {0.031f, 0.031f, 0.004f}, {}, {0.0f, 0.080f, -0.021f}, kLens},   // rear lens
            {P::Cylinder, {0.031f, 0.031f, 0.004f}, {}, {0.0f, 0.080f, 0.081f}, kLens},    // front lens
            {P::Cube, {0.026f, 0.012f, 0.50f}, {}, {0.0f, 0.037f, 0.10f}, kAnod},          // top rail
            {P::Cube, {0.0025f, 0.0025f, 0.29f}, {}, {0.025f, 0.031f, 0.0f}, kEdge},       // worn edge (top)
            {P::Cube, {0.0025f, 0.0025f, 0.29f}, {}, {0.022f, -0.031f, 0.0f}, kEdge},      // worn edge (bottom)
            {P::Cube, {0.0025f, 0.0025f, 0.50f}, {}, {0.013f, 0.043f, 0.10f}, kEdge},      // rail edge
            {P::Cylinder, {0.068f, 0.072f, 0.012f}, {}, {0.0f, -0.003f, 0.468f}, kEdge},   // handguard end cap
            {P::Cube, {0.050f, 0.030f, 0.32f}, {}, {0.0f, -0.022f, 0.31f}, kPoly},         // handguard flat underside
            {P::Cube, {0.004f, 0.012f, 0.045f}, {}, {0.033f, -0.008f, 0.22f}, kRubber},    // M-LOK slots
            {P::Cube, {0.004f, 0.012f, 0.045f}, {}, {0.033f, -0.008f, 0.31f}, kRubber},
            {P::Cube, {0.004f, 0.012f, 0.045f}, {}, {0.033f, -0.008f, 0.40f}, kRubber},
            {P::Cube, {0.032f, 0.008f, 0.012f}, {}, {0.0f, 0.014f, 0.727f}, kRubber},      // brake port
            {P::Cylinder, {0.033f, 0.033f, 0.008f}, {}, {0.0f, 0.0f, 0.771f}, kEdge},      // crown
            {P::Cube, {0.016f, 0.026f, 0.014f}, {}, {0.0f, 0.054f, 0.335f}, kAnod},        // folded front sight
            {P::Cube, {0.034f, 0.016f, 0.080f}, {-14, 0, 0}, {0.0f, -0.232f, 0.065f}, kRubber},  // magazine floor plate
            {P::Cube, {0.046f, 0.092f, 0.022f}, {}, {0.0f, -0.032f, -0.416f}, kRubber},   // butt pad
            {P::Cube, {0.038f, 0.020f, 0.11f}, {}, {0.0f, 0.018f, -0.32f}, kPoly},         // cheek rest
            {P::Cube, {0.060f, 0.012f, 0.025f}, {}, {0.0f, 0.026f, -0.155f}, kAnod},       // charging handle
            {P::Cube, {0.010f, 0.008f, 0.075f}, {}, {0.0f, -0.083f, -0.030f}, kAnod},      // trigger guard
            {P::Cube, {0.006f, 0.030f, 0.012f}, {}, {0.025f, -0.036f, -0.025f}, kEdge},    // bolt catch
            {P::Cylinder, {0.014f, 0.014f, 0.012f}, {90, 0, 0}, {0.0f, 0.104f, 0.03f}, kEdge},    // elevation turret
            {P::Cylinder, {0.014f, 0.014f, 0.012f}, {0, 90, 0}, {0.024f, 0.080f, 0.03f}, kEdge},   // windage turret
        };
        // Gloved hands: right on the grip (index finger along the receiver), left under the handguard; sleeves.
        const std::vector<VmPart> hands = {
            {P::Sphere, {0.072f, 0.10f, 0.115f}, {20, 0, 0}, {-0.010f, -0.108f, -0.108f}, kGlove},   // right hand
            {P::Cube, {0.052f, 0.085f, 0.032f}, {20, 0, 0}, {0.0f, -0.118f, -0.062f}, kGlove},       // right fingers
            vmLimb({0.026f, -0.060f, -0.110f}, {0.028f, -0.050f, -0.055f}, 0.021f, kGlove),          // right thumb
            vmLimb({0.026f, -0.078f, -0.075f}, {0.027f, -0.070f, 0.000f}, 0.018f, kGlove),           // index finger
            {P::Sphere, {0.088f, 0.078f, 0.125f}, {}, {0.014f, -0.060f, 0.33f}, kGlove},             // left hand
            {P::Cube, {0.024f, 0.058f, 0.105f}, {0, 0, -8}, {0.039f, -0.014f, 0.33f}, kGlove},       // left fingers
            vmLimb({-0.012f, -0.135f, -0.150f}, {-0.105f, -0.310f, -0.430f}, 0.088f, kSleeve),        // right forearm
            vmLimb({0.020f, -0.085f, 0.285f}, {0.175f, -0.215f, 0.030f}, 0.090f, kSleeve),            // left forearm
            vmLimb({-0.008f, -0.128f, -0.135f}, {-0.030f, -0.170f, -0.195f}, 0.080f, kGlove),         // right cuff
            vmLimb({0.022f, -0.082f, 0.290f}, {0.060f, -0.115f, 0.228f}, 0.084f, kGlove),             // left cuff
            {P::Sphere, {0.105f, 0.105f, 0.105f}, {}, {0.175f, -0.215f, 0.030f}, kSleeve},             // left elbow
            vmLimb({0.175f, -0.215f, 0.030f}, {0.340f, -0.200f, -0.420f}, 0.105f, kSleeve),           // left upper arm
        };
        // The hands are more important than the small rifle details: put them right after the main rifle parts.
        v.insert(v.begin() + 13, hands.begin(), hands.end());
        return v;
    }();
    return parts;
}

}  // namespace

void App::applyViewmodel(int index, const RigPose& eye) {
    viewmodelHidden_.clear();
    const std::vector<Controllable*> list = sa::controllables(*scene_);
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    const Soldier* s = dynamic_cast<const Soldier*>(list[index]);
    if (!s) return;
    const SoldierRig& r = s->rig();
    auto setVis = [&](SceneNode* n, bool on) {
        if (!n || n->visible == on) return;
        viewmodelHidden_.push_back({n, n->visible});
        n->visible = on;
    };
    // Hide what would sit on the lens: head, helmet, neck, the arms and hands, both rifles. Those leaves form the pool
    // the viewmodel parts are made of.
    std::vector<SceneNode*> stack, pool;
    if (r.head && r.head->parent) stack.push_back(r.head->parent);
    for (SceneNode* g : {r.rifleBack, r.rifle, r.wrench})
        if (g) stack.push_back(g);
    for (int i = 0; i < 2; ++i)
        if (r.shoulder[i]) stack.push_back(r.shoulder[i]);
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isLeaf()) {
            setVis(n, false);
            if (n->material.opacity >= 1.0f && n->material.lightIntensity <= 0.0f) pool.push_back(n);
        }
        for (auto& c : n->children) stack.push_back(c.get());
    }
    // Gun pose: idle breathing sway, walk bob and roll from the gait, recoil kick.
    const float t = scene_->time();
    const float k = clampf(std::fabs(s->speed()) / 4.0f, 0.0f, 1.0f), ph = s->gaitPhase();
    const float swayX = 0.0035f * std::sin(t * 0.9f) + 0.012f * std::sin(ph) * k;
    const float swayY = 0.0025f * std::sin(t * 1.7f + 0.6f) + 0.010f * std::fabs(std::cos(ph)) * k - 0.006f * k;
    const float yaw = 0.5f * std::sin(t * 0.7f) + 1.2f * std::sin(ph) * k, roll = 1.5f * std::sin(ph) * k;
    const Mat4 gun = eye.world() * translate({kGunEye.x + swayX, kGunEye.y + swayY, kGunEye.z + 0.045f * recoil_}) *
                     rotateX(radians(0.8f + 4.0f * recoil_)) * rotateY(radians(-2.0f + yaw)) * rotateZ(radians(roll)) *
                     rotateY(3.14159265f);
    Material mats[kVmMats];
    for (int m = 0; m < kVmMats; ++m) mats[m] = vmMaterial(m);
    const std::vector<VmPart>& parts = viewmodelParts();
    for (size_t i = 0; i < parts.size() && i < pool.size(); ++i) {
        SceneNode* n = pool[i];
        Hidden h{n, n->visible, true, n->primitive, n->material, n->world};
        viewmodelHidden_.push_back(h);
        const VmPart& p = parts[i];
        n->primitive = p.prim;
        n->material = mats[p.mat];
        n->world = gun * Transform{p.pos, p.rotDeg, p.size}.matrix();
        n->visible = true;
    }
}

void App::restoreViewmodel() {
    for (auto it = viewmodelHidden_.rbegin(); it != viewmodelHidden_.rend(); ++it) {
        if (it->borrowed) {
            it->node->primitive = it->primitive;
            it->node->material = it->material;
            it->node->world = it->world;
        }
        it->node->visible = it->visible;
    }
    viewmodelHidden_.clear();
}

// ------------------------------------------------------------------------------------------------ v7 figures

// --capture DIR --only game_a,game_b: the v7 POV / game-mode figures from one simulated run (night segment, then
// day). Shots: game_rover_bumper, game_rover_chase, game_uav2_gimbal, game_missile_follow, game_missile_impact,
// game_picker, game_jump_blend, game_soldier_fp, game_soldier_fire, game_heli_cockpit, game_tank_commander,
// game_free_fly. Default size 1280 x 720 (--size overrides); quality = --quality or the GPU's preset.
int App::captureGame() {
    const std::string dir = opt_.outPath.empty() ? "captures" : opt_.outPath;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int width = opt_.sizeGiven ? opt_.width : 1280, height = opt_.sizeGiven ? opt_.height : 720;
    constexpr float kDt = 1.0f / 60.0f;
    settings_ = RenderSettings{};
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    std::printf("game capture: %dx%d, quality %s\n", width, height, qualityName(settings_.quality));
    showHud_ = true;
    showFeed_ = false;
    RenderTarget target;
    if (!target.create(width, height, 0, false)) return 1;
    int written = 0, failed = 0;
    auto wanted = [&](const char* name) { return opt_.only.find("," + std::string(name) + ",") != std::string::npos; };
    auto run = [&](float seconds) {
        for (float t = 0.0f; t < seconds; t += kDt) {
            step(kDt);
            updateGameCamera(kDt);
        }
    };
    auto shot = [&](const char* name) {
        if (!wanted(name)) return;
        renderer_->resetHistory();
        const int frames = qualityProfile(settings_.quality).taa ? 8 : 1;
        for (int f = 0; f < frames; ++f) {
            target.bind();
            renderFrame(width, height, true);
        }
        std::vector<uint8_t> rgb;
        target.read(rgb);
        const std::string path = dir + "/" + name + ".png";
        const bool ok = writePng(path, width, height, rgb);
        PovSource src;
        const bool has = currentSource(src);
        std::printf("%s %s (t = %.1f s, cam %s, %s, eye %.1f %.1f %.1f)\n", ok ? "wrote" : "FAILED", path.c_str(), scene_->time(),
                    cameraModeName(camera_.mode), has ? src.name.c_str() : "-", shownPose_.eye.x, shownPose_.eye.y, shownPose_.eye.z);
        ok ? ++written : ++failed;
    };
    auto findKind = [&](PovKind k, bool outdoors = false) -> uint32_t {
        for (const PovSource& p : listPovSources(*scene_, shownPose_.eye)) {
            if (p.kind != k) continue;
            if (outdoors && p.position.y < -0.5f) continue;
            return p.key;
        }
        return 0;
    };
    auto reset = [&]() {
        held_ = ControlInput{};
        releaseRover();
        possess(-1);
        camera_ = Camera{};
        flyCam_ = false;
        povHold_ = -1.0f;
        pickerOpen_ = false;
        rig_.cancel();
        fovKick_ = letterbox_ = 0.0f;
    };

    // ---- night: rovers, the wing UAV, a round in flight
    run(4.0f);
    for (float t = 0.0f; t < 90.0f && !sa::anyAlive(*scene_); t += 0.5f) run(0.5f);
    run(6.0f);
    if (const uint32_t rover = findKind(PovKind::Rover)) {
        reset();
        gameMode_ = true;
        // The mast camera (0.6 m): the hood camera (0.42 m) sees only grass blades in the meadow (WP-CAP review).
        jumpToPov(rover, static_cast<int>(PovView::Alt), false);
        takeControl();
        held_.forward = 1.0f;
        run(1.2f);
        shot("game_rover_bumper");
        cycleView();
        run(CameraRig::kBlendTime + 0.1f);
        shot("game_rover_chase");
    } else {
        std::printf("game capture: no intruder rover on the field, rover shots skipped\n");
    }
    reset();
    gameMode_ = false;
    if (const uint32_t uav2 = findKind(PovKind::Uav)) {
        uint32_t wing = 0;
        for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
            if (p.kind == PovKind::Uav && p.uav == 1) wing = p.key;
        jumpToPov(wing ? wing : uav2, static_cast<int>(PovView::First), false);
        run(0.2f);
        shot("game_uav2_gimbal");
    }
    if (wanted("game_missile_follow") || wanted("game_missile_impact")) {
        reset();
        uint32_t round = 0;
        for (float t = 0.0f; t < 30.0f && findKind(PovKind::Missile); t += kDt) run(kDt);   // a fresh launch
        for (float t = 0.0f; t < 120.0f && !round; t += kDt) {
            run(kDt);
            round = findKind(PovKind::Missile);
        }
        if (round) {
            gameMode_ = true;
            // Nose camera: the chase view looks through the motor's glow, which blooms over the whole frame.
            jumpToPov(round, static_cast<int>(PovView::First), false);
            run(0.1f);
            shot("game_missile_follow");
            for (float t = 0.0f; t < 8.0f && povHold_ < 0.0f; t += kDt) run(kDt);
            run(0.35f);
            shot("game_missile_impact");
        } else {
            std::printf("game capture: no UAV round within 120 s, missile shots skipped\n");
        }
    }
    reset();
    gameMode_ = false;
    togglePicker();
    for (int i = 0; i < 3; ++i) pickerMove(1);
    run(0.1f);
    shot("game_picker");
    pickerOpen_ = false;

    // ---- day: soldiers, vehicles, free camera
    if (DayCycle* clock = sa::dayCycle(*scene_)) {
        clock->setAuto(false);
        clock->setHour(10.5f);
    }
    run(4.0f);
    reset();
    gameMode_ = true;
    if (const uint32_t sold = findKind(PovKind::Soldier, true)) {
        run(0.1f);
        target.bind();
        renderFrame(width, height, false);   // the orbit view is the shown pose the jump blends from (even when the
                                             // shots before it were not requested and nothing has been rendered yet)
        jumpToPov(sold, -1, true);   // from the orbit view: mid-jump frame (blend + letterbox)
        run(0.22f);
        shot("game_jump_blend");
        run(CameraRig::kBlendTime);
        takeControl();
        held_.forward = 1.0f;
        run(1.6f);
        shot("game_soldier_fp");
        held_.forward = 0.0f;
        held_.fire = true;
        run(0.3f);
        shot("game_soldier_fire");
        held_.fire = false;
    }
    reset();
    gameMode_ = false;
    if (const uint32_t heli = findKind(PovKind::Helicopter)) {
        jumpToPov(heli, static_cast<int>(PovView::First), false);
        run(0.1f);
        shot("game_heli_cockpit");
    }
    reset();
    if (const uint32_t tank = findKind(PovKind::Tank)) {
        jumpToPov(tank, static_cast<int>(PovView::Alt), false);
        run(0.1f);
        shot("game_tank_commander");
    }
    reset();
    camera_.mode = CameraMode::Orbit;
    run(0.05f);
    renderFrame(width, height, false);   // a shown pose to start from
    enterFreeCam();
    camera_.freePos = {14.0f, 7.0f, 16.0f};
    camera_.freeYawDeg = -140.0f;
    camera_.freePitchDeg = -16.0f;
    rig_.cancel();
    run(0.1f);
    shot("game_free_fly");
    reset();
    gameMode_ = false;
    std::printf("game capture: %d written, %d failed\n", written, failed);
    return failed ? 1 : 0;
}

}  // namespace ts
