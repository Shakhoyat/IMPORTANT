#include "scene/Pov.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/Uav.h"
#include "scene/Control.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "scene/SceneApi.h"

namespace ts {

namespace {

constexpr uint32_t kCatControllable = 0x01000000u, kCatUav = 0x02000000u, kCatRover = 0x03000000u, kCatRound = 0x04000000u;

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Kind of a possessable controllable from its name (doors, barrier, sky clock, flag, windsock: not moving objects).
bool vehicleKind(const char* name, PovKind& kind) {
    if (nameHas(name, "DOOR") || nameHas(name, "BARRIER") || nameHas(name, "GATE") || nameHas(name, "SKY") ||
        nameHas(name, "CLOCK") || nameHas(name, "FLAG") || nameHas(name, "WINDSOCK"))
        return false;
    if (nameHas(name, "UAV") || nameHas(name, "DRONE")) kind = PovKind::Uav;
    else if (nameHas(name, "HELI")) kind = PovKind::Helicopter;
    else if (nameHas(name, "TANK") || nameHas(name, "MBT")) kind = PovKind::Tank;
    else if (nameHas(name, "AA") || nameHas(name, "GATLING")) kind = PovKind::AaGun;
    else if (nameHas(name, "SAM") || nameHas(name, "MLRS") || nameHas(name, "LAUNCH") || nameHas(name, "ROCKET")) kind = PovKind::Launcher;
    else if (nameHas(name, "RADAR")) kind = PovKind::Radar;
    else return false;
    return true;
}

float pickRadius(PovKind k) {
    switch (k) {
        case PovKind::Soldier: return 0.55f;
        case PovKind::Rover: return 0.5f;
        case PovKind::Uav: return 0.7f;
        case PovKind::Helicopter: return 3.2f;
        case PovKind::Tank: return 3.0f;
        case PovKind::AaGun: return 1.6f;
        case PovKind::Launcher: return 3.0f;
        case PovKind::Radar: return 2.2f;
        case PovKind::Missile: return 0.7f;
        case PovKind::Shell: return 0.6f;
    }
    return 0.5f;
}

float centreHeight(PovKind k) {
    switch (k) {
        case PovKind::Soldier: return 0.95f;
        case PovKind::Rover: return 0.2f;
        case PovKind::Tank: return 1.2f;
        case PovKind::AaGun: return 1.0f;
        case PovKind::Launcher: return 1.4f;
        case PovKind::Radar: return 1.5f;
        default: return 0.0f;
    }
}

// Free look on top of a pose: yaw about the world up, pitch about the camera's right axis.
void applyLook(RigPose& p, float yawDeg, float pitchDeg) {
    if (yawDeg == 0.0f && pitchDeg == 0.0f) return;
    const Quat qy = quatAxisAngle({0, 1, 0}, radians(-yawDeg));
    const Quat qp = quatAxisAngle({1, 0, 0}, radians(clampf(pitchDeg, -80.0f, 80.0f)));
    p.rot = quatNormalize(quatMul(qy, quatMul(p.rot, qp)));
}

RigPose fromHint(const CameraHint& h, float nearZ) {
    RigPose p = RigPose::look(h.eye, h.target, h.fovDeg);
    p.nearZ = nearZ;
    return p;
}

// Belly camera of a UAV body: looking down and a little forward.
RigPose bellyCam(const Mat4& body) {
    const Vec3 eye = transformPoint(body, {0.0f, -0.12f, 0.05f});
    const Vec3 fwd = normalize(transformDirection(body, {0, 0, 1}));
    Vec3 flatFwd{fwd.x, 0.0f, fwd.z};
    flatFwd = length(flatFwd) > 1e-3f ? normalize(flatFwd) : Vec3{0, 0, 1};
    RigPose p = RigPose::look(eye, eye + Vec3{0, -1.0f, 0} + flatFwd * 0.35f, 70.0f, flatFwd);
    p.nearZ = 0.05f;
    return p;
}

RigPose uavChase(const Vec3& p, float yawDeg) {
    const float yaw = radians(yawDeg);
    const Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)};
    RigPose r = RigPose::look(p - fwd * 3.2f + Vec3{0, 1.3f, 0}, p + fwd * 3.0f - Vec3{0, 0.8f, 0}, 55.0f);
    r.nearZ = 0.15f;
    return r;
}

}  // namespace

