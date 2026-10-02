// T90-T99: helicopter flight model and rotor kinematics, radar modes, part inventory of both objects.
#include <algorithm>
#include <cmath>
#include <functional>

#include "TestKit.h"
#include "objects/Helicopter.h"
#include "objects/Radar.h"
#include "scene/Combat.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {

using namespace ts;

constexpr float kDt = 1.0f / 60.0f;

void step(Controllable& c, const ControlInput* in, float seconds, float* time = nullptr) {
    WorldContext w;
    w.dt = kDt;
    const int n = static_cast<int>(std::lround(seconds / kDt));
    for (int i = 0; i < n; ++i) {
        if (time) *time += kDt;
        w.time = time ? *time : 0.0f;
        c.update(w, in);
    }
}

ControlInput press() {
    ControlInput in;
    in.actionPressed = true;
    return in;
}

void tick(Controllable& c, const ControlInput& in) {
    WorldContext w;
    w.dt = kDt;
    c.update(w, &in);
}

// Spool up and take off to the given height.
void flyTo(Helicopter& h, float agl) {
    ControlInput idle, up;
    up.lift = 1.0f;
    tick(h, press());
    step(h, &idle, 6.5f);
    for (int i = 0; i < 60 * 30 && h.altitudeAgl() < agl; ++i) tick(h, up);
    step(h, &idle, 3.0f);
}

void countLeaves(const SceneNode& n, int& leaves, bool& labOnly, bool& named) {
    if (n.isLeaf()) {
        ++leaves;
        const auto p = n.primitive;
        if (!(p == PrimitiveType::Cube || p == PrimitiveType::Sphere || p == PrimitiveType::Cylinder ||
              p == PrimitiveType::Cone || p == PrimitiveType::BeamCone))
            labOnly = false;
    }
    if (n.name.empty() || n.object.empty() || n.purpose.empty()) named = false;
    for (const auto& c : n.children) countLeaves(*c, leaves, labOnly, named);
}

