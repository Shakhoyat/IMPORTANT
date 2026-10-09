// v8 player control: click a soldier (or any moving object) to select it, Enter / double-click to take control in first
// person, V to switch between first and third person (spring-arm camera that never clips into walls, the ground or the
// character), camera-relative W A S D in third person, strafe + mouse look in first person, sprint, jump, crouch, rifle
// fire in both views, and the selection / control HUD. The pieces under it are the v7.1 WP-T building blocks
// (ThirdPersonCam, thirdPersonSoldierInput, stepFoot, sphereCast); this file wires them into the App.
//
// Extensibility: everything here works on PovSource (scene/Pov.h), so any new Controllable automatically becomes
// selectable and controllable; only soldiers get the on-foot camera (first / third person), vehicles use their chase
// hints. New on-foot entities only need to derive from Soldier-like Controllables and be listed by listPovSources.
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "app/App.h"
#include "app/HeadlessGL.h"
#include "rendering/ImageWriter.h"
#include "rendering/RenderTarget.h"

namespace ts {

namespace {

constexpr float kDoubleClickSeconds = 0.45f;
constexpr float kAimFovFirstPerson = 44.0f;   // right mouse in first person (iron-sight zoom)

const Rgba kSel{0.35f, 1.0f, 0.55f, 1.0f};
const Rgba kSelDim{0.35f, 1.0f, 0.55f, 0.55f};
const Rgba kHover{0.92f, 0.95f, 1.0f, 0.85f};
const Rgba kHint{0.86f, 0.9f, 0.95f, 0.92f};
const Rgba kBack{0.0f, 0.0f, 0.0f, 0.5f};
const Rgba kTip{0.62f, 0.68f, 0.75f, 0.85f};

const Soldier* soldierOf(const Scene& scene, int index) {
    const std::vector<Controllable*> c = sa::controllables(scene);
    if (index < 0 || index >= static_cast<int>(c.size())) return nullptr;
    return dynamic_cast<const Soldier*>(c[index]);
}

void leavesOf(SceneNode& n, std::vector<SceneNode*>& out) {
    if (n.isLeaf()) out.push_back(&n);
    for (auto& c : n.children) leavesOf(*c, out);
}

// Screen position (pixels, origin top-left) of a world point; false behind the camera.
bool project(const CameraView& cam, int w, int h, const Vec3& p, float& x, float& y) {
    const Vec4 c = cam.proj * cam.view * Vec4{p.x, p.y, p.z, 1.0f};
    if (c.w <= 0.05f) return false;
    x = (c.x / c.w * 0.5f + 0.5f) * static_cast<float>(w);
    y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * static_cast<float>(h);
    return true;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ queries

bool App::controllingSoldier() const { return camera_.mode == CameraMode::Possess && possessedSoldierIndex() >= 0; }

bool App::viewThirdPerson() const { return camera_.mode == CameraMode::Possess && !camera_.possessSight; }

std::vector<PovSource> App::povSources() const { return listPovSources(*scene_, shownPose_.eye); }

int App::pickSourceAt(double nx, double ny, std::vector<PovSource>& list) const {
    if (!shownValid_) return -1;
    list = listPovSources(*scene_, shownPose_.eye);
    const Mat4 proj = perspective(radians(shownPose_.fovDeg + fovKick_), lastAspect_, shownPose_.nearZ, 600.0f);
    Vec3 o, d;
    pixelRay(inverse(proj * shownPose_.view()), static_cast<float>(nx) * static_cast<float>(lastW_),
             static_cast<float>(ny) * static_cast<float>(lastH_), lastW_, lastH_, o, d);
    return pickPov(list, o, d, 2.5f);
}

bool App::selectedSource(PovSource& out) const {
    if (selKey_ == 0) return false;
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    const int i = findPov(list, selKey_);
    if (i < 0) return false;
    out = list[i];
    return true;
}

// ------------------------------------------------------------------------------------------------ selection

void App::updateHover(double nx, double ny) {
    std::vector<PovSource> list;
    const int i = pickSourceAt(nx, ny, list);
    hoverKey_ = i >= 0 ? list[i].key : 0;
}

void App::selectAt(double nx, double ny) {
    std::vector<PovSource> list;
    const int i = pickSourceAt(nx, ny, list);
    const float now = static_cast<float>(nowSeconds());
    if (i < 0) {   // a click on empty ground clears the selection
        if (selKey_ != 0) audio_.uiCue(audio::UiCue::PickerClose);
        selKey_ = 0;
        return;
    }
    const uint32_t key = list[i].key;
    if (key == selKey_ && now - selClickT_ < kDoubleClickSeconds) {
        controlSelected();   // double click: take control at once
    } else {
        if (key != selKey_) audio_.uiCue(audio::UiCue::PickerMove);
        selKey_ = key;
        std::printf("Selected: %s (%s)  -- Enter or double-click to take control\n", list[i].name.c_str(), povKindName(list[i].kind));
    }
    selClickT_ = now;
}

bool App::controlSource(int sourceIndex) {
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(list.size())) return false;
    const PovSource& src = list[sourceIndex];
    if (!src.canControl) return jumpToPov(src.key);   // autonomous: ride along / spectate
    const RigPose from = shownPose_;
    const ViewMemo before = memo();
    if (src.controllable >= 0) {
        possess(src.controllable);
        if (possessed_ != src.controllable) return false;
        // Soldiers: through their eyes (V switches to third person); vehicles and UAVs: their chase view.
        const bool soldier = src.kind == PovKind::Soldier;
        camera_.possessSight = soldier;
        thirdPerson_ = !soldier;
        tp_.invalidate();
        tpPoseValid_ = false;
        alignYaw_ = 0.0f;
    } else if (src.intruder >= 0) {
        if (!jumpToPov(src.key, static_cast<int>(PovView::Chase), false)) return false;
        takeControl();
    }
    prevView_ = before;
    beginJump(from);
    audio_.uiCue(audio::UiCue::Possess);
    std::printf("Control: %s (%s)\n", src.name.c_str(), camera_.possessSight ? "first person" : "third person");
    return true;
}

void App::controlSelected() {
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    const int i = findPov(list, selKey_);
    if (i < 0) {
        selKey_ = 0;
        return;
    }
    controlSource(i);
}

bool App::possessRandomSoldier() {
    const Garrison* g = sa::garrison(*scene_);
    if (!g || g->count() == 0) return false;
    randSeed_ = randSeed_ * 1664525u + 1013904223u;
    const int start = static_cast<int>((randSeed_ >> 8) % static_cast<uint32_t>(g->count()));
    for (int pass = 0; pass < 2; ++pass)   // outdoors first: the underground ones are dark and boxed in
        for (int k = 0; k < g->count(); ++k) {
            const int u = (start + k) % g->count();
            const Soldier* s = g->unit(u);
            if (!s || (pass == 0 && s->underground())) continue;
            const int index = sa::soldierIndex(*scene_, u);
            if (index < 0) continue;
            const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i].controllable == index) {
                    selKey_ = list[i].key;
                    return controlSource(static_cast<int>(i));
                }
        }
    return false;
}

