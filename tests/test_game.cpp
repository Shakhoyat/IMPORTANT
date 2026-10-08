// T240-T258 (v7 WP-G game camera): quaternion poses, the spring-blended camera rig (continuity, moving targets, no
// overshoot), game-feel curves, compass strip, free-fly camera, the POV source list (contents, stable keys, every
// view valid), picker order and layout, Insert / Delete cycling, the middle-click ray pick, soldier eyes with head bob,
// possession round trips (soldier, intruder rover, UAV), the rover drive at the field edge, projectile follow until
// impact, and the v7 key table.
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "scene/CameraRig.h"
#include "scene/Combat.h"
#include "scene/Control.h"
#include "scene/HudMath.h"
#include "scene/Pov.h"
#include "scene/Scene.h"
#include "scene/SceneApi.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

float maxAbsDiff(const Mat4& a, const Mat4& b) {
    float m = 0.0f;
    for (int i = 0; i < 16; ++i) m = std::max(m, std::fabs(a.m[i] - b.m[i]));
    return m;
}

float qlen(const Quat& q) { return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z); }

// One v6 day scene shared by the scene tests (the garrison's route planning is the slow part).
Scene& dayScene() {
    static Scene* s = nullptr;
    if (!s) {
        s = new Scene(SceneOptions::v6(4242u, 10.5f, false));
        for (int i = 0; i < 30; ++i) s->update(1.0f / 60.0f, Detection{});
    }
    return *s;
}

int controllableNamed(const Scene& s, const char* needle) {
    const std::vector<Controllable*> c = sa::controllables(s);
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i] && nameHas(c[i]->name(), needle)) return static_cast<int>(i);
    return -1;
}

}  // namespace