const char* povKindName(PovKind k) {
    switch (k) {
        case PovKind::Soldier: return "SOLDIER";
        case PovKind::Rover: return "INTRUDER ROVER";
        case PovKind::Uav: return "UAV";
        case PovKind::Helicopter: return "HELICOPTER";
        case PovKind::Tank: return "TANK";
        case PovKind::AaGun: return "AA GUN";
        case PovKind::Launcher: return "LAUNCHER";
        case PovKind::Radar: return "RADAR";
        case PovKind::Missile: return "MISSILE";
        case PovKind::Shell: return "TANK SHELL";
    }
    return "?";
}

const char* povKindTag(PovKind k) {
    switch (k) {
        case PovKind::Soldier: return "SLD";
        case PovKind::Rover: return "RVR";
        case PovKind::Uav: return "UAV";
        case PovKind::Helicopter: return "HEL";
        case PovKind::Tank: return "TNK";
        case PovKind::AaGun: return "AAG";
        case PovKind::Launcher: return "LCH";
        case PovKind::Radar: return "RDR";
        case PovKind::Missile: return "MSL";
        case PovKind::Shell: return "SHL";
    }
    return "???";
}

bool povHostile(PovKind k) { return k == PovKind::Rover; }
bool povProjectile(PovKind k) { return k == PovKind::Missile || k == PovKind::Shell; }

const char* povViewName(PovView v) {
    switch (v) {
        case PovView::First: return "FIRST PERSON";
        case PovView::Chase: return "CHASE";
        case PovView::Alt: return "ALT CAM";
    }
    return "?";
}

uint32_t projectileKey(const Projectile& p, float combatTime) {
    // Launch time in 1/60 s ticks (time - age is constant over the flight), owner and kind.
    const long tick = std::lround(static_cast<double>(combatTime - p.age) * 60.0);
    return kCatRound | (static_cast<uint32_t>(p.owner & 0x1F) << 18) | (static_cast<uint32_t>(p.kind) << 16) |
           (static_cast<uint32_t>(tick) & 0xFFFFu);
}