// Esc: give control back (the soldier resumes its duty), leave a ride-along, else clear the selection.
// Returns false when there was nothing to leave (the caller may then quit).
bool App::releaseControl() {
    if (possessedRover_ >= 0 || camera_.mode == CameraMode::Possess) {
        const RigPose from = shownPose_;
        audio_.uiCue(audio::UiCue::Release);
        if (possessedRover_ >= 0) releaseRover();
        if (camera_.mode == CameraMode::Possess) {
            const Vec3 at = possessed_ >= 0 ? sa::controllables(*scene_)[possessed_]->position() : camera_.target;
            possess(-1);
            if (camera_.mode == CameraMode::Orbit || camera_.mode == CameraMode::Pov) {   // look at where we were
                camera_.mode = CameraMode::Orbit;
                camera_.target = at + Vec3{0.0f, 1.0f, 0.0f};
                camera_.distance = std::min(camera_.distance, 16.0f);
            }
        } else if (camera_.mode == CameraMode::Pov) {
            camera_.mode = CameraMode::Orbit;
            camera_.target = povSrc_.position;
            camera_.distance = 14.0f;
        }
        aiming_ = false;
        beginJump(from);
        return true;
    }
    if (camera_.mode == CameraMode::Pov) {
        const RigPose from = shownPose_;
        camera_.mode = CameraMode::Orbit;
        camera_.target = povSrc_.position;
        camera_.distance = 14.0f;
        beginJump(from);
        return true;
    }
    if (selKey_ != 0) {
        selKey_ = 0;
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ first / third person

void App::setViewThirdPerson(bool third) {
    if (camera_.mode != CameraMode::Possess || possessed_ < 0) return;
    const RigPose from = shownPose_;
    thirdPerson_ = third;
    if (const Soldier* s = soldierOf(*scene_, possessedSoldierIndex())) {
        if (third) {
            // The arm starts behind the soldier looking where the eyes looked.
            const Vec3 f = shownValid_ ? from.forward() : Vec3{std::sin(radians(s->yawDeg())), 0.0f, std::cos(radians(s->yawDeg()))};
            tp_.reset(s->position() + Vec3{0.0f, 1.45f, 0.0f}, degrees(std::atan2(f.x, f.z)),
                      clampf(degrees(std::asin(clampf(f.y, -1.0f, 1.0f))) - 8.0f, -45.0f, 30.0f));
            tpKey_ = static_cast<uint32_t>(possessed_) + 1u;
            tpPoseValid_ = false;
        } else {
            // First person looks along the camera's heading: turn the body there over the next step.
            alignYaw_ = angleDiffDeg(tp_.valid() ? tp_.yawDeg : s->yawDeg(), s->yawDeg());
        }
    }
    camera_.possessSight = !third;
    possessAlt_ = false;
    beginJump(from);
}

// Distance along unit 'd' from 'o' to an oriented box inflated by r (slab test in the box frame); -1 = miss, or the
// origin already inside (the subject's own volume, as sphereCast).
float App::NearBox::cast(const Vec3& o, const Vec3& d, float r) const {
    const Vec3 rel = o - c;
    float t0 = 0.0f, t1 = 1e9f;
    bool inside = true;
    for (int k = 0; k < 3; ++k) {
        const float e = h[k] + r, oo = dot(rel, u[k]), dd = dot(d, u[k]);
        if (std::fabs(oo) > e) inside = false;
        if (std::fabs(dd) < 1e-7f) {
            if (std::fabs(oo) > e) return -1.0f;
            continue;
        }
        float a = (-e - oo) / dd, b = (e - oo) / dd;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return -1.0f;
    }
    return inside ? -1.0f : t0;
}

float App::armCast(const Vec3& o, const Vec3& d, float len, float r) const {
    float t = sphereCast(sa::walkSpace(*scene_), {}, o, d, len, r, tpFeetY_);   // ground, floors, ceilings, walls, pines
    for (const NearBox& b : tpBoxes_) {
        const float h = b.cast(o, d, r);
        if (h >= 0.0f && h < t) t = h;
    }
    return t;
}

// Oriented boxes the spring arm tests this frame: the static solid parts near the subject (exact orientation: the
// rotated HESCO cells' axis-aligned bounds would swallow the shoulder point), the parts of nearby vehicles /
// installations (not soldiers: the arm passes through people), closed doors.
void App::gatherNearSolids(const Vec3& centre, float radius) {
    tpBoxes_.clear();
    auto add = [&](const SceneNode& n) {
        if (!n.visible || n.material.opacity < 0.5f || n.primitive == PrimitiveType::None ||
            n.primitive == PrimitiveType::TerrainDisc || n.primitive == PrimitiveType::BeamCone)
            return;
        NearBox b;
        b.c = transformPoint(n.world, {0.0f, 0.0f, 0.0f});
        if (std::fabs(b.c.x - centre.x) > radius + 4.0f || std::fabs(b.c.z - centre.z) > radius + 4.0f) return;
        for (int k = 0; k < 3; ++k) {
            const Vec3 axis = transformDirection(n.world, {k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f});
            const float len = length(axis);
            b.u[k] = len > 1e-6f ? axis * (1.0f / len) : Vec3{k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f};
            b.h[k] = 0.5f * len;
        }
        if (std::min(b.h[0], std::min(b.h[1], b.h[2])) * 2.0f < 0.04f &&
            std::max(b.h[0], std::max(b.h[1], b.h[2])) * 2.0f > 0.5f)
            return;   // wires, antennas, cables: the arm passes them
        const float reach = std::sqrt(b.h[0] * b.h[0] + b.h[1] * b.h[1] + b.h[2] * b.h[2]);
        if (std::hypot(b.c.x - centre.x, b.c.z - centre.z) > radius + reach) return;
        tpBoxes_.push_back(b);
    };
    for (const SceneNode* n : scene_->leaves())
        if (n->obstacle && !n->dynamic) add(*n);
    for (Controllable* c : sa::controllables(*scene_)) {
        if (!c || dynamic_cast<const Soldier*>(c) || !c->root()) continue;
        for (const Box& bx : c->dynamicObstacles()) {
            NearBox b;
            b.c = (bx.min + bx.max) * 0.5f;
            b.u[0] = {1, 0, 0};
            b.u[1] = {0, 1, 0};
            b.u[2] = {0, 0, 1};
            const Vec3 hh = (bx.max - bx.min) * 0.5f;
            b.h[0] = hh.x;
            b.h[1] = hh.y;
            b.h[2] = hh.z;
            tpBoxes_.push_back(b);
        }
        if (!c->moves() || length(c->position() - centre) > radius + 6.0f) continue;
        std::vector<SceneNode*> parts;
        leavesOf(*c->root(), parts);
        for (const SceneNode* n : parts) add(*n);
    }
}

RigPose App::thirdPersonTarget(float dt) {
    const int si = possessedSoldierIndex();
    const Soldier* s = soldierOf(*scene_, si);
    if (!s) return shownPose_;
    const Vec3 p = s->position();
    // v9 M3: the pivot rides on the drawn feet (smooth over steps) and sinks with the motor's crouch blend.
    const float crouch = 0.42f * s->motor().crouchBlend();
    const Vec3 pivot = Vec3{p.x, s->drawnFeetY(), p.z} + Vec3{0.0f, 1.45f - crouch, 0.0f};
    const uint32_t key = static_cast<uint32_t>(si) + 1u;
    if (tpKey_ != key || !tp_.valid()) {
        tp_.reset(pivot, s->yawDeg(), -10.0f);
        tpKey_ = key;
    }
    ThirdPersonParams params = ThirdPersonParams::soldier();
    if (const WalkSpace* ws = sa::walkSpace(*scene_); ws && ws->ceilingHeight(p.x, p.z, p.y) < 1e8f) {
        params.arm = 1.9f;            // indoors (bunker rooms, the barracks): a shorter arm, closer shoulder
        params.shoulder = 0.45f;
        params.maxPitch = 30.0f;
    }
    tp_.look(tpLookYaw_, tpLookPitch_, params);
    tpLookYaw_ = tpLookPitch_ = 0.0f;
    tpFeetY_ = p.y;
    gatherNearSolids(pivot, params.arm + 2.0f);
    const ArmCast cast = [this](const Vec3& o, const Vec3& d, float len, float r) { return armCast(o, d, len, r); };
    tpPose_ = tp_.update(pivot, dt, aiming_, params, cast);
    tpPoseValid_ = true;
    return tpPose_;
}

void App::shapePlayerInput(ControlInput& in, float dt) {
    const Soldier* s = soldierOf(*scene_, possessedSoldierIndex());
    if (!s) return;
    in.jump = jumpEdge_;
    jumpEdge_ = false;
    const float fwd = in.forward, right = in.turn;   // W - S, D - A as sampled by the loop
    if (camera_.possessSight) {
        // First person: A / D strafe, the mouse (or the arrows) turns.
        in.strafe = right;
        in.turn = 0.0f;
        if (alignYaw_ != 0.0f) {
            in.aimYawDelta -= alignYaw_;   // Soldier: yaw -= aimYawDelta
            alignYaw_ = 0.0f;
        }
    } else {
        // Third person: movement relative to the camera; the body turns into the walk (or faces the aim).
        const float pitchDelta = in.aimPitchDelta;
        in = thirdPersonSoldierInput(in, fwd, right, tp_.yawDeg, s->yawDeg(), aiming_, dt);
        in.aimPitchDelta = aiming_ ? pitchDelta : 0.0f;
    }
}

void App::updateOpenWorld(float dt) {
    const Soldier* s = soldierOf(*scene_, possessedSoldierIndex());
    if (!s || camera_.mode != CameraMode::Possess) {
        wasAirborne_ = false;
        feelShake_ = 0.0f;
        return;
    }
    if (wasAirborne_ && !s->airborne()) feelShake_ = std::max(feelShake_, clampf(s->lastLandSpeed() / 9.0f, 0.1f, 0.6f));
    wasAirborne_ = s->airborne();
    feelShake_ = easeTowards(feelShake_, 0.0f, dt, 0.12f);
    aimBlend_ = easeTowards(aimBlend_, aiming_ && camera_.possessSight ? 1.0f : 0.0f, dt, 0.08f);
    if (!camera_.possessSight) thirdPersonTarget(dt);
}

// Where the crosshair ray first hits a solid, the ground or a floor (up to maxDist).
Vec3 App::aimPoint(const RigPose& eye, float maxDist) const {
    const Vec3 o = eye.eye, d = eye.forward();
    const WalkSpace* ws = sa::walkSpace(*scene_);
    float hit = maxDist;
    const Vec3 end = o + d * maxDist;
    const Box seg{{std::min(o.x, end.x), std::min(o.y, end.y), std::min(o.z, end.z)},
                  {std::max(o.x, end.x), std::max(o.y, end.y), std::max(o.z, end.z)}};
    for (const Box& b : scene_->staticSolids()) {
        if (b.max.x < seg.min.x || b.min.x > seg.max.x || b.max.y < seg.min.y || b.min.y > seg.max.y ||
            b.max.z < seg.min.z || b.min.z > seg.max.z)
            continue;
        // slab test for the entry distance
        float t0 = 0.0f, t1 = hit;
        const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, mn[3] = {b.min.x, b.min.y, b.min.z},
                    mx[3] = {b.max.x, b.max.y, b.max.z};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            if (std::fabs(dd[k]) < 1e-6f) {
                ok = oo[k] >= mn[k] && oo[k] <= mx[k];
                continue;
            }
            float a = (mn[k] - oo[k]) / dd[k], c = (mx[k] - oo[k]) / dd[k];
            if (a > c) std::swap(a, c);
            t0 = std::max(t0, a);
            t1 = std::min(t1, c);
            ok = t0 <= t1;
        }
        if (ok && t0 > 0.3f) hit = std::min(hit, t0);   // > 0.3 m: not the box the camera / shoulder is in
    }
    for (float t = 0.5f; t < hit; t += 0.25f) {   // ground / floors
        const Vec3 q = o + d * t;
        if (q.y <= groundHeight(ws, q.x, q.z, q.y + 0.3f)) {
            hit = t;
            break;
        }
    }
    return o + d * hit;
}

// ------------------------------------------------------------------------------------------------ HUD

void App::drawPlayHud(const CameraView& cam, int w, int h) {
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f, W = static_cast<float>(w), H = static_cast<float>(h);
    const std::vector<PovSource> list = listPovSources(*scene_, cam.position);

    // Brackets + label around a source (selected: green with its status and the control prompt; hovered: white).
    auto mark = [&](const PovSource& src, bool selected) {
        float cx, cy;
        if (!project(cam, w, h, src.centre, cx, cy)) return;
        const float dist = std::max(length(src.centre - cam.position), 0.5f);
        const float f = static_cast<float>(h) * 0.5f / std::tan(radians(fovFromProjection(cam.proj) * 0.5f));
        const float r = clampf(src.radius * f / dist, 14.0f * s, 260.0f * s);
        const Rgba c = selected ? kSel : kHover;
        hud.brackets(cx - r, cy - r, 2 * r, 2 * r, std::max(6.0f * s, r * 0.35f), c);
        char line[200];
        std::snprintf(line, sizeof(line), "%s   %.0f M", src.name.c_str(), dist);
        const float tx = cx - r, ty = cy - r - 22 * s;
        hud.rect(tx - 4 * s, ty - 3 * s, hud.textWidth(line, 14 * s) + 8 * s, 20 * s, kBack);
        hud.text(tx, ty, line, 14 * s, c);
        if (selected && src.controllable >= 0) {
            const std::string st = sa::controllables(*scene_)[src.controllable]->status();
            const std::string shortSt = st.size() > 70 ? st.substr(0, 70) : st;
            hud.text(tx, cy + r + 6 * s, shortSt, 11 * s, kSelDim);
        }
    };

    if (camera_.mode != CameraMode::Possess && possessedRover_ < 0) {
        PovSource sel;
        const bool hasSel = selectedSource(sel);
        if (hoverKey_ != 0 && (!hasSel || hoverKey_ != sel.key)) {
            const int i = findPov(list, hoverKey_);
            if (i >= 0) mark(list[i], false);
        }
        if (hasSel) {
            mark(sel, true);
            const char* what = sel.kind == PovKind::Soldier ? "TAKE CONTROL (FIRST PERSON)" : sel.canControl ? "TAKE CONTROL" : "RIDE ALONG";
            char prompt[200];
            std::snprintf(prompt, sizeof(prompt), "%s  -  ENTER or DOUBLE-CLICK: %s   ESC: clear", sel.name.c_str(), what);
            const float pw = hud.textWidth(prompt, 15 * s) + 28 * s;
            hud.roundRect(W * 0.5f - pw * 0.5f, H - 132 * s, pw, 32 * s, 6 * s, Rgba{0.0f, 0.0f, 0.0f, 0.6f});
            hud.text(W * 0.5f - pw * 0.5f + 14 * s, H - 124 * s, prompt, 15 * s, kSel);
        } else if (!gameMode_ && !pickerOpen_) {
            const char* tip = "CLICK a soldier or UAV to select it   DOUBLE-CLICK / ENTER to take control   F4 list";
            hud.text(W * 0.5f - hud.textWidth(tip, 12 * s) * 0.5f, H - 120 * s, tip, 12 * s, kTip);
        }
        return;
    }

    // Controlling a soldier: crosshair (third person) and the key strip.
    if (controllingSoldier()) {
        if (!camera_.possessSight) {
            const float cx = W * 0.5f, cy = H * 0.5f;
            const Soldier* sd = soldierOf(*scene_, possessedSoldierIndex());
            const float spread = (sd ? std::min(std::fabs(sd->speed()) / 4.5f, 1.0f) : 0.0f) * 10.0f * s + (aiming_ ? 3.0f : 7.0f) * s;
            const Rgba c{1.0f, 1.0f, 1.0f, aiming_ ? 0.95f : 0.7f};
            hud.disc(cx, cy, 1.6f * s, c);
            hud.line(cx - spread - 7 * s, cy, cx - spread, cy, c);
            hud.line(cx + spread, cy, cx + spread + 7 * s, cy, c);
            hud.line(cx, cy - spread - 7 * s, cx, cy - spread, c);
            hud.line(cx, cy + spread, cx, cy + spread + 7 * s, c);
        }
        if (!gameMode_) return;   // the possession panel lists the keys (F11 game mode hides it: show them here)
        const char* keys = camera_.possessSight
                               ? "W A S D move   SHIFT sprint   SPACE jump   ALT crouch   MOUSE look   LMB fire   RMB aim   V third person   ESC release"
                               : "W A S D move   SHIFT sprint   SPACE jump   ALT crouch   MOUSE camera   LMB fire   RMB aim   V first person   ESC release";
        const float kw = hud.textWidth(keys, 12 * s) + 24 * s;
        hud.roundRect(W * 0.5f - kw * 0.5f, H - 40 * s, kw, 26 * s, 5 * s, Rgba{0.0f, 0.0f, 0.0f, 0.45f});
        hud.text(W * 0.5f - kw * 0.5f + 12 * s, H - 33 * s, keys, 12 * s, kHint);
    }
}

// ------------------------------------------------------------------------------------------------ captures
// --capture DIR --only play_*: scripted run through the player-control paths (selection, first person, third person,
// camera-relative turn, the arm pulled in by a wall), so they can be checked off-screen like the other figures.
int App::capturePlay() {
    const std::string dir = opt_.outPath.empty() ? "captures" : opt_.outPath;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int width = opt_.sizeGiven ? opt_.width : 1280, height = opt_.sizeGiven ? opt_.height : 720;
    constexpr float kDt = 1.0f / 60.0f;
    settings_ = RenderSettings{};
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    std::printf("play capture: %dx%d, quality %s\n", width, height, qualityName(settings_.quality));
    showHud_ = true;
    showFeed_ = false;
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
            renderFrame(width, height, false);   // keeps the shown pose (blend start) and the TAA history current
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
        const Soldier* sd = soldierOf(*scene_, possessedSoldierIndex());
        std::printf("%s %s (t = %.1f s, cam %s %s, eye %.2f %.2f %.2f, soldier %s at %.2f %.2f %.2f yaw %.0f, arm %.2f/%.2f)\n",
                    ok ? "wrote" : "FAILED", path.c_str(), scene_->time(), cameraModeName(camera_.mode),
                    camera_.mode == CameraMode::Possess ? (camera_.possessSight ? "FIRST" : "THIRD") : "", shownPose_.eye.x,
                    shownPose_.eye.y, shownPose_.eye.z, sd ? sd->name() : "-", sd ? sd->position().x : 0.0f,
                    sd ? sd->position().y : 0.0f, sd ? sd->position().z : 0.0f, sd ? sd->yawDeg() : 0.0f, tp_.arm(), tp_.wantedArm());
        ok ? ++written : ++failed;
    };

    if (DayCycle* clock = sa::dayCycle(*scene_)) {
        clock->setAuto(false);
        clock->setHour(10.5f);
    }
    run(3.0f);
    // An outdoor soldier near the middle of the camp, seen from an orbit view.
    uint32_t key = 0;
    PovSource target0;
    for (const PovSource& p : listPovSources(*scene_, {0.0f, 2.0f, 0.0f}))
        if (p.kind == PovKind::Soldier && p.position.y > -0.5f && (key == 0 || p.distance < target0.distance)) {
            key = p.key;
            target0 = p;
        }
    if (key == 0) {
        std::printf("play capture: no outdoor soldier\n");
        return 1;
    }
    camera_ = Camera{};
    camera_.mode = CameraMode::Orbit;
    camera_.target = target0.position + Vec3{0.0f, 0.9f, 0.0f};
    camera_.distance = 9.0f;
    camera_.pitchDeg = 24.0f;
    run(0.2f);
    selKey_ = key;
    hoverKey_ = 0;
    run(0.1f);
    shot("play_select");

    // Take control: first person through the soldier's eyes, walk forward for a moment.
    controlSelected();
    run(CameraRig::kBlendTime + 0.1f);
    held_.forward = 1.0f;
    run(1.2f);
    held_.forward = 0.0f;
    run(0.3f);
    shot("play_first_person");

    // V: third person on the spring arm, walk on.
    setViewThirdPerson(true);
    run(CameraRig::kBlendTime + 0.1f);
    held_.forward = 1.0f;
    run(1.0f);
    shot("play_third_person");
    // Swing the camera 100 degrees to the right (mouse) and keep holding W: the body turns into the new direction.
    tpLookYaw_ += 100.0f;
    run(1.2f);
    held_.forward = 0.0f;
    run(0.3f);
    shot("play_third_person_turn");
    // Aim (right mouse) and fire from the hip line of the arm.
    aiming_ = true;
    run(0.4f);
    held_.fire = true;
    run(0.25f);
    shot("play_third_person_aim");
    held_.fire = false;
    aiming_ = false;
    // Indoors with the back to a wall: the command room of the bunker (2.6 m ceiling); the arm pulls in short of the
    // west wall instead of looking through it.
    if (Garrison* g = scene_->garrison()) {
        const int si = possessedSoldierIndex();
        Soldier* own = nullptr;
        for (int u = 0; u < g->count(); ++u)
            if (sa::soldierIndex(*scene_, u) == si) own = g->unit(u);
        if (own) own->place(-4.3f, -5.0f, 90.0f, -3.4f);
        tp_.invalidate();
        tpPoseValid_ = false;
        run(0.6f);
        shot("play_third_person_indoor");
        // v9 M3 (after the v8 shots, so those stay comparable): back outside; first-person sprint with the FOV kick,
        // third-person crouch walk, a jump in the air.
        if (own && opt_.only.find(",play_sprint") != std::string::npos) {
            // An open strip of the south apron: 10 m east from (-11, -24) keep 1.2 m from every obstacle.
            own->place(-11.0f, -24.0f, 90.0f);
            tp_.yawDeg = 90.0f;   // first person looks (and runs) east
            setViewThirdPerson(false);
            run(CameraRig::kBlendTime + 0.1f);
            held_.forward = 1.0f;
            held_.boost = true;
            run(1.2f);
            shot("play_sprint_fov");
            held_ = ControlInput{};
            run(0.5f);
            own->place(own->position().x, own->position().z, -90.0f);   // turn round: back west along the strip
            setViewThirdPerson(true);
            run(CameraRig::kBlendTime + 0.1f);
            tpLookYaw_ += 160.0f;   // the camera swings round behind (a little off to the side)
            crouchWant_ = 1;
            run(0.4f);
            held_.forward = 1.0f;
            run(1.0f);
            shot("play_crouch");
            held_.forward = 0.0f;
            crouchWant_ = 0;
            run(0.5f);
            jumpEdge_ = true;
            run(0.3f);
            shot("play_jump");
            run(1.0f);
        }
    }
    // ---- the barracks hall: outside, inside by day (residents training / talking / resting), a resident in third
    // person inside, and the hall at night (asleep in the bunks)
    possess(-1);
    held_ = ControlInput{};
    auto freeView = [&](const Vec3& eye, const Vec3& at, float fov) {
        camera_ = Camera{};
        camera_.mode = CameraMode::Free;
        camera_.freePos = eye;
        const Vec3 d = normalize(at - eye);
        camera_.freeYawDeg = degrees(std::atan2(d.x, d.z));
        camera_.freePitchDeg = degrees(std::asin(d.y));
        camera_.fovDeg = fov;
        rig_.cancel();
    };
    if (opt_.only.find(",play_hall") != std::string::npos) {
        run(20.0f);   // the residents spread over their day stops
        freeView({14.5f, 5.5f, -1.5f}, {23.5f, 0.6f, -10.5f}, 55.0f);
        run(0.2f);
        shot("play_hall_outside");
        freeView({27.0f, 1.9f, -9.2f}, {20.3f, 0.5f, -13.6f}, 72.0f);
        run(0.2f);
        shot("play_hall_inside");
        // take control of a resident in the hall (third person)
        const std::vector<PovSource> list = listPovSources(*scene_, {23.0f, 1.0f, -11.5f});
        int best = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].kind == PovKind::Soldier && list[i].name.find("BARRACKS") != std::string::npos &&
                std::fabs(list[i].position.x - 23.0f) < 5.0f && std::fabs(list[i].position.z + 11.8f) < 3.5f &&
                (best < 0 || list[i].distance < list[static_cast<size_t>(best)].distance))
                best = static_cast<int>(i);
        if (best >= 0) {
            selKey_ = list[static_cast<size_t>(best)].key;
            controlSelected();
            setViewThirdPerson(true);
            run(CameraRig::kBlendTime + 0.2f);
            tpLookYaw_ += 140.0f;
            run(0.8f);
            shot("play_hall_resident");
            possess(-1);
        }
        // close-ups of the activities (wait until a resident is in the middle of one, then frame him from the side)
        const Garrison* gar = sa::garrison(*scene_);
        auto closeUp = [&](Activity act, const char* name, float side) {
            if (!wanted(name) || !gar) return;
            for (float t = 0.0f; t < 400.0f; t += 1.0f) {
                for (int u = Garrison::kSoldiers; u < gar->count(); ++u) {
                    const Soldier* s = gar->unit(u);
                    if (!s || s->activity() != act || s->travelling()) continue;
                    const Vec3 p = s->position();
                    const float yr = radians(s->yawDeg());
                    const Vec3 f{std::sin(yr), 0.0f, std::cos(yr)}, rgt{-f.z, 0.0f, f.x};
                    const float h = act == Activity::PullUps ? 1.5f : 0.6f;
                    freeView(p + rgt * (2.3f * side) + f * 0.4f + Vec3{0.0f, h + 0.5f, 0.0f}, p + Vec3{0.0f, h, 0.0f}, 60.0f);
                    run(1.2f);
                    shot(name);
                    return;
                }
                run(1.0f);
            }
            std::printf("play capture: no %s within 400 s\n", activityName(act));
        };
        closeUp(Activity::PushUps, "play_hall_pushups", 1.0f);
        closeUp(Activity::SitUps, "play_hall_situps", 1.0f);
        closeUp(Activity::PullUps, "play_hall_pullups", 1.0f);
        closeUp(Activity::Talk, "play_hall_talk", -1.0f);
        closeUp(Activity::Sleep, "play_hall_sleep", 1.0f);
        if (DayCycle* clock = sa::dayCycle(*scene_)) clock->setHour(23.0f);
        run(70.0f);   // shift change: everybody to bed
        freeView({27.0f, 1.9f, -9.2f}, {20.3f, 0.5f, -13.6f}, 72.0f);
        run(0.2f);
        shot("play_hall_night");
    }
    playCapture_ = false;
    std::printf("play capture: %d written, %d failed\n", written, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace ts
