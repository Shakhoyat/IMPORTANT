// v9 M5 (UAV game view and duty): T360-T369. CPU only. The UAV cameras are replaced by the synthetic camera of
// tests/test_squad.cpp (every live, unoccluded rover projected into the image of each UAV whose vision is active).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "TestKit.h"
#include "ai/Squad.h"
#include "objects/Terrain.h"
#include "objects/Uav.h"
#include "objects/UavPad.h"
#include "scene/Scene.h"
#include "scene/UavView.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

constexpr float kDt = 1.0f / 30.0f;

std::vector<Measurement2D> blobsFor(const Scene& s, int uav) {
    std::vector<Measurement2D> out;
    if (!s.uavVisionActive(uav)) return out;   // the app renders no image for a docked UAV
    const Mat4 inv = inverse(s.cameraWorld(uav));
    const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * static_cast<float>(kCvWidth) / kCvHeight;
    for (const auto& in : s.intruders()) {
        if (in->life != Intruder::Life::Alive) continue;
        const Vec3 l = transformPoint(inv, in->state.position + Vec3{0, 0.2f, 0});
        if (l.z < 0.5f) continue;
        if (!s.squad().clearShot(transformPoint(s.cameraWorld(uav), {0, 0, 0}), in->state.position + Vec3{0, 0.25f, 0})) continue;
        Measurement2D m;
        m.u = -(l.x / l.z) / tx;
        m.v = (l.y / l.z) / ty;
        if (std::fabs(m.u) > 0.98f || std::fabs(m.v) > 0.98f) continue;
        m.area = 12;
        m.peak = 255;
        out.push_back(m);
    }
    return out;
}

void stepVision(Scene& s, const ControlInput* in = nullptr, int possessed = -1) {
    for (int u = 0; u < kUavCount; ++u)
        if (s.uavVisionActive(u)) s.submitVision(u, blobsFor(s, u), s.cameraWorld(u));
    s.update(kDt, Detection{}, in, possessed);
}

struct DutyChange {
    float t, hour;
    int uav;
    std::string from, to;
};

// Runs the scene and records every duty change (and a compact log line per second for determinism checks).
void runDuty(Scene& s, float seconds, std::vector<DutyChange>& changes, std::string* log = nullptr) {
    std::string prev[kUavCount] = {s.uavDutyState(0), s.uavDutyState(1)};
    for (float t = 0.0f; t < seconds; t += kDt) {
        stepVision(s);
        for (int u = 0; u < kUavCount; ++u) {
            const std::string now = s.uavDutyState(u);
            if (now != prev[u]) changes.push_back({s.time(), s.dayCycle().hour(), u, prev[u], now});
            prev[u] = now;
        }
        if (log && static_cast<int>(s.time() * 30.0f + 0.5f) % 30 == 0) {
            *log += fmt("%.2f %s %s %.4f %.4f %.4f|", s.time(), s.uavDutyState(0), s.uavDutyState(1), s.droneState(0).position.x,
                        s.droneState(1).position.y, s.uavRotorSpool(1));
            for (const auto& in : s.intruders())
                if (in->life == Intruder::Life::Alive) *log += fmt("%.4f,%.4f;", in->state.position.x, in->state.position.z);
            *log += "\n";
        }
    }
}

// Ray against an axis-aligned box inflated by r: entry distance, -1 for a miss or an origin inside (the subject).
float castBox(const Box& b, const Vec3& o, const Vec3& d, float r) {
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const float lo[3] = {b.min.x - r, b.min.y - r, b.min.z - r}, hi[3] = {b.max.x + r, b.max.y + r, b.max.z + r};
    float t0 = 0.0f, t1 = 1e9f;
    bool inside = true;
    for (int k = 0; k < 3; ++k) {
        if (oo[k] < lo[k] || oo[k] > hi[k]) inside = false;
        if (std::fabs(dd[k]) < 1e-7f) {
            if (oo[k] < lo[k] || oo[k] > hi[k]) return -1.0f;
            continue;
        }
        float a = (lo[k] - oo[k]) / dd[k], c = (hi[k] - oo[k]) / dd[k];
        if (a > c) std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1) return -1.0f;
    }
    return inside ? -1.0f : t0;
}