std::vector<PovSource> listPovSources(const Scene& scene, const Vec3& viewer) {
    std::vector<PovSource> out;
    const std::vector<Controllable*> c = sa::controllables(scene);
    const Garrison* g = scene.garrison();
    auto isSoldier = [&](const Controllable* x, int& roster) {
        roster = -1;
        if (!g) return false;
        for (int k = 0; k < g->count(); ++k)
            if (g->soldier(k) == x) {
                roster = k;
                return true;
            }
        return false;
    };
    auto finish = [&](PovSource& s) {
        s.radius = pickRadius(s.kind);
        s.centre = s.position + Vec3{0.0f, centreHeight(s.kind), 0.0f};
        s.distance = length(s.centre - viewer);
        out.push_back(s);
    };
    // Vehicles and installations, then the soldiers (each group in scene order).
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < c.size(); ++i) {
            if (!c[i] || !c[i]->possessable()) continue;
            int roster = -1;
            const bool soldier = isSoldier(c[i], roster);
            if (soldier != (pass == 1)) continue;
            PovSource s;
            if (soldier) {
                s.kind = PovKind::Soldier;
            } else if (!vehicleKind(c[i]->name(), s.kind)) {
                continue;
            }
            s.key = kCatControllable | static_cast<uint32_t>(i);
            s.controllable = static_cast<int>(i);
            s.uav = s.kind == PovKind::Uav ? 0 : -1;
            s.name = c[i]->name();
            s.position = c[i]->position();
            s.canControl = true;
            s.hasAlt = s.kind == PovKind::Tank || s.kind == PovKind::Uav;
            finish(s);
        }
    }
    // The wing UAV (flown by the squad only).
    for (int u = 1; u < kUavCount; ++u) {
        PovSource s;
        s.kind = PovKind::Uav;
        s.key = kCatUav | static_cast<uint32_t>(u);
        s.uav = u;
        char buf[48];
        std::snprintf(buf, sizeof(buf), "UAV-%d DRONE (WING)", u + 1);
        s.name = buf;
        s.position = scene.droneState(u).position;
        s.velocity = scene.droneState(u).velocity;
        s.speed = length(s.velocity);
        s.hasAlt = true;
        finish(s);
    }
    // Intruder rovers on the field.
    const auto& ins = scene.intruders();
    for (size_t i = 0; i < ins.size(); ++i) {
        if (ins[i]->life != Intruder::Life::Alive) continue;
        PovSource s;
        s.kind = PovKind::Rover;
        s.key = kCatRover | static_cast<uint32_t>(i);
        s.intruder = static_cast<int>(i);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "INTRUDER R%d", static_cast<int>(i) + 1);
        s.name = buf;
        s.position = ins[i]->state.position;
        const float v = 0.5f * (ins[i]->state.leftSpeed + ins[i]->state.rightSpeed);
        s.velocity = Vec3{std::sin(ins[i]->state.yaw), 0.0f, std::cos(ins[i]->state.yaw)} * v;
        s.speed = std::fabs(v);
        s.canControl = true;
        s.hasAlt = true;
        finish(s);
    }
    // Rounds in flight (tracers are too fast and short-lived to ride).
    const Combat& combat = scene.combat();
    const std::vector<Projectile>& rounds = combat.projectiles();
    for (size_t i = 0; i < rounds.size(); ++i) {
        const Projectile& p = rounds[i];
        if (!p.alive || p.kind == ProjectileKind::Tracer) continue;
        PovSource s;
        s.kind = p.kind == ProjectileKind::Missile ? PovKind::Missile : PovKind::Shell;
        s.key = projectileKey(p, combat.time());
        s.projectile = static_cast<int>(i);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%s (%s)", s.kind == PovKind::Missile ? "UAV ROUND" : "TANK SHELL",
                      p.owner == kDroneOwner ? "UAV-1" : p.owner == 10 ? "UAV-2" : "CAMP");
        s.name = buf;
        s.position = p.position;
        s.velocity = p.velocity;
        s.speed = length(p.velocity);
        finish(s);
    }
    return out;
}

void sortByDistance(std::vector<PovSource>& list) {
    std::stable_sort(list.begin(), list.end(), [](const PovSource& a, const PovSource& b) { return a.distance < b.distance; });
}

int findPov(const std::vector<PovSource>& list, uint32_t key) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].key == key) return static_cast<int>(i);
    return -1;
}

int cyclePov(const std::vector<PovSource>& list, uint32_t key, int dir) {
    const int n = static_cast<int>(list.size());
    if (n == 0) return -1;
    const int at = findPov(list, key);
    if (at < 0) return dir >= 0 ? 0 : n - 1;
    return ((at + (dir >= 0 ? 1 : -1)) % n + n) % n;
}