TS_TEST_GROUP(game_camera) {
    // ---- T240 quaternion poses
    {
        const Vec3 eye{3.0f, 2.0f, -4.0f}, target{-1.0f, 0.5f, 6.0f};
        const RigPose p = RigPose::look(eye, target, 55.0f);
        const float dView = maxAbsDiff(p.view(), lookAt(eye, target, {0, 1, 0}));
        const RigPose back = RigPose::fromView(lookAt(eye, target, {0, 1, 0}), eye, 55.0f);
        const float dRot = quatAngleDeg(p.rot, back.rot);
        const float dFwd = length(p.forward() - normalize(target - eye));
        const Quat a = quatAxisAngle({0, 1, 0}, 0.3f), b = quatAxisAngle({1, 0, 0}, -1.1f);
        const float ends = quatAngleDeg(quatSlerp(a, b, 0.0f), a) + quatAngleDeg(quatSlerp(a, b, 1.0f), b);
        const float half = std::fabs(quatAngleDeg(quatSlerp(a, b, 0.5f), a) - 0.5f * quatAngleDeg(a, b));
        const float inv = maxAbsDiff(p.world() * p.view(), Mat4::identity());
        const float fov = std::fabs(fovFromProjection(perspective(radians(47.0f), 1.7f, 0.1f, 100.0f)) - 47.0f);
        const bool pass = dView < 1e-4f && dRot < 0.05f && dFwd < 1e-4f && ends < 0.05f && half < 0.05f && inv < 1e-4f && fov < 1e-3f;
        record("T240", "Game camera poses: quaternion look-at equals lookAt(), view <-> pose round trip, slerp ends / midpoint, world = view^-1, FOV from the projection",
               "view diff < 1e-4, round trip < 0.05 deg, slerp exact at 0 / 1 and half angle at 0.5, inverse < 1e-4",
               fmt("view %.2e, round trip %.3f deg, fwd %.2e, slerp ends %.3f mid %.3f deg, inverse %.2e, fov %.4f", dView, dRot, dFwd, ends, half, inv, fov),
               pass);
    }
    // ---- T241 blend continuity (static target)
    {
        const RigPose from = RigPose::look({0, 10, 30}, {0, 0, 0}, 50.0f);
        const RigPose to = RigPose::look({12, 1.7f, -4}, {12, 1.5f, -14}, 70.0f);
        CameraRig rig;
        rig.start(from, to);
        const RigPose p0 = rig.evaluate(to);
        const float startErr = length(p0.eye - from.eye) + quatAngleDeg(p0.rot, from.rot) + std::fabs(p0.fovDeg - from.fovDeg);
        float maxStep = 0.0f, firstStep = 0.0f, prevW = 1.0f;
        bool monotone = true;
        Vec3 prev = p0.eye;
        int frames = 0;
        while (rig.active() && frames < 200) {
            rig.advance(1.0f / 60.0f);
            const RigPose p = rig.evaluate(to);
            const float st = length(p.eye - prev);
            if (frames == 0) firstStep = st;
            maxStep = std::max(maxStep, st);
            monotone = monotone && rig.offsetWeight() <= prevW + 1e-6f;
            prevW = rig.offsetWeight();
            prev = p.eye;
            ++frames;
        }
        const RigPose end = rig.evaluate(to);
        const float endErr = length(end.eye - to.eye) + quatAngleDeg(end.rot, to.rot);
        const float dist = length(from.eye - to.eye);
        const float timeS = static_cast<float>(frames) / 60.0f;
        const bool pass = startErr < 1e-3f && endErr < 1e-3f && monotone && std::fabs(timeS - CameraRig::kBlendTime) < 0.03f &&
                          firstStep < 0.02f * dist && maxStep < 0.12f * dist;
        record("T241", "Camera rig blend: starts exactly at the old view, ends exactly at the new one after 0.6 s, offset decays monotonically (critically damped), eases in (no jump on the first frame)",
               "start / end error < 1e-3, 0.6 s, monotone, first frame step < 2 % and every step < 12 % of the 37 m jump",
               fmt("start %.2e, end %.2e, %d frames (%.2f s), monotone %d, first step %.3f m, max step %.2f m of %.1f m", startErr, endErr, frames,
                   timeS, monotone, firstStep, maxStep, dist),
               pass);
    }
    // ---- T242 moving target: converges, never overshoots
    {
        CameraRig rig;
        const RigPose from = RigPose::look({-20, 6, 0}, {0, 0, 0}, 50.0f);
        RigPose target = RigPose::look({0, 1, 0}, {0, 1, 10}, 60.0f);
        rig.start(from, target);
        float prevD = 1e9f;
        bool noOvershoot = true;
        float t = 0.0f;
        for (int i = 0; i < 60; ++i) {
            rig.advance(1.0f / 60.0f);
            t += 1.0f / 60.0f;
            target.eye = Vec3{0, 1, 5.0f * t};   // 5 m/s along +z
            const RigPose p = rig.evaluate(target);
            const float d = length(p.eye - target.eye);
            noOvershoot = noOvershoot && d <= prevD + 0.05f;
            prevD = d;
        }
        const float finalD = length(rig.evaluate(target).eye - target.eye);
        const bool pass = noOvershoot && finalD < 0.01f && !rig.active();
        record("T242", "Camera rig with a moving target (5 m/s): the offset is relative to the target, so the camera catches up without overshoot",
               "distance to the target non-increasing, < 1 cm at the end, blend finished",
               fmt("non-increasing %d, final %.4f m, active %d", noOvershoot, finalD, rig.active()), pass);
    }
    // ---- T243 game-feel curves
    {
        const bool lb = jumpLetterbox(0.0f) == 0.0f && jumpLetterbox(1.0f) == 0.0f && std::fabs(jumpLetterbox(0.5f) - 0.11f) < 1e-4f &&
                        jumpLetterbox(0.2f) > 0.05f;
        const bool kick = fovKickTarget(false, 4.0f, 4.5f) == 0.0f && fovKickTarget(true, 2.0f, 4.0f) < fovKickTarget(true, 4.0f, 4.0f) &&
                          std::fabs(fovKickTarget(true, 99.0f, 4.0f) - 8.0f) < 1e-4f;
        const bool hit = hitMarkerAlpha(0.03f) == 1.0f && hitMarkerAlpha(0.2f) > 0.0f && hitMarkerAlpha(0.2f) < 1.0f && hitMarkerAlpha(0.5f) == 0.0f;
        float v = 0.0f;
        for (int i = 0; i < 60; ++i) v = easeTowards(v, 10.0f, 1.0f / 60.0f, 0.2f);
        float v2 = 0.0f;
        for (int i = 0; i < 6; ++i) v2 = easeTowards(v2, 10.0f, 1.0f / 6.0f, 0.2f);
        const bool ease = std::fabs(v - v2) < 1e-3f && std::fabs(v - 10.0f * (1.0f - std::exp(-5.0f))) < 1e-2f;
        record("T243", "Game feel curves: jump letterbox bump, FOV kick with speed (only on boost, capped 8 deg), hit marker fade, frame-rate independent easing",
               "letterbox 0 at both ends and 0.11 mid-way; kick 0 without boost, grows, capped; hit 1 -> 0 by 0.4 s; 60 Hz == 6 Hz ease",
               fmt("letterbox %d (mid %.3f), kick %d, hit %d, ease %d (%.4f vs %.4f)", lb, jumpLetterbox(0.5f), kick, hit, ease, v, v2),
               lb && kick && hit && ease);
    }
    // ---- T244 compass strip
    {
        const std::vector<CompassTick> ticks = compassTicks(0.0f, 120.0f, 600.0f, 15);
        bool northCentre = false, eastRight = false, westLeft = false;
        for (const CompassTick& t : ticks) {
            if (t.deg == 0 && std::fabs(t.x) < 1e-3f && std::string(t.label) == "N") northCentre = true;
            if (t.deg == 45 && t.x > 0.0f && std::string(t.label) == "NE") eastRight = true;
            if (t.deg == 315 && t.x < 0.0f && std::string(t.label) == "NW") westLeft = true;
        }
        bool in1 = false, in2 = true;
        const float o1 = compassOffset(350.0f, 20.0f, 120.0f, 600.0f, &in1);    // 30 deg to the right across north
        const float o2 = compassOffset(0.0f, 180.0f, 120.0f, 600.0f, &in2);     // behind: clamped to the edge
        const bool head = std::fabs(headingDeg({1, 0, 0}) - 90.0f) < 1e-3f && std::fabs(headingDeg({0, 0, -1}) - 180.0f) < 1e-3f;
        const bool pass = ticks.size() == 9 && northCentre && eastRight && westLeft && in1 && std::fabs(o1 - 150.0f) < 1e-2f && !in2 &&
                          std::fabs(std::fabs(o2) - 300.0f) < 1e-2f && head;
        record("T244", "Compass strip: 120 deg over 600 px, ticks every 15 deg labelled at 45, bearings wrap across north and clamp behind, heading 0 = +z",
               "9 ticks, N at the centre, NE right, NW left, 350 -> 020 = +150 px, behind clamped to +-300 px",
               fmt("%zu ticks, N %d NE %d NW %d, offset %.1f (in %d), behind %.1f (in %d), heading %d", ticks.size(), northCentre, eastRight,
                   westLeft, o1, in1, o2, in2, head),
               pass);
    }
    // ---- T245 free-fly camera
    {
        FlyCam f;
        f.speed = 8.0f;
        Vec3 moved;
        for (int i = 0; i < 60; ++i) moved = moved + f.step(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, false, 1.0f / 60.0f);
        const float cruise = length(f.velocity);
        for (int i = 0; i < 60; ++i) f.step(90.0f, 0.0f, 1.0f, 0.0f, 0.0f, true, 1.0f / 60.0f);
        const float boosted = length(f.velocity);
        const bool dirX = f.velocity.x > 0.95f * boosted;
        for (int i = 0; i < 60; ++i) f.step(90.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, 1.0f / 60.0f);
        const float glide = length(f.velocity);
        FlyCam w;
        w.wheel(100.0f);
        const float hi = w.speed;
        w.wheel(-100.0f);
        const bool pass = std::fabs(cruise - 8.0f) < 0.05f && moved.z > 6.0f && std::fabs(boosted - 24.0f) < 0.2f && dirX && glide < 0.01f &&
                          hi == FlyCam::kMaxSpeed && w.speed == FlyCam::kMinSpeed;
        record("T245", "Free fly camera (Home): glides up to the set speed (0.12 s), Shift x3, yaw 90 flies along +x, stops smoothly, wheel speed clamped 1..60 m/s",
               "8 m/s after 1 s, 24 m/s boosted along +x, ~0 after release, wheel limits",
               fmt("cruise %.2f (moved %.2f m), boost %.2f along x %d, glide %.4f, wheel %.0f..%.0f", cruise, moved.z, boosted, dirX, glide, hi, w.speed),
               pass);
    }

    Scene& scene = dayScene();
    const Vec3 viewer{0.0f, 10.0f, 30.0f};
    const std::vector<PovSource> list = listPovSources(scene, viewer);
    // ---- T246 source list contents
    {
        int count[10] = {};
        std::set<uint32_t> keys;
        bool excluded = true;
        for (const PovSource& s : list) {
            ++count[static_cast<int>(s.kind)];
            keys.insert(s.key);
            excluded = excluded && !nameHas(s.name.c_str(), "DOOR") && !nameHas(s.name.c_str(), "BARRIER") && !nameHas(s.name.c_str(), "SKY");
        }
        const std::vector<PovSource> again = listPovSources(scene, viewer);
        bool stable = again.size() == list.size();
        for (size_t i = 0; stable && i < list.size(); ++i) stable = again[i].key == list[i].key;
        const int soldiers = count[static_cast<int>(PovKind::Soldier)], uavs = count[static_cast<int>(PovKind::Uav)];
        const bool pass = soldiers == 8 && uavs == 2 && count[static_cast<int>(PovKind::Tank)] == 1 && count[static_cast<int>(PovKind::Helicopter)] == 1 &&
                          count[static_cast<int>(PovKind::AaGun)] == 1 && count[static_cast<int>(PovKind::Launcher)] >= 2 &&
                          count[static_cast<int>(PovKind::Radar)] == 1 && keys.size() == list.size() && excluded && stable;
        record("T246", "POV sources: every moving object of the v6 scene (8 soldiers, both UAVs, tank, helicopter, AA gun, SAM + MLRS, radar); doors / barrier / sky clock left out; unique keys, stable order",
               "8 soldiers, 2 UAVs, 1 tank, 1 heli, 1 AA, >= 2 launchers, 1 radar, unique keys, same list twice",
               fmt("%zu sources: soldiers %d, UAVs %d, tank %d, heli %d, AA %d, launchers %d, radar %d, unique %d, excluded %d, stable %d", list.size(),
                   soldiers, uavs, count[static_cast<int>(PovKind::Tank)], count[static_cast<int>(PovKind::Helicopter)],
                   count[static_cast<int>(PovKind::AaGun)], count[static_cast<int>(PovKind::Launcher)], count[static_cast<int>(PovKind::Radar)],
                   keys.size() == list.size(), excluded, stable),
               pass);
    }
    // ---- T247 every POV valid
    {
        int checked = 0, bad = 0;
        std::string firstBad;
        for (const PovSource& s : list)
            for (int v = 0; v < 3; ++v) {
                RigPose p;
                const bool ok = povPose(scene, s, static_cast<PovView>(v), p);
                const float d = length(p.eye - s.centre);
                const float limit = v == 1 ? (s.kind == PovKind::Helicopter || s.kind == PovKind::Tank || s.kind == PovKind::Launcher || s.kind == PovKind::Radar ? 16.0f : 6.0f)
                                           : (s.kind == PovKind::Soldier || s.kind == PovKind::Rover ? 1.2f : 6.0f);
                const bool good = ok && std::isfinite(p.eye.x) && std::fabs(qlen(p.rot) - 1.0f) < 1e-3f && d < limit && p.fovDeg >= 20.0f &&
                                  p.fovDeg <= 100.0f && p.nearZ > 0.0f;
                ++checked;
                if (!good) {
                    ++bad;
                    if (firstBad.empty()) firstBad = fmt("%s view %d: ok %d d %.2f fov %.1f", s.name.c_str(), v, ok, d, p.fovDeg);
                }
            }
        record("T247", "Every POV source gives a valid camera for every view (first person, chase, alt): finite, unit orientation, close to the object, sane FOV and near plane",
               "0 bad poses",
               fmt("%d poses checked, %d bad%s%s", checked, bad, bad ? ": " : "", firstBad.c_str()), checked > 40 && bad == 0);
    }
    // ---- T248 picker order and layout
    {
        std::vector<PovSource> sorted = list;
        sortByDistance(sorted);
        bool ascending = true;
        for (size_t i = 1; i < sorted.size(); ++i) ascending = ascending && sorted[i - 1].distance <= sorted[i].distance;
        const int n = 40;
        const PickerLayout l = pickerLayout(1280, 720, n, 30);
        bool rowsOk = true;
        for (int r = 0; r < l.rows; ++r) {
            const float y = l.y + l.headerH + l.rowH * (static_cast<float>(r) + 0.5f);
            rowsOk = rowsOk && pickerRowAt(l, n, l.x + 10.0f, y) == l.firstRow + r;
        }
        const bool outside = pickerRowAt(l, n, l.x - 5.0f, l.y + l.headerH + 2.0f) == -1 && pickerRowAt(l, n, l.x + 10.0f, l.y + 2.0f) == -1;
        const bool visible = 30 >= l.firstRow && 30 < l.firstRow + l.rows && l.rows < n;
        record("T248", "POV picker: nearest first; row hit test maps every visible row to its list index, header / margins miss; a long list scrolls to keep the selection visible",
               "ascending distances, rows exact, outside -1, selection 30 of 40 visible",
               fmt("ascending %d (%zu entries, nearest %s %.1f m), rows %d (%d shown from %d), outside %d, visible %d", ascending, sorted.size(),
                   sorted.empty() ? "-" : sorted[0].name.c_str(), sorted.empty() ? 0.0f : sorted[0].distance, rowsOk, l.rows, l.firstRow, outside, visible),
               ascending && rowsOk && outside && visible);
    }
    // ---- T249 Insert / Delete cycling
    {
        std::set<uint32_t> seen;
        uint32_t key = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            const int k = cyclePov(list, key, +1);
            seen.insert(list[k].key);
            key = list[k].key;
        }
        const bool wrapFwd = cyclePov(list, list.back().key, +1) == 0;
        const bool wrapBack = cyclePov(list, list.front().key, -1) == static_cast<int>(list.size()) - 1;
        const bool fresh = cyclePov(list, 0xFFFFFFFFu, -1) == static_cast<int>(list.size()) - 1 && cyclePov({}, 0, 1) == -1;
        record("T249", "Insert / Delete cycle every moving object once in the stable order and wrap both ways",
               "all visited, wraps, unknown key starts at an end, empty list -1",
               fmt("visited %zu / %zu, wrap fwd %d back %d, fresh %d", seen.size(), list.size(), wrapFwd, wrapBack, fresh),
               seen.size() == list.size() && wrapFwd && wrapBack && fresh);
    }
    // ---- T250 middle-click ray pick
    {
        int hits = 0, tried = 0;
        for (const PovSource& s : list) {
            if (s.kind != PovKind::Soldier && s.kind != PovKind::Uav && s.kind != PovKind::Tank) continue;
            ++tried;
            // Look at it from 25 m away, up and to the side; a ray through the screen centre must pick it.
            const Vec3 eye = s.centre + Vec3{14.0f, 9.0f, 18.0f};
            const RigPose p = RigPose::look(eye, s.centre, 50.0f);
            const Mat4 vp = perspective(radians(50.0f), 16.0f / 9.0f, 0.1f, 600.0f) * p.view();
            Vec3 o, d;
            pixelRay(inverse(vp), 640.0f, 360.0f, 1280, 720, o, d);
            const int k = pickPov(list, o, d);
            if (k >= 0 && (list[k].key == s.key || length(list[k].centre - s.centre) < s.radius + list[k].radius)) ++hits;
        }
        // Two objects on one ray: the nearer one wins; a near miss still picks within 4 degrees.
        std::vector<PovSource> two(2);
        two[0].centre = {0, 1, -20};
        two[1].centre = {0, 1, -10};
        two[0].radius = two[1].radius = 0.5f;
        const bool nearer = pickPov(two, {0, 1, 0}, {0, 0, -1}) == 1;
        const std::vector<PovSource> one(two.begin() + 1, two.end());   // a single small target 10 m away
        const bool miss = pickPov(one, {0, 1, 0}, normalize(Vec3{0.06f, 0.0f, -1.0f})) == 0 && pickPov(two, {0, 1, 0}, {1, 0, 0}) == -1;
        Vec3 o, d;
        const RigPose c = RigPose::look({1, 2, 3}, {4, 1, -7}, 60.0f);
        pixelRay(inverse(perspective(radians(60.0f), 1.5f, 0.1f, 100.0f) * c.view()), 300.0f, 200.0f, 600, 400, o, d);
        const bool centreRay = length(d - c.forward()) < 1e-3f;
        record("T250", "Middle-click pick: the screen-centre ray picks the object the camera looks at (soldiers, UAVs, tank), the nearer of two on a ray wins, a near miss within 4 deg still picks",
               "all targets picked, nearer wins, near miss picks, 90 deg off picks none, centre ray = forward",
               fmt("%d / %d picked, nearer %d, miss %d, centre ray %d", hits, tried, nearer, miss, centreRay),
               tried >= 8 && hits == tried && nearer && miss && centreRay);
    }
    // ---- T251 soldier eyes with head bob; T252 soldier possession round trip
    {
        int idx = -1;
        for (int i = 0; i < 8 && idx < 0; ++i) {
            const int k = sa::soldierIndex(scene, i);
            if (k >= 0 && sa::controllables(scene)[k]->position().y > -0.5f) idx = k;
        }
        Controllable* c = idx >= 0 ? sa::controllables(scene)[idx] : nullptr;
        float minY = 1e9f, maxY = -1e9f, maxOff = 0.0f, maxAng = 0.0f;
        Vec3 start, end;
        if (c) {
            start = c->position();
            ControlInput in;
            in.forward = 1.0f;
            for (int i = 0; i < 150; ++i) {
                scene.update(1.0f / 60.0f, Detection{}, &in, idx);
                if (i < 40) continue;   // up to walking speed
                const RigPose e = soldierEyePose(scene, idx);
                const CameraHint h = c->camera(true);
                const float rel = e.eye.y - c->position().y;
                minY = std::min(minY, rel);
                maxY = std::max(maxY, rel);
                maxOff = std::max(maxOff, length(e.eye - h.eye));
                maxAng = std::max(maxAng, degrees(std::acos(clampf(dot(e.forward(), normalize(h.target - h.eye)), -1.0f, 1.0f))));
            }
            end = c->position();
        }
        const float bob = maxY - minY;
        record("T251", "Soldier first person: the eyes ride in the head node (gait bob and sway), next to the soldier's own eye point, looking where he looks",
               "bob 0.5..10 cm while walking, eye within 30 cm of camera(true), direction within 1 deg",
               fmt("soldier %d, bob %.3f m, eye offset %.3f m, angle %.2f deg", idx, bob, maxOff, maxAng),
               c && bob > 0.005f && bob < 0.10f && maxOff < 0.30f && maxAng < 1.0f);
        // Release: the soldier is no longer driven, and possession works again.
        float moved = length(end - start);
        bool again = false;
        if (c) {
            for (int i = 0; i < 120; ++i) scene.update(1.0f / 60.0f, Detection{});
            ControlInput turn;
            turn.turn = 1.0f;
            const float yaw0 = dynamic_cast<Soldier*>(c)->yawDeg();
            for (int i = 0; i < 20; ++i) scene.update(1.0f / 60.0f, Detection{}, &turn, idx);
            again = std::fabs(dynamic_cast<Soldier*>(c)->yawDeg() - yaw0) > 5.0f;
        }
        record("T252", "Soldier possession round trip: W walks him (~1.5 m in 1.8 s), release hands him back to his duty program, a second possession turns him again",
               "moved > 1 m while possessed, turned again after release",
               fmt("moved %.2f m, possessable again %d", moved, again), c && moved > 1.0f && again);
    }
    // ---- T253 rover: take the wheel, drive, release
    {
        Scene& s = scene;
        s.setMissionEnabled(false);
        const int slot = s.spawnIntruder(Vec3{9.0f, 0.0f, -9.0f}, 0.0f, Vec3{0.0f, 0.0f, 0.0f}, 77u);
        bool listed = false;
        float moved = 0.0f, yawTurn = 0.0f, aiMoved = 0.0f;
        bool alive = false, stillTarget = false;
        if (slot >= 0) {
            for (const PovSource& p : listPovSources(s, viewer)) listed = listed || (p.kind == PovKind::Rover && p.intruder == slot && p.canControl);
            const RoverState& rs = s.intruders()[slot]->state;
            const Vec3 p0 = rs.position;
            ControlInput in;
            in.forward = 1.0f;
            for (int i = 0; i < 120; ++i) {
                s.setPlayerIntruder(slot, &in);
                s.update(1.0f / 60.0f, Detection{});
            }
            moved = length(Vec3{rs.position.x - p0.x, 0.0f, rs.position.z - p0.z});
            const float y0 = rs.yaw;
            ControlInput right;
            right.turn = 1.0f;
            for (int i = 0; i < 30; ++i) {
                s.setPlayerIntruder(slot, &right);
                s.update(1.0f / 60.0f, Detection{});
            }
            yawTurn = rs.yaw - y0;
            s.setPlayerIntruder(-1, nullptr);
            const Vec3 p1 = rs.position;
            for (int i = 0; i < 240; ++i) s.update(1.0f / 60.0f, Detection{});
            aiMoved = length(Vec3{rs.position.x - p1.x, 0.0f, rs.position.z - p1.z});
            alive = s.roverAlive(slot);
            for (const Vec3& r : sa::roverPositions(s)) stillTarget = stillTarget || length(r - rs.position) < 1e-3f;
        }
        const bool pass = slot >= 0 && listed && moved > 1.0f && yawTurn < -0.3f && aiMoved > 0.2f && alive && stillTarget && s.playerIntruder() == -1;
        record("T253", "Intruder rover round trip: take the wheel (planner paused), W drives ~1.1 m/s, D turns right (yaw down), release gives it back to its planner; still a rover on the field (the UAVs hunt it)",
               "listed + controllable, > 1 m in 2 s, yaw < -0.3 rad, planner moves it again, alive",
               fmt("slot %d, listed %d, moved %.2f m, yaw %.2f rad, AI moved %.2f m, alive %d, on field %d, player slot %d", slot, listed, moved, yawTurn,
                   aiMoved, alive, stillTarget, s.playerIntruder()),
               pass);
    }
    // ---- T254 UAV possession round trip
    {
        const int uav = controllableNamed(scene, "UAV");
        float climb = 0.0f;
        bool manualOn = false, manualOff = true;
        if (uav >= 0) {
            const float y0 = scene.droneState(0).position.y;
            ControlInput in;
            in.lift = 1.0f;
            for (int i = 0; i < 90; ++i) scene.update(1.0f / 60.0f, Detection{}, &in, uav);
            manualOn = scene.droneState(0).manual;
            climb = scene.droneState(0).position.y - y0;
            for (int i = 0; i < 10; ++i) scene.update(1.0f / 60.0f, Detection{});
            manualOff = !scene.droneState(0).manual;
        }
        record("T254", "UAV possession round trip: possessed it flies by hand (PgUp climbs), released it returns to the squad's autonomous control",
               "manual while possessed, climbs > 0.3 m in 1.5 s, autonomous again",
               fmt("index %d, manual %d, climb %.2f m, released %d", uav, manualOn, climb, manualOff), uav >= 0 && manualOn && climb > 0.3f && manualOff);
    }
    // ---- T255 rover drive kinematics + field edge
    {
        RoverState r;
        r.position = {0.0f, 0.0f, 17.5f};
        r.yaw = 0.0f;   // heading +z: out of the field
        ControlInput in;
        in.forward = 1.0f;
        in.boost = true;
        float maxR = 0.0f;
        for (int i = 0; i < 600; ++i) {
            driveRover(r, in, 1.0f / 60.0f, nullptr);
            maxR = std::max(maxR, std::sqrt(r.position.x * r.position.x + r.position.z * r.position.z));
        }
        RoverState q;
        q.position = {0.0f, 0.0f, 0.0f};
        ControlInput go;
        go.forward = 1.0f;
        for (int i = 0; i < 120; ++i) driveRover(q, go, 1.0f / 60.0f, nullptr);
        const float speed = 0.5f * (q.leftSpeed + q.rightSpeed);
        const bool wheels = q.leftWheelAngle > 0.0f && q.moving;
        record("T255", "Player rover drive: differential drive reaches 1.1 m/s (1.8 boosted), wheels turn, the field edge (19 m) stops it",
               "speed 1.1 m/s, wheels turning, never beyond 19 m",
               fmt("speed %.2f m/s, wheels %d, max radius %.2f m", speed, wheels, maxR), std::fabs(speed - kRoverDriveSpeed) < 0.02f && wheels && maxR <= 19.0f);
    }
    // ---- T256 projectile follow until impact
    {
        Combat c;
        c.setObstacles({{{-5.0f, 0.0f, 28.0f}, {5.0f, 10.0f, 30.0f}}});   // a wall 28 m ahead
        c.update(1.0f / 60.0f, 1.0f);
        c.fireMissile({0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -1, 0);
        ProjectileFollow f;
        f.key = projectileKey(c.projectiles()[0], c.time());
        f.kind = static_cast<int>(ProjectileKind::Missile);
        f.owner = 0;
        f.position = c.projectiles()[0].position;
        f.velocity = c.projectiles()[0].velocity;
        const uint32_t key0 = f.key;
        bool keyStable = true;
        int frames = 0;
        float t = 1.0f;
        while (frames < 600) {
            t += 1.0f / 60.0f;
            c.update(1.0f / 60.0f, t);
            const int idx = f.track(c.projectiles(), c.time(), 1.0f / 60.0f);
            if (idx < 0) break;
            keyStable = keyStable && f.key == key0;
            ++frames;
        }
        const bool lost = f.lost && f.impact.z > 24.0f && f.impact.z < 30.5f;
        record("T256", "Round in flight: the follow camera keeps the same missile frame after frame (stable launch-time key) and holds on its impact point when it detonates",
               "same key every frame, lost at the wall (z 24..30 m)",
               fmt("%d frames, key stable %d, lost %d, impact z %.2f", frames, keyStable, f.lost, f.impact.z), keyStable && lost && frames > 10);
    }
    // ---- T257 v7 keys in the help / key table
    {
        bool f4 = false, home = false, ins = false, f11 = false;
        for (const HelpLine& l : helpLines()) {
            const std::string t = l.text;
            f4 = f4 || t.find("F4") != std::string::npos;
            home = home || t.find("HOME") != std::string::npos;
            ins = ins || t.find("INS") != std::string::npos;
            f11 = f11 || t.find("F11") != std::string::npos;
        }
        bool allV7 = keyBindingsV7().size() >= 8;
        for (const KeyBinding& k : keyBindingsV7()) allV7 = allV7 && std::string(k.version) == "v7";
        record("T257", "F1 help and the key table list the v7 game camera keys (F4, Home / End, Insert / Delete, F11)",
               "all four in the help, >= 8 v7 key rows", fmt("F4 %d HOME %d INS %d F11 %d, table %d (%zu rows)", f4, home, ins, f11, allV7, keyBindingsV7().size()),
               f4 && home && ins && f11 && allV7);
    }
    // ---- T258 wing UAV / rover alt views look the right way
    {
        bool uavDown = false, gimbalMatch = false, roverFwd = false;
        for (const PovSource& s : listPovSources(scene, viewer)) {
            RigPose p;
            if (s.kind == PovKind::Uav && s.uav == 1) {
                if (povPose(scene, s, PovView::Alt, p)) uavDown = p.forward().y < -0.8f;
                if (povPose(scene, s, PovView::First, p)) {
                    const Vec3 camFwd = normalize(transformDirection(scene.cameraWorld(1), {0, 0, 1}));
                    gimbalMatch = length(p.forward() - camFwd) < 1e-3f;
                }
            }
            if (s.kind == PovKind::Rover && povPose(scene, s, PovView::First, p)) {
                const float yaw = scene.intruders()[s.intruder]->state.yaw;
                roverFwd = dot(p.forward(), Vec3{std::sin(yaw), 0.0f, std::cos(yaw)}) > 0.9f;
            }
        }
        record("T258", "Special cameras: the wing UAV's first person is its thermal gimbal axis, its belly camera looks down, the rover's bumper camera looks along its heading",
               "gimbal axis match, belly forward.y < -0.8, bumper along the heading", fmt("gimbal %d, belly %d, bumper %d", gimbalMatch, uavDown, roverFwd),
               gimbalMatch && uavDown && roverFwd);
    }
}