float angDiff(float a, float b) {
    float d = std::fmod(a - b, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

}  // namespace

TS_TEST_GROUP(AirObjects) {
    // ---- T90: rotor spool-up ----
    {
        Helicopter h;
        ControlInput idle;
        float t = 0.0f;
        const float rest = h.rotorFraction();
        tick(h, press());
        const bool started = h.state() == Helicopter::State::Startup;
        float tReady = -1.0f;
        for (int i = 0; i < 60 * 12; ++i) {
            tick(h, idle);
            t += kDt;
            if (h.state() == Helicopter::State::Hover && tReady < 0) { tReady = t; break; }
        }
        record("T90", "helicopter rotor spools up in ~6 s after the start command", "rest 0%, STARTUP, HOVER at 6.0 +- 0.2 s, 100%",
               fmt("rest %.0f%%, startup %d, ready after %.2f s, rotor %.0f%%", rest * 100, started, tReady, h.rotorFraction() * 100),
               rest == 0.0f && started && std::fabs(tReady - 6.0f) < 0.2f && h.rotorFraction() > 0.99f);
    }
    // ---- T91: blade azimuths ----
    {
        Helicopter h;
        ControlInput idle;
        tick(h, press());
        step(h, &idle, 6.0f);
        const double a0 = h.rotorAngleTotalDeg();
        // spool-up integral: rpm * 6 deg/s * 0.5 * 6 s = 5400 deg (the 60 Hz sum is within one step of that)
        const double expectSpool = 0.5 * Helicopter::kRotorRpm * 6.0 * Helicopter::kSpoolUpSeconds;
        step(h, &idle, 2.0f);
        const double a1 = h.rotorAngleTotalDeg();
        const double expect = Helicopter::kRotorRpm * 6.0 * 2.0;
        float worstSpacing = 0.0f;
        for (int i = 0; i < Helicopter::kBlades; ++i) {
            const float d = angDiff(h.bladeAzimuthDeg((i + 1) % Helicopter::kBlades), h.bladeAzimuthDeg(i));
            worstSpacing = std::max(worstSpacing, std::fabs(d - 72.0f));
        }
        const float nodeAz = std::fmod(static_cast<float>(a1) + 0.0f, 360.0f);
        const float blade0 = h.bladeAzimuthDeg(0);
        record("T91", "5 blades evenly spaced (72 deg) and the angle integrates rpm * dt",
               "spool-up 5400 +- 60 deg, 2 s at full rpm = 3600 +- 5 deg, spacing 72 +- 0.1, blade 1 azimuth = total angle mod 360",
               fmt("spool %.0f, 2 s %.1f, spacing error %.3f, blade1 %.1f vs %.1f", a0, a1 - a0, worstSpacing, blade0, nodeAz),
               std::fabs(a0 - expectSpool) < 60.0 && std::fabs((a1 - a0) - expect) < 5.0 && worstSpacing < 0.1f &&
                   std::fabs(angDiff(blade0, nodeAz)) < 0.5f);
    }
    // ---- T92: attitude bounded, speed limits ----
    {
        Helicopter h;
        flyTo(h, 10.0f);
        ControlInput in;
        float maxPitch = 0, maxRoll = 0, maxSpeed = 0, maxPitchFwd = 0;
        in.forward = 1.0f;
        in.boost = true;
        for (int i = 0; i < 60 * 40; ++i) {
            in.turn = i < 60 * 5 ? 0.0f : ((i / 300) % 2 ? 1.0f : -1.0f);
            if (i > 60 * 20) in.forward = (i / 200) % 2 ? 1.0f : -1.0f;
            tick(h, in);
            maxPitch = std::max(maxPitch, std::fabs(h.pitchDeg()));
            maxRoll = std::max(maxRoll, std::fabs(h.rollDeg()));
            maxSpeed = std::max(maxSpeed, h.groundSpeed());
            if (i < 60 * 8) maxPitchFwd = std::max(maxPitchFwd, h.pitchDeg());
        }
        record("T92", "attitude follows acceleration but stays bounded; speed capped at 25 m/s", "pitch <= 25, roll <= 30 deg, nose-down >= 3 deg when accelerating, speed in (15, 25.5]",
               fmt("max pitch %.1f, roll %.1f, nose-down %.1f, speed %.1f m/s", maxPitch, maxRoll, maxPitchFwd, maxSpeed),
               maxPitch <= 25.0f && maxRoll <= 30.0f && maxPitchFwd >= 3.0f && maxSpeed > 15.0f && maxSpeed <= 25.5f);
    }
    // ---- T93: altitude and radius limits ----
    {
        Helicopter h;
        flyTo(h, 5.0f);
        ControlInput up, down, fwd;
        up.lift = 1.0f;
        down.lift = -1.0f;
        float maxAgl = 0, minAgl = 1e9f;
        for (int i = 0; i < 60 * 25; ++i) { tick(h, up); maxAgl = std::max(maxAgl, h.altitudeAgl()); }
        for (int i = 0; i < 60 * 30; ++i) { tick(h, down); minAgl = std::min(minAgl, h.altitudeAgl()); }
        fwd.forward = 1.0f;
        fwd.boost = true;
        fwd.lift = 0.3f;
        float maxR = 0;
        for (int i = 0; i < 60 * 15; ++i) {
            fwd.turn = 0.0f;
            tick(h, fwd);
            const Vec3 p = h.position();
            maxR = std::max(maxR, std::sqrt(p.x * p.x + p.z * p.z));
        }
        record("T93", "altitude limited to 0.5..40 m AGL while airborne, radius <= 60 m",
               "max AGL in [39, 40.05], min AGL >= 0.49 (airborne), r <= 60.1",
               fmt("max AGL %.2f, min AGL %.2f, max r %.2f", maxAgl, minAgl, maxR),
               maxAgl >= 39.0f && maxAgl <= 40.05f && minAgl >= 0.49f && maxR <= 60.1f);
    }
    // ---- T94: landing rules ----
    {
        Helicopter h;
        flyTo(h, 30.0f);  // high: refuse
        tick(h, press());
        const bool refusedHigh = h.state() != Helicopter::State::Landing && h.landRefusals() == 1;
        ControlInput idle, down;
        down.lift = -1.0f;
        for (int i = 0; i < 60 * 12; ++i) tick(h, down);  // down to the 0.5 m floor
        ControlInput fast;
        fast.forward = 1.0f;
        fast.boost = true;
        fast.lift = 0.2f;
        for (int i = 0; i < 150; ++i) tick(h, fast);   // fast (2.5 s, stays inside the 60 m disc)
        const int before = h.landRefusals();
        tick(h, press());
        const bool refusedFast = h.state() != Helicopter::State::Landing && h.landRefusals() == before + 1;
        for (int i = 0; i < 60 * 10; ++i) tick(h, idle);
        tick(h, press());
        const bool accepted = h.state() == Helicopter::State::Landing;
        step(h, &idle, 12.0f);
        const bool down2 = h.state() == Helicopter::State::Parked && h.altitudeAgl() < 0.05f;
        step(h, &idle, 9.0f);
        // steep ground: refuse
        float bestSlope = 0, bx = 0, bz = 0;
        for (float x = -18; x <= 18; x += 0.5f)
            for (float z = -18; z <= 18; z += 0.5f) {
                const float s = std::fabs(Helicopter::groundAt(x + 1.5f, z) - Helicopter::groundAt(x - 1.5f, z)) / 3.0f;
                if (s > bestSlope) { bestSlope = s; bx = x; bz = z; }
            }
        Helicopter g;
        g.debugPlace({bx, Helicopter::groundAt(bx, bz) + 3.0f, bz}, 90.0f, Helicopter::State::Hover, 1.0f);
        tick(g, press());
        const bool slopeRefused = bestSlope <= Helicopter::kLandMaxSlope ? true : g.state() != Helicopter::State::Landing;
        record("T94", "landing only from low speed / height on flat ground; touches down and spools down",
               "refused at 30 m AGL, refused at >6 m/s, accepted when slow and low, PARKED on the ground, rotor 0%, steep ground refused",
               fmt("high %d, fast %d, accepted %d, parked %d, rotor %.0f%%, steepest slope %.2f refused %d", refusedHigh, refusedFast, accepted,
                   down2, h.rotorFraction() * 100, bestSlope, slopeRefused),
               refusedHigh && refusedFast && accepted && down2 && h.rotorFraction() == 0.0f && slopeRefused);
    }
    // ---- T95: not possessed ----
    {
        Helicopter parked;
        const Vec3 p0 = parked.position();
        step(parked, nullptr, 5.0f);
        const bool still = parked.state() == Helicopter::State::Parked && parked.rotorFraction() == 0.0f &&
                           length(parked.position() - p0) < 1e-4f;
        Helicopter f;
        flyTo(f, 12.0f);
        ControlInput fwd;
        fwd.forward = 1.0f;
        for (int i = 0; i < 60 * 5; ++i) tick(f, fwd);
        const float hAtRelease = f.altitudeAgl();
        step(f, nullptr, 8.0f);
        const Vec3 pa = f.position();
        step(f, nullptr, 2.0f);
        const float drift = length(f.position() - pa);
        record("T95", "not possessed: parked helicopter stays still; released in flight it hovers in place",
               "parked: rotor 0, no motion; flying: speed < 0.3 m/s after 8 s, altitude held within 0.5 m, still HOVER",
               fmt("parked still %d; flying: speed %.2f, alt %.1f -> %.1f, drift %.3f, state %s", still, f.groundSpeed(), hAtRelease,
                   f.altitudeAgl(), drift, f.stateName()),
               still && f.groundSpeed() < 0.3f && std::fabs(f.altitudeAgl() - hAtRelease) < 3.0f && drift < 0.2f &&
                   f.state() == Helicopter::State::Hover);
    }
    // ---- T96: helicopter parts ----
    {
        Helicopter h;
        int leaves = 0;
        bool lab = true, named = true;
        countLeaves(*h.root(), leaves, lab, named);
        std::vector<Box> boxes = h.dynamicObstacles();
        // blur disc translucent and fades in with rotor speed
        const float opRest = h.mainBlurDisc()->material.opacity;
        ControlInput idle;
        tick(h, press());
        step(h, &idle, 7.0f);
        const float opFull = h.mainBlurDisc()->material.opacity;
        const Vec3 pos = h.position();
        record("T96", "helicopter: lab primitives only, every node named, blur disc fades in, parked on the pad with an obstacle box",
               "leaves >= 60, all lab primitives, 0 < blur opacity < 1 at full rpm, >= 1 obstacle box while parked",
               fmt("leaves %d, lab %d, named %d, blur %.2f -> %.2f, boxes %zu, pos (%.1f, %.1f, %.1f)", leaves, lab, named, opRest, opFull,
                   boxes.size(), pos.x, pos.y, pos.z),
               leaves >= 60 && lab && named && opRest < 0.01f && opFull > 0.1f && opFull < 1.0f && !boxes.empty());
    }
    // ---- T97: radar rotation ----
    {
        Radar r;
        ControlInput idle;
        step(r, &idle, 1.0f);
        const float a0 = r.azimuthDeg();
        float swept = 0, prev = a0;
        for (int i = 0; i < 300; ++i) {
            tick(r, idle);
            swept += std::fmod(r.azimuthDeg() - prev + 360.0f, 360.0f);
            prev = r.azimuthDeg();
        }
        record("T97", "radar ROTATE mode turns 12 rpm (360 deg in 5 s)", "5 s sweep = 360 +- 1 deg, mode ROTATE when idle",
               fmt("swept %.2f deg in 5 s, mode %s", swept, r.modeName()), std::fabs(swept - 360.0f) < 1.0f && r.mode() == Radar::Mode::Rotate);
    }
    // ---- T98: sector ----
    {
        Radar r;
        ControlInput idle, act = press();
        step(r, &idle, 2.3f);
        tick(r, act);  // ROTATE -> SECTOR
        const float centre = r.sectorCentreDeg();
        float lo = 0, hi = 0;
        bool inside = true;
        for (int i = 0; i < 60 * 24; ++i) {
            tick(r, idle);
            const float d = angDiff(r.azimuthDeg(), centre);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
            if (std::fabs(d) > 45.01f) inside = false;
        }
        record("T98", "radar SECTOR mode sweeps +-45 deg around the aimed centre", "mode SECTOR, |az - centre| <= 45 always, both extremes within 1 deg of +-45",
               fmt("mode %s, centre %.1f, range %.1f .. %.1f", r.modeName(), centre, lo, hi),
               r.mode() == Radar::Mode::Sector && inside && lo < -44.0f && hi > 44.0f);
    }
    // ---- T99: tilt limits, radar parts ----
    {
        Radar r;
        ControlInput up, down;
        up.aimPitch = 1.0f;
        down.aimPitch = -1.0f;
        tick(r, up);
        for (int i = 0; i < 60 * 3; ++i) tick(r, up);
        const float hiT = r.tiltDeg();
        for (int i = 0; i < 60 * 3; ++i) tick(r, down);
        const float loT = r.tiltDeg();
        int leaves = 0, tiles = 0;
        bool lab = true, named = true;
        countLeaves(*r.root(), leaves, lab, named);
        std::function<void(const SceneNode&)> cnt = [&](const SceneNode& n) {
            if (n.name.rfind("Array tile", 0) == 0) ++tiles;
            for (const auto& c : n.children) cnt(*c);
        };
        cnt(*r.root());
        record("T99", "radar tilt limited to -5..+20 deg; antenna = 7 x 3 curved tiles; lab primitives only",
               "tilt max 20, min -5, 21 tiles, all lab primitives, nodes named",
               fmt("tilt %.1f .. %.1f, tiles %d, leaves %d, lab %d, named %d", loT, hiT, tiles, leaves, lab, named),
               std::fabs(hiT - 20.0f) < 0.01f && std::fabs(loT + 5.0f) < 0.01f && tiles == 21 && lab && named);
    }
}