float boxDistance(const Box& b, const Vec3& p) {
    const float dx = std::max({b.min.x - p.x, 0.0f, p.x - b.max.x});
    const float dy = std::max({b.min.y - p.y, 0.0f, p.y - b.max.y});
    const float dz = std::max({b.min.z - p.z, 0.0f, p.z - b.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
float rnd(uint32_t& s) { return static_cast<float>(lcg(s) >> 8) / 16777216.0f; }

}  // namespace

TS_TEST_GROUP(V9M5) {
    // ---- T360: duty state machine over a full simulated day (no day ops) ---------------------------------------------
    {
        SceneOptions o = SceneOptions::v6(5, 12.0f, true);
        o.garrison = false;
        Scene s(o);
        const bool dockedAtNoon = std::string(s.uavDutyState(0)) == "DOCKED" && std::string(s.uavDutyState(1)) == "DOCKED";
        // By day: both stay on their pads (rotors still, no vision) until dusk.
        bool dayOk = true, nightAirborne = false;
        float liftOff[kUavCount] = {-1.0f, -1.0f}, maxClimb = 0.0f, touchSpeed = 0.0f, lastH[kUavCount] = {0.0f, 0.0f};
        std::vector<DutyChange> changes;
        std::string prev[kUavCount] = {s.uavDutyState(0), s.uavDutyState(1)};
        // One full day: noon -> dusk -> night -> dawn -> 17:00 the next day (at most 900 s of simulation). At dawn the UAVs
        // stay up until the last rover has left the field (withdrawn, or released after kRetreatTimeout).
        bool pastMidnight = false;
        float clearT = -1.0f, clearHour = 0.0f;   // first tick after dawn with no searchlights and no rover on the field
        for (float t = 0.0f; t < 900.0f; t += kDt) {
            stepVision(s);
            const float h = s.dayCycle().hour();
            if (h < 3.0f) pastMidnight = true;
            if (pastMidnight && h > 17.0f && h < 18.0f) break;
            if (pastMidnight && clearT < 0.0f && !s.dayRules().beamsOn && s.aliveIntruders() == 0) {
                clearT = s.time();
                clearHour = h;
            }
            for (int u = 0; u < kUavCount; ++u) {
                const std::string now = s.uavDutyState(u);
                if (now != prev[u]) changes.push_back({s.time(), h, u, prev[u], now});
                prev[u] = now;
                if (!pastMidnight && h > 12.0f && h < 16.5f && (now != "DOCKED" || s.uavAirborne(u) || s.uavRotorSpool(u) > 0.0f || s.uavVisionActive(u)))
                    dayOk = false;
                const float hp = s.squad().heightAbovePad(u), vy = (hp - lastH[u]) / kDt;
                if (liftOff[u] < 0.0f && now == "TAKE-OFF" && hp > 0.02f) liftOff[u] = s.time();
                if (now == "TAKE-OFF") maxClimb = std::max(maxClimb, vy);
                if (now == "LANDING" && hp < 0.1f && lastH[u] > 0.0f) touchSpeed = std::max(touchSpeed, -vy);
                lastH[u] = hp;
            }
            if (h > 0.5f && h < 1.0f)
                nightAirborne = nightAirborne || (std::string(s.uavDutyState(0)) == "PATROL" && std::string(s.uavDutyState(1)) == "PATROL" &&
                                                  s.squad().heightAbovePad(0) > 3.0f && s.squad().heightAbovePad(1) > 3.0f);
        }
        // Each UAV: DOCKED -> TAKE-OFF -> PATROL (dusk) ... PATROL -> RETURN -> LANDING -> DOCKED (dawn), in order.
        const std::vector<std::string> want = {"DOCKED>TAKE-OFF", "TAKE-OFF>PATROL", "PATROL>RETURN", "RETURN>LANDING", "LANDING>DOCKED"};
        bool order = true, duskOk = true, dawnOk = true;
        float takeOff[2] = {-1.0f, -1.0f};
        std::string seq[2];
        for (int u = 0; u < kUavCount; ++u) {
            std::vector<std::string> got;
            for (const DutyChange& c : changes)
                if (c.uav == u) {
                    got.push_back(c.from + ">" + c.to);
                    seq[u] += c.to.substr(0, 1);
                    if (c.to == "TAKE-OFF") {
                        if (takeOff[u] < 0.0f) takeOff[u] = c.t;
                        duskOk = duskOk && c.hour > 17.0f && c.hour < 19.5f;
                    }
                    if (c.to == "RETURN") dawnOk = dawnOk && clearT >= 0.0f && c.t >= clearT - 1e-3f && c.t - clearT < 0.1f;
                    if (c.to == "DOCKED") dawnOk = dawnOk && c.hour > 4.5f && c.hour < 17.0f;
                }
            order = order && got == want;
        }
        const bool docked = std::string(s.uavDutyState(0)) == "DOCKED" && std::string(s.uavDutyState(1)) == "DOCKED" &&
                            length(s.droneState(0).position - s.squad().pad(0).touchdown) < 1e-3f &&
                            length(s.droneState(1).position - s.squad().pad(1).touchdown) < 1e-3f;
        record("T360", "Duty FSM over a full day (no day ops): docked by day, take-off at dusk, patrol at night, return / land at dawn",
               "noon: both DOCKED (rotors still, no vision); per UAV exactly DOCKED>TAKE-OFF>PATROL>RETURN>LANDING>DOCKED; take-off "
               "17-19:30; RETURN on the first tick after dawn with no rover left, docked again by day; both PATROL > 3 m at 00:30; "
               "back on the pads at 17:00",
               fmt("noon docked %d, day ok %d, sequences %s / %s, take-off %.1f / %.1f s, dusk ok %d, dawn ok %d, night airborne %d, "
                   "field clear %.2f h, end docked %d at %.2f h after %.0f s, %zu changes", dockedAtNoon ? 1 : 0, dayOk ? 1 : 0, seq[0].c_str(), seq[1].c_str(),
                   takeOff[0], takeOff[1], duskOk ? 1 : 0, dawnOk ? 1 : 0, nightAirborne ? 1 : 0, clearHour, docked ? 1 : 0, s.dayCycle().hour(),
                   s.time(), changes.size()),
               dockedAtNoon && dayOk && order && duskOk && dawnOk && nightAirborne && docked);
        // ---- T361: take-off / landing kinematics on the same day: stagger, spool, climb and touch-down speeds -------
        record("T361", "Take-off stagger, spool-up and smooth landing over the same day",
               "lift-off >= kRotorSpoolUp (2 s) after TAKE-OFF; UAV-2 lifts off >= 2.5 s after UAV-1; climb <= kMaxClimb (1.6 m/s); "
               "touch-down <= kTouchdownSpeed (0.25 m/s) in the last 10 cm",
               fmt("TAKE-OFF at %.2f / %.2f s, lift-off %.2f / %.2f s (gap %.2f s), max climb %.2f m/s, touch-down %.3f m/s", takeOff[0],
                   takeOff[1], liftOff[0], liftOff[1], liftOff[1] - liftOff[0], maxClimb, touchSpeed),
               takeOff[0] > 0.0f && liftOff[0] - takeOff[0] >= kRotorSpoolUp - 0.05f &&
                   liftOff[1] - liftOff[0] >= kTakeOffStagger - 0.05f && maxClimb <= kMaxClimb + 1e-3f && touchSpeed > 0.0f &&
                   touchSpeed <= kTouchdownSpeed + 1e-3f);
    }

    // ---- T362: rotor blur and docked look: blade fades out, disc fades in with the rotor speed; pads ------------------
    {
        SceneOptions o = SceneOptions::v6(3, 10.5f, false);
        o.garrison = false;
        Scene s(o);
        s.update(kDt, Detection{});
        const UavRig& rig = s.uav(0);
        const bool still = rig.blades[0]->visible && rig.blades[0]->material.opacity >= 1.0f && !rig.blurDiscs[0]->visible &&
                           !rig.beam->visible && !s.uavVisionActive(0);
        // Spin up through day ops; sample blade / disc opacity against the spool.
        s.setDayOps(true);
        bool monotone = true, mid = false;
        float lastDisc = -1.0f;
        for (int i = 0; i < 90; ++i) {
            s.update(kDt, Detection{});
            const float disc = rig.blurDiscs[0]->visible ? rig.blurDiscs[0]->material.opacity : 0.0f;
            const float blade = rig.blades[0]->visible ? rig.blades[0]->material.opacity : 0.0f;
            monotone = monotone && disc >= lastDisc - 1e-5f;
            lastDisc = disc;
            mid = mid || (disc > 0.05f && blade > 0.05f && blade < 0.95f);
        }
        const bool full = s.uavRotorSpool(0) >= 1.0f && !rig.blades[0]->visible &&
                          std::fabs(rig.blurDiscs[0]->material.opacity - kRotorDiscOpacity) < 1e-5f;
        const bool curve = rotorBlur(0.0f) == 0.0f && rotorBlur(kRotorBlurStartRps) == 0.0f && rotorBlur(kRotorBlurFullRps) == 1.0f &&
                           rotorBlur(kRotorFullRps) == 1.0f && rotorBlur(15.0f) > 0.3f && rotorBlur(15.0f) < 0.7f;
        // Pads: flush (no obstacle), 2 pads, touchdown = slab + rail + gear drop above the terrain.
        const UavPadRig pads = buildUavPads();
        const float g = terrainHeight(kUavPadX[0], kUavPadZ[0]);
        const bool padOk = std::fabs(pads.touchdown[0].y - (g + kUavPadSlab + kUavPadRail + kUavGearDrop)) < 1e-5f;
        record("T362", "Rotor blur: still blades on the pad; spinning up the blade fades out while the blur disc fades in",
               "docked: blades opaque, no disc / beam / vision; disc opacity rises monotonically, a mixed phase exists, at full "
               "speed only the disc (kRotorDiscOpacity); blur curve 0 below 8, 1 above 22 rev/s; pad touchdown on the rails",
               fmt("still %d, monotone %d, mixed phase %d, full %d (spool %.2f), curve %d, pad %d", still ? 1 : 0, monotone ? 1 : 0,
                   mid ? 1 : 0, full ? 1 : 0, s.uavRotorSpool(0), curve ? 1 : 0, padOk ? 1 : 0),
               still && monotone && mid && full && curve && padOk);
    }

    // ---- T363: day ops: daytime rover waves, the UAVs scramble and the squad tracker confirms a target by day ---------
    {
        SceneOptions o = SceneOptions::v6(11, 10.5f, false);
        o.garrison = false;
        Scene s(o);
        const bool quiet = !s.dayRules().wavesAllowed && s.aliveIntruders() == 0;
        for (int i = 0; i < 300; ++i) stepVision(s);   // 10 s of a quiet day
        const bool stillQuiet = s.aliveIntruders() == 0 && std::string(s.uavDutyState(0)) == "DOCKED";
        s.setDayOps(true);
        std::vector<DutyChange> changes;
        int confirmedAt = -1, spawned = 0;
        float hourAtConfirm = 0.0f;
        for (int i = 0; i < 30 * 120 && confirmedAt < 0; ++i) {
            stepVision(s);
            spawned = std::max(spawned, s.aliveIntruders());
            for (const Track& t : s.squad().tracker().tracks())
                if (t.state != TrackState::Tentative && confirmedAt < 0) {
                    confirmedAt = i;
                    hourAtConfirm = s.dayCycle().hour();
                }
        }
        const bool scrambled = s.uavAirborne(0) && s.uavAirborne(1);
        const bool daylight = s.dayRules().daylight > 0.9f && !s.dayRules().beamsOn;
        record("T363", "Day ops: rover waves by day, the UAVs scramble, the squad tracker confirms a target in daylight",
               "before: no waves / rovers, UAVs docked; with day ops: rovers spawn, both UAVs airborne, a confirmed track within 120 s, "
               "daylight > 0.9 (searchlights off)",
               fmt("quiet %d/%d, rovers %d, both airborne %d, confirmed after %.1f s at %.2f h, daylight %d", quiet ? 1 : 0,
                   stillQuiet ? 1 : 0, spawned, scrambled ? 1 : 0, confirmedAt * kDt, hourAtConfirm, daylight ? 1 : 0),
               quiet && stillQuiet && spawned > 0 && scrambled && confirmedAt >= 0 && daylight);
    }

    // ---- T364: determinism: same seed -> identical duty / rover log (day ops switched on and off mid-run) -------------
    {
        auto runLog = [](uint32_t seed) {
            SceneOptions o = SceneOptions::v6(seed, 10.0f, false);
            o.garrison = false;
            Scene s(o);
            std::vector<DutyChange> ch;
            std::string log;
            runDuty(s, 5.0f, ch, &log);
            s.setDayOps(true);
            runDuty(s, 40.0f, ch, &log);
            s.setDayOps(false);
            runDuty(s, 60.0f, ch, &log);
            for (const DutyChange& c : ch) log += fmt("%.4f %d %s>%s\n", c.t, c.uav, c.from.c_str(), c.to.c_str());
            return log;
        };
        const std::string a = runLog(21), b = runLog(21), c = runLog(22);
        record("T364", "Determinism under --seed: duty states, UAV positions and rover positions per tick",
               "two runs with seed 21 identical (day ops on at 5 s, off at 45 s); seed 22 differs",
               fmt("log %zu bytes, same seed identical %d, other seed differs %d", a.size(), a == b ? 1 : 0, a != c ? 1 : 0),
               !a.empty() && a == b && a != c);
    }

    // ---- T365: chase camera framing: the drone on the lower third, no overshoot of the springs ------------------------
    {
        UavChaseParams p;
        UavChaseCam cam;
        UavChaseInput in;
        in.position = {2.0f, 6.0f, -3.0f};
        in.yawDeg = 30.0f;
        RigPose pose;
        for (int i = 0; i < 180; ++i) pose = cam.update(in, 1.0f / 60.0f, p, nullptr);
        float x = 0.0f, y = 0.0f;
        const bool front = poseNdc(pose, 16.0f / 9.0f, in.position, x, y);
        // Spring: unit step from 1 to 0 never crosses 0 (critically damped), 1 % left after 0.66 s at omega 10.
        float sx = 1.0f, sv = 0.0f, minX = 1.0f, at066 = 0.0f;
        for (int i = 1; i <= 120; ++i) {
            springStep(sx, sv, 0.0f, 10.0f, 1.0f / 60.0f);
            minX = std::min(minX, sx);
            if (i == 40) at066 = sx;
        }
        // Flying at 6 m/s: the camera trails, the distance to the drone settles without oscillation.
        UavChaseCam cam2;
        UavChaseInput mv = in;
        mv.velocity = {std::sin(radians(30.0f)) * 6.0f, 0.0f, std::cos(radians(30.0f)) * 6.0f};
        float lastD = 0.0f, maxD = 0.0f;
        int dirChanges = 0;
        float prevDelta = 0.0f;
        for (int i = 0; i < 240; ++i) {
            mv.position = mv.position + mv.velocity * (1.0f / 60.0f);
            const RigPose q = cam2.update(mv, 1.0f / 60.0f, p, nullptr);
            const float d = length(q.eye - mv.position);
            const float delta = d - lastD;
            if (i > 2 && delta * prevDelta < -1e-10f && std::fabs(delta) > 1e-5f) ++dirChanges;
            if (i > 0) prevDelta = delta;
            lastD = d;
            maxD = std::max(maxD, d);
        }
        record("T365", "Chase camera: the hovering drone sits on the lower third of the frame; critically damped follow",
               "drone NDC y = -framing (-0.40, inside the lower third < -1/3) +- 0.01, x ~ 0; spring step never < 0, ~1.5 % at 0.66 s; "
               "camera distance in steady flight rises monotonically (no oscillation)",
               fmt("ndc (%.3f, %.3f) front %d, spring min %.5f at0.66 %.4f, flight distance %.2f m (max %.2f), direction changes %d", x, y,
                   front ? 1 : 0, minX, at066, lastD, maxD, dirChanges),
               front && std::fabs(y + p.framing) < 0.01f && y < -1.0f / 3.0f && std::fabs(x) < 0.01f && minX >= 0.0f && at066 < 0.02f &&
                   dirChanges == 0);
    }

    // ---- T366: chase camera collision: no penetration of solids or the ground over 500 random poses -------------------
    {
        UavChaseParams p;
        uint32_t seed = 12345u;
        int worstCase = 0, pulled = 0;
        float worst = 1e9f, worstNear = 1e9f;   // near: boxes whose probe-inflated bounds already hold the pivot
        for (int k = 0; k < 500; ++k) {
            std::vector<Box> boxes;
            const Vec3 drone{rnd(seed) * 4.0f - 2.0f, 0.6f + rnd(seed) * 4.0f, rnd(seed) * 4.0f - 2.0f};
            const Vec3 vel{rnd(seed) * 4.0f - 2.0f, 0.0f, rnd(seed) * 4.0f - 2.0f};   // 0.5 s of flight below
            const Vec3 end = drone + vel * 0.5f;
            for (int j = 0; j < 4; ++j) {   // walls / masts / huts around the drone
                const Vec3 c{drone.x + rnd(seed) * 6.0f - 3.0f, rnd(seed) * 3.0f, drone.z + rnd(seed) * 6.0f - 3.0f};
                const Vec3 h{0.1f + rnd(seed) * 1.5f, 0.2f + rnd(seed) * 2.5f, 0.1f + rnd(seed) * 1.5f};
                Box b{c - h, c + h};
                if (boxDistance(b, drone) < 0.45f || boxDistance(b, end) < 0.45f || boxDistance(b, (drone + end) * 0.5f) < 0.45f)
                    continue;   // the drone never flies into a solid (its own avoidance keeps it > 0.9 m away)
                boxes.push_back(b);
            }
            const ArmCast cast = [&](const Vec3& o, const Vec3& d, float len, float r) {
                float t = len;
                if (d.y < -1e-6f) t = std::min(t, (o.y - r) / -d.y);   // ground plane y = 0
                for (const Box& b : boxes) {
                    const float hit = castBox(b, o, d, r);
                    if (hit >= 0.0f) t = std::min(t, hit);
                }
                return std::max(0.0f, t);
            };
            UavChaseCam cam;
            UavChaseInput in;
            in.position = drone;
            in.yawDeg = rnd(seed) * 360.0f - 180.0f;
            in.velocity = vel;
            RigPose pose;
            for (int i = 0; i < 30; ++i) {
                in.position = in.position + in.velocity * (1.0f / 60.0f);
                pose = cam.update(in, 1.0f / 60.0f, p, cast);
                float clearance = pose.eye.y;   // above the ground
                const Vec3 pivot = in.position + Vec3{0.0f, p.pivotLift, 0.0f};
                for (const Box& b : boxes) {
                    const Box inflated{b.min - Vec3{p.probe, p.probe, p.probe}, b.max + Vec3{p.probe, p.probe, p.probe}};
                    if (boxDistance(inflated, pivot) > 0.0f) clearance = std::min(clearance, boxDistance(b, pose.eye));
                    else worstNear = std::min(worstNear, boxDistance(b, pose.eye));   // the probe starts touching it
                }
                if (clearance < worst) {
                    worst = clearance;
                    worstCase = k;
                }
            }
            if (cam.arm() < cam.wantedArm() - 0.05f) ++pulled;
        }
        record("T366", "Chase camera collision: probe sphere never enters a solid or the ground (500 random poses with walls)",
               "eye clearance from every box / the ground >= probe radius (0.25 m) - 1 mm (a box the probe already touches at the pivot: >= the thin "
               "probe 0.08 m); some arms pulled in",
               fmt("worst clearance %.4f m (case %d), touching boxes %.4f m, arms pulled in %d / 500", worst, worstCase,
                   worstNear > 1e8f ? -1.0f : worstNear, pulled),
               worst >= p.probe - 1e-3f && worstNear >= p.probeMin - 1e-3f && pulled > 0);
    }

    // ---- T367: chase camera banks into a turn and pitches with the acceleration -----------------------------------------
    {
        UavChaseParams p;
        UavChaseCam cam;
        UavChaseInput in;
        in.position = {0.0f, 6.0f, 0.0f};
        in.yawDeg = 0.0f;
        RigPose pose;
        for (int i = 0; i < 120; ++i) {   // right turn: the yaw falls at 40 deg/s (D), flying at 4 m/s
            in.yawDeg -= 40.0f / 60.0f;
            in.velocity = Vec3{std::sin(radians(in.yawDeg)), 0.0f, std::cos(radians(in.yawDeg))} * 4.0f;
            in.position = in.position + in.velocity * (1.0f / 60.0f);
            pose = cam.update(in, 1.0f / 60.0f, p, nullptr);
        }
        float hdg = 0.0f, pitch = 0.0f, roll = 0.0f;
        poseAttitude(pose, hdg, pitch, roll);
        const float bankRight = cam.bankDeg();
        UavChaseCam cam2;
        UavChaseInput acc;
        acc.position = {0.0f, 6.0f, 0.0f};
        float tiltAccel = 0.0f;
        for (int i = 0; i < 60; ++i) {   // accelerating forward at 3 m/s^2
            acc.velocity = acc.velocity + Vec3{0.0f, 0.0f, 3.0f / 60.0f};
            acc.position = acc.position + acc.velocity * (1.0f / 60.0f);
            cam2.update(acc, 1.0f / 60.0f, p, nullptr);
            tiltAccel = cam2.tiltDeg();
        }
        record("T367", "Chase camera banks into a right turn (right side down) and pitches down while the drone accelerates",
               "40 deg/s right turn: bank -> 0.30 * 40 = 12 deg (8..15), pose roll positive and equal to the bank +- 1 deg; 3 m/s^2: tilt > 3 deg",
               fmt("bank %.2f deg, pose roll %.2f deg, tilt %.2f deg", bankRight, roll, tiltAccel),
               bankRight > 8.0f && bankRight <= 15.0f && std::fabs(roll - bankRight) < 1.0f && tiltAccel > 3.0f);
    }

    // ---- T368: FPV / gimbal poses, the view cycle and the HUD projections ------------------------------------------------
    {
        const Mat4 body = translate({1.0f, 5.0f, 2.0f}) * rotateEulerYXZ({8.0f, 40.0f, 12.0f});   // pitch, yaw, roll
        const RigPose fpv = uavFpvPose(body);
        const Vec3 bf = normalize(transformDirection(body, {0, 0, 1})), bu = normalize(transformDirection(body, {0, 1, 0}));
        const float upTilt = degrees(std::asin(clampf(dot(fpv.forward(), bu), -1.0f, 1.0f)));
        const bool fpvOk = std::fabs(upTilt - kFpvUptiltDeg) < 0.05f && dot(fpv.forward(), bf) > 0.96f &&
                           std::fabs(dot(fpv.right(), bf)) < 1e-3f && fpv.fovDeg == kFpvFovDeg;
        const Mat4 g = translate({0.0f, 4.0f, 0.0f}) * rotateEulerYXZ({50.0f, 10.0f, 0.0f});
        const RigPose gim = uavGimbalPose(g);
        const bool gimOk = dot(gim.forward(), normalize(transformDirection(g, {0, 0, 1}))) > 0.9999f;
        const bool cycle = nextUavView(UavView::Chase) == UavView::Fpv && nextUavView(UavView::Fpv) == UavView::Gimbal &&
                           nextUavView(UavView::Gimbal) == UavView::Chase && std::string(uavViewName(UavView::Fpv)) == "FPV";
        // Pitch ladder: camera 20 deg down, 60 deg FOV, 900 px: the horizon rung 450 tan 20 / tan 30 = 283.7 px above.
        float horizon = 1e9f;
        for (const LadderRung& r : pitchLadder(-20.0f, 60.0f, 900.0f, 10, 400.0f))
            if (r.deg == 0) horizon = r.offset;
        const std::vector<TapeMark> tape = tapeMarks(7.4f, 1.0f, 5, 16.0f, 150.0f);
        bool tapeOk = !tape.empty();
        for (const TapeMark& m : tape) tapeOk = tapeOk && std::fabs(m.offset - (m.value - 7.4f) * 16.0f) < 1e-3f &&
                                                (m.major == (std::fmod(m.value + 1000.0f, 5.0f) < 1e-3f));
        const bool lockOk = lockProgress(0.4f, 0.8f) == 0.5f && lockProgress(2.0f, 0.8f) == 1.0f && lockProgress(-1.0f, 0.8f) == 0.0f;
        RigPose framed;
        framed.eye = {0, 2, 0};
        framed.fovDeg = 60.0f;
        framed.rot = framedLook(framed.eye, {0, 0, 10}, 60.0f, 0.0f, 9.0f);
        float h0 = 0.0f, p0 = 0.0f, r0 = 0.0f;
        poseAttitude(framed, h0, p0, r0);
        record("T368", "FPV nose camera, gimbal camera, V view cycle and the HUD projections (pitch ladder, tapes, lock ring)",
               "FPV looks 14 deg above the airframe's nose and rolls with it; gimbal along its +Z; chase > FPV > gimbal > chase; "
               "horizon rung +283.7 px; tape marks at (v - value) * ppu, every 5th major; lock 0.5 / 1 / 0; attitude roll 9",
               fmt("fpv up-tilt %.2f (ok %d), gimbal %d, cycle %d, horizon %.1f px, tape %d (%zu marks), lock %d, heading %.1f roll %.2f",
                   upTilt, fpvOk ? 1 : 0, gimOk ? 1 : 0, cycle ? 1 : 0, horizon, tapeOk ? 1 : 0, tape.size(), lockOk ? 1 : 0, h0, r0),
               fpvOk && gimOk && cycle && std::fabs(horizon - 283.7f) < 0.5f && tapeOk && lockOk && std::fabs(r0 - 9.0f) < 0.05f &&
                   std::fabs(h0) < 0.05f);
    }

    // ---- T369: the player flies UAV-1 off its pad by day and lets go: MANUAL, then RETURN, LANDING, DOCKED -------------
    {
        SceneOptions o = SceneOptions::v6(9, 11.0f, false);
        o.garrison = false;
        Scene s(o);
        const Vec3 pad = s.squad().pad(0).touchdown;
        ControlInput in;
        in.lift = 1.0f;
        float heldUntil = -1.0f;
        for (int i = 0; i < 30 * 4; ++i) {   // PgUp from the pad: spool-up first, then the climb
            stepVision(s, &in, 0);
            if (s.droneState(0).position.y <= pad.y + 1e-4f) heldUntil = s.time();
        }
        const bool manual = std::string(s.uavDutyState(0)) == "MANUAL";
        const float climbed = s.droneState(0).position.y - pad.y;
        in = ControlInput{};
        in.forward = 1.0f;
        in.turn = 1.0f;   // D: yaw right (the yaw falls)
        const float yaw0 = s.droneState(0).yawDeg;
        for (int i = 0; i < 30; ++i) stepVision(s, &in, 0);
        const float dyaw = s.droneState(0).yawDeg - yaw0;
        const bool vision = s.uavVisionActive(0);
        // Let go by day: back over the pad, down at <= the touch-down speed near the slab, docked, rotors spooling down.
        std::vector<std::string> seq;
        std::string prev = s.uavDutyState(0);
        float lastSpeed = 0.0f;
        for (int i = 0; i < 30 * 60 && prev != "DOCKED"; ++i) {
            const float y0 = s.droneState(0).position.y;
            stepVision(s);
            const std::string now = s.uavDutyState(0);
            if (now != prev) seq.push_back(now);
            if (now == "LANDING" && s.squad().heightAbovePad(0) < 0.2f && s.squad().heightAbovePad(0) > 0.0f) lastSpeed = (y0 - s.droneState(0).position.y) / kDt;
            prev = now;
        }
        for (int i = 0; i < 30 * 4; ++i) stepVision(s);
        std::string seqText;
        for (const std::string& q : seq) seqText += q + " ";
        const bool home = prev == "DOCKED" && length(s.droneState(0).position - pad) < 1e-3f && s.uavRotorSpool(0) == 0.0f;
        record("T369", "Manual flight from the pad by day: spool-up, climb, D yaws right; on release RETURN > LANDING > DOCKED",
               "held on the pad ~2 s (spool-up), then > 1 m up, duty MANUAL, D lowers the yaw, camera active in the air; release: "
               "RETURN LANDING DOCKED, touch-down speed <= 0.26 m/s, parked on the pad, rotors still",
               fmt("held until %.2f s, climbed %.2f m, %s, dyaw %.1f, vision %d, after release: %s(touch-down %.3f m/s), home %d",
                   heldUntil, climbed, manual ? "MANUAL" : s.uavDutyState(0), dyaw, vision ? 1 : 0, seqText.c_str(), lastSpeed, home ? 1 : 0),
               heldUntil > 1.8f && heldUntil < 2.2f && climbed > 0.8f && manual && dyaw < -10.0f && vision &&
                   seqText == "RETURN LANDING DOCKED " && lastSpeed <= kTouchdownSpeed + 0.01f && home);
    }
}