int pickPov(const std::vector<PovSource>& list, const Vec3& origin, const Vec3& dirIn, float maxAngleDeg) {
    const Vec3 dir = normalize(dirIn);
    int best = -1;
    float bestT = 1e30f;
    for (size_t i = 0; i < list.size(); ++i) {
        // Ray / sphere: |o + t d - c|^2 = r^2
        const Vec3 oc = origin - list[i].centre;
        const float b = dot(oc, dir), cc = dot(oc, oc) - list[i].radius * list[i].radius;
        const float disc = b * b - cc;
        if (disc < 0.0f) continue;
        const float sq = std::sqrt(disc);
        float t = -b - sq;
        if (t < 0.0f) t = -b + sq;
        if (t < 0.0f || t >= bestT) continue;
        bestT = t;
        best = static_cast<int>(i);
    }
    if (best >= 0) return best;
    float bestAngle = maxAngleDeg, bestD = 1e30f;
    for (size_t i = 0; i < list.size(); ++i) {
        const Vec3 to = list[i].centre - origin;
        const float d = length(to);
        if (d < 1e-4f) continue;
        const float a = degrees(std::acos(clampf(dot(to / d, dir), -1.0f, 1.0f)));
        // Smallest angle; within 0.05 deg of each other the nearer object wins (same line of sight).
        if (a < bestAngle - 0.05f || (a <= bestAngle + 0.05f && a <= maxAngleDeg && d < bestD)) {
            bestAngle = std::min(a, bestAngle);
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void pixelRay(const Mat4& invViewProj, float px, float py, int w, int h, Vec3& origin, Vec3& dir) {
    const float x = 2.0f * px / static_cast<float>(std::max(w, 1)) - 1.0f;
    const float y = 1.0f - 2.0f * py / static_cast<float>(std::max(h, 1));
    const Vec4 n = invViewProj * Vec4{x, y, -1.0f, 1.0f};
    const Vec4 f = invViewProj * Vec4{x, y, 1.0f, 1.0f};
    const Vec3 a{n.x / n.w, n.y / n.w, n.z / n.w}, b{f.x / f.w, f.y / f.w, f.z / f.w};
    origin = a;
    dir = normalize(b - a);
}

RigPose soldierEyePose(const Scene& scene, int controllable, float lookYawDeg, float lookPitchDeg) {
    const std::vector<Controllable*> c = sa::controllables(scene);
    RigPose p;
    if (controllable < 0 || controllable >= static_cast<int>(c.size()) || !c[controllable]) return p;
    const CameraHint h = c[controllable]->camera(true);
    p = RigPose::look(h.eye, h.target, h.fovDeg);
    p.nearZ = 0.06f;
    // Eyes in the head: the head node carries the gait's bob and sway (pelvis -> spine -> chest -> neck -> head).
    if (const Soldier* s = dynamic_cast<const Soldier*>(c[controllable]))
        if (const SceneNode* head = s->rig().head) {
            const Vec3 eye = transformPoint(head->world, {0.0f, 0.11f, 0.12f});
            if (finite(eye) && length(eye - h.eye) < 0.6f) p.eye = eye;
        }
    applyLook(p, lookYawDeg, lookPitchDeg);
    return p;
}

bool povPose(const Scene& scene, const PovSource& src, PovView view, RigPose& out, float lookYawDeg, float lookPitchDeg) {
    RigPose p;
    if (!src.hasAlt && view == PovView::Alt) view = PovView::First;
    if (src.controllable >= 0) {
        const std::vector<Controllable*> c = sa::controllables(scene);
        if (src.controllable >= static_cast<int>(c.size()) || !c[src.controllable]) return false;
        const Controllable& ctl = *c[src.controllable];
        if (src.kind == PovKind::Soldier && view == PovView::First) {
            out = soldierEyePose(scene, src.controllable, lookYawDeg, lookPitchDeg);
            return finite(out.eye);
        }
        if (view == PovView::Alt && src.kind == PovKind::Uav) {
            p = bellyCam(scene.uav(0).root->world);
        } else if (view == PovView::Alt && src.kind == PovKind::Tank) {
            // Commander's hatch: above and behind the gunner's sight, looking the same way (level).
            const CameraHint h = ctl.camera(true);
            Vec3 d = h.target - h.eye;
            d.y = 0.0f;
            d = length(d) > 1e-4f ? normalize(d) : Vec3{0, 0, 1};
            const Vec3 eye = h.eye - d * 0.9f + Vec3{0.0f, 0.75f, 0.0f};
            p = RigPose::look(eye, eye + d * 10.0f - Vec3{0.0f, 0.9f, 0.0f}, 62.0f);
            p.nearZ = 0.1f;
        } else {
            const bool first = view == PovView::First;
            p = fromHint(ctl.camera(first), first ? 0.08f : 0.15f);
        }
    } else if (src.uav >= 0) {
        if (src.uav >= kUavCount) return false;
        if (view == PovView::First) {
            const Mat4 cam = scene.cameraWorld(src.uav);
            p.eye = transformPoint(cam, {0, 0, kLampFrontOffset + 0.03f});
            p.rot = quatLook(transformDirection(cam, {0, 0, 1}), normalize(transformDirection(cam, {0, 1, 0})));
            p.fovDeg = 40.0f;
            p.nearZ = 0.05f;
        } else if (view == PovView::Alt) {
            p = bellyCam(scene.uav(src.uav).root->world);
        } else {
            p = uavChase(scene.droneState(src.uav).position, scene.droneState(src.uav).yawDeg);
        }
    } else if (src.intruder >= 0) {
        const auto& ins = scene.intruders();
        if (src.intruder >= static_cast<int>(ins.size())) return false;
        const Intruder& in = *ins[src.intruder];
        if (in.life != Intruder::Life::Alive && in.life != Intruder::Life::Wreck) return false;
        const Mat4& w = in.rig.root->world;
        const Vec3 fwd = normalize(transformDirection(w, {0, 0, 1})), up = normalize(transformDirection(w, {0, 1, 0}));
        if (view == PovView::First) {          // hood camera at the front of the hull (the bumper sits in the grass)
            p.eye = transformPoint(w, {0.0f, 0.42f, 0.24f});
            p.rot = quatLook(fwd - up * 0.10f, up);
            p.fovDeg = 72.0f;
            p.nearZ = 0.06f;
        } else if (view == PovView::Alt) {     // mast camera beside the scanner head
            p.eye = transformPoint(w, {0.0f, 0.60f, 0.10f});
            p.rot = quatLook(fwd - up * 0.12f, up);
            p.fovDeg = 66.0f;
            p.nearZ = 0.03f;
        } else {
            const Vec3 pos = in.state.position;
            Vec3 f{fwd.x, 0.0f, fwd.z};
            f = length(f) > 1e-3f ? normalize(f) : Vec3{0, 0, 1};
            p = RigPose::look(pos - f * 1.9f + Vec3{0, 1.15f, 0}, pos + f * 1.4f + Vec3{0, 0.1f, 0}, 60.0f);
            p.nearZ = 0.08f;
        }
    } else if (povProjectile(src.kind)) {   // projectile -1: the round is gone, its last state stands in
        const std::vector<Projectile>& rounds = scene.combat().projectiles();
        Vec3 pos = src.position, vel = src.velocity;
        if (src.projectile >= 0 && src.projectile < static_cast<int>(rounds.size())) {
            pos = rounds[src.projectile].position;
            vel = rounds[src.projectile].velocity;
        }
        const Vec3 d = length(vel) > 1e-3f ? normalize(vel) : Vec3{0, 0, 1};
        if (view == PovView::First) {          // nose camera
            p.eye = pos + d * 0.25f;
            p.rot = quatLook(d);
            p.fovDeg = 75.0f;
            p.nearZ = 0.05f;
        } else {                               // follow camera behind and above the round
            p = RigPose::look(pos - d * 1.8f + Vec3{0, 0.4f, 0}, pos + d * 4.0f, 62.0f);
            p.nearZ = 0.1f;
        }
    } else {
        return false;
    }
    applyLook(p, lookYawDeg, lookPitchDeg);
    out = p;
    return finite(out.eye) && std::isfinite(out.rot.w);
}

void PovSpeedMeter::update(std::vector<PovSource>& list, float time) {
    for (PovSource& s : list) {
        if (povProjectile(s.kind)) continue;   // exact velocity already
        auto it = last_.find(s.key);
        if (it == last_.end()) {
            Entry e;
            e.position = s.position;
            e.time = time;
            e.speed = s.speed;
            e.velocity = s.velocity;
            last_[s.key] = e;
            continue;
        }
        Entry& e = it->second;
        const float dt = time - e.time;
        if (dt > 1e-4f) {
            const Vec3 v = (s.position - e.position) / dt;
            const float k = 1.0f - std::exp(-dt / 0.25f);   // 0.25 s smoothing
            e.velocity = lerp(e.velocity, v, k);
            e.speed = lerpf(e.speed, length(v), k);
            e.position = s.position;
            e.time = time;
        } else if (dt < 0.0f) {   // the clock jumped back (new world)
            e = Entry{s.position, time, 0.0f, {}};
        }
        if (s.kind != PovKind::Uav || s.uav == 0) {   // the wing UAV's state has its own velocity
            s.speed = e.speed;
            s.velocity = e.velocity;
        }
    }
}

int ProjectileFollow::track(const std::vector<Projectile>& rounds, float combatTime, float dt) {
    if (lost) return -1;
    int found = -1;
    for (size_t i = 0; i < rounds.size() && found < 0; ++i)
        if (rounds[i].alive && projectileKey(rounds[i], combatTime) == key) found = static_cast<int>(i);
    if (found < 0) {
        const Vec3 predicted = position + velocity * std::max(dt, 0.0f);
        float best = 2.0f + length(velocity) * std::max(dt, 0.0f) * 0.5f;
        for (size_t i = 0; i < rounds.size(); ++i) {
            const Projectile& p = rounds[i];
            if (!p.alive || static_cast<int>(p.kind) != kind || p.owner != owner) continue;
            const float d = length(p.position - predicted);
            if (d < best) {
                best = d;
                found = static_cast<int>(i);
            }
        }
    }
    if (found < 0) {
        lost = true;
        impact = position;
        return -1;
    }
    const Projectile& p = rounds[found];
    position = p.position;
    velocity = p.velocity;
    key = projectileKey(p, combatTime);
    kind = static_cast<int>(p.kind);
    owner = p.owner;
    return found;
}

float designationAlert(const Scene& scene, const Vec3& p) {
    float a = 0.0f;
    const Squad& sq = scene.squad();
    for (int u = 0; u < kUavCount; ++u) {
        if (!sq.designatorValid(u)) continue;
        const Vec3 d = sq.designatorPoint(u) - p;
        const float dist = std::sqrt(d.x * d.x + d.z * d.z);
        a = std::max(a, clampf(1.0f - dist / 3.0f, 0.0f, 1.0f));
    }
    for (const Track& tr : sq.tracker().tracks()) {
        if (tr.state == TrackState::Tentative) continue;
        const Vec3 d = tr.position() - p;
        if (std::sqrt(d.x * d.x + d.z * d.z) < 2.0f) a = std::max(a, 0.6f);
    }
    return a;
}

PickerLayout pickerLayout(int screenW, int screenH, int count, int selected) {
    PickerLayout l;
    const float s = static_cast<float>(screenH) / 900.0f;
    l.w = std::min(640.0f * s, static_cast<float>(screenW) - 40.0f * s);
    l.x = 40.0f * s;
    l.headerH = 64.0f * s;
    l.rowH = 26.0f * s;
    const int maxRows = std::max(1, static_cast<int>((static_cast<float>(screenH) * 0.72f - l.headerH) / l.rowH));
    l.rows = std::min(count, maxRows);
    l.y = (static_cast<float>(screenH) - (l.headerH + l.rowH * static_cast<float>(std::max(l.rows, 1)))) * 0.5f;
    // Scroll so the selection stays visible (centred when possible).
    l.firstRow = 0;
    if (count > l.rows && selected >= 0) l.firstRow = std::clamp(selected - l.rows / 2, 0, count - l.rows);
    return l;
}

int pickerRowAt(const PickerLayout& l, int count, float x, float y) {
    if (x < l.x || x > l.x + l.w) return -1;
    const float top = l.y + l.headerH;
    if (y < top) return -1;
    const int row = static_cast<int>((y - top) / l.rowH);
    if (row < 0 || row >= l.rows) return -1;
    const int idx = l.firstRow + row;
    return idx < count ? idx : -1;
}

}  // namespace ts
