// v9 M3 (character motor, spring camera, locomotion): T350-T359. CPU only.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/Terrain.h"
#include "scene/CameraRig.h"
#include "scene/CharacterMotor.h"
#include "scene/Gait.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

constexpr float kTick = 1.0f / 60.0f;

// Flat test ground: one floor at y = 0 over the middle of the map (WalkSpace floors override the terrain).
WalkSpace flatGround() {
    WalkSpace ws;
    WalkFloor f{-30.0f, -30.0f, 30.0f, 30.0f, 0.0f, 0.0f, 0, 1e9f};
    ws.addFloor(f);
    return ws;
}

struct Run {
    CharacterMotor m;
    const WalkSpace* ws = nullptr;
    std::vector<Box> solids;
    float t = 0.0f;
    explicit Run(const WalkSpace* w, float x = 0.0f, float z = 0.0f, float y = 0.0f) : ws(w) {
        m.foot.radius = 0.25f;
        m.reset(x, z, y);
    }
    void tick(const MotorInput& in) {
        m.step(in, ws, solids, kTick);
        t += kTick;
    }
};

MotorInput forward(float f = 1.0f, float yaw = 90.0f) {
    MotorInput in;
    in.forward = f;
    in.yawDeg = yaw;   // +x
    return in;
}

// First tick (s) at which the speed reaches 'level' (or -1).
float riseTime(Run& r, const MotorInput& in, float level, float maxT, float* peak = nullptr) {
    const float t0 = r.t;
    float hit = -1.0f, top = 0.0f;
    while (r.t - t0 < maxT) {
        r.tick(in);
        top = std::max(top, r.m.groundSpeed());
        if (hit < 0.0f && r.m.groundSpeed() >= level) hit = r.t - t0;
    }
    if (peak) *peak = top;
    return hit;
}

float boxDist(const Vec3& p, const Box& b) {
    const float dx = std::max(std::max(b.min.x - p.x, 0.0f), p.x - b.max.x);
    const float dy = std::max(std::max(b.min.y - p.y, 0.0f), p.y - b.max.y);
    const float dz = std::max(std::max(b.min.z - p.z, 0.0f), p.z - b.max.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Segment a -> b against an AABB (slab test).
bool segmentHits(const Vec3& a, const Vec3& b, const Box& box) {
    float t0 = 0.0f, t1 = 1.0f;
    const float o[3] = {a.x, a.y, a.z}, d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-9f) {
            if (o[k] < lo[k] || o[k] > hi[k]) return false;
            continue;
        }
        float p = (lo[k] - o[k]) / d[k], q = (hi[k] - o[k]) / d[k];
        if (p > q) std::swap(p, q);
        t0 = std::max(t0, p);
        t1 = std::min(t1, q);
        if (t0 > t1) return false;
    }
    return true;
}

Mat4 worldOf(const SceneNode* n) {
    Mat4 m = n->local.matrix();
    for (const SceneNode* p = n->parent; p; p = p->parent) m = p->local.matrix() * m;
    return m;
}

struct Lcg {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777215.0f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

}  // namespace

TS_TEST_GROUP(V9M3) {
    const WalkSpace flat = flatGround();

    // ---- T350: velocity response: 90 % of walk speed <= 0.25 s, stop <= 0.2 s, no overshoot; sprint -------------
    {
        Run r(&flat);
        float peak = 0.0f;
        const float rise = riseTime(r, forward(), 0.9f * 1.3f, 1.5f, &peak);
        // release: time until the body is below 5 cm/s (and 1 cm/s)
        float stop5 = -1.0f, stop1 = -1.0f;
        const float t0 = r.t;
        while (r.t - t0 < 1.0f) {
            r.tick(MotorInput{forward(0.0f)});
            if (stop5 < 0.0f && r.m.groundSpeed() < 0.05f) stop5 = r.t - t0;
            if (stop1 < 0.0f && r.m.groundSpeed() < 0.01f) stop1 = r.t - t0;
        }
        Run q(&flat);
        MotorInput sprint = forward();
        sprint.sprint = true;
        float sprintPeak = 0.0f;
        const float sprintRise = riseTime(q, sprint, 0.9f * 3.4f, 2.0f, &sprintPeak);
        const float analytic = critRiseTime(q.m.params.omegaAccel, 0.9f);
        record("T350", "Character motor: critically damped speed response (no linear clamps): walk 90 % <= 0.25 s, stop <= 0.2 s",
               "rise to 1.17 m/s <= 0.25 s, below 5 cm/s <= 0.2 s after release, peak <= 1.3 m/s (no overshoot), sprint reaches 3.06 m/s",
               fmt("walk 90 %% after %.3f s (analytic %.3f s), peak %.4f m/s; stop < 5 cm/s %.3f s, < 1 cm/s %.3f s; sprint 90 %% after %.3f s, peak %.3f",
                   rise, analytic, peak, stop5, stop1, sprintRise, sprintPeak),
               rise > 0.0f && rise <= 0.25f && stop5 > 0.0f && stop5 <= 0.2f && peak <= 1.3f + 1e-3f && sprintRise > 0.0f &&
                   sprintPeak <= 3.4f + 1e-3f);
    }

    // ---- T351: coyote time (0.12 s) and jump buffer (0.15 s) ----------------------------------------------------------
    {
        // Ledge: a 1 m platform (solid box) ending at x = 2; walk off its edge, press jump k ticks after leaving; only a
        // take-off before touching the ground below counts (a late press is buffered and fires on landing instead).
        auto ledge = [&](int pressTick) {
            Run r(&flat, 0.0f, 0.0f, 1.0f);
            r.solids.push_back({{-3.0f, 0.0f, -2.0f}, {2.0f, 1.0f, 2.0f}});
            int left = -1;
            bool jumped = false;
            for (int i = 0; i < 240 && !jumped; ++i) {
                MotorInput in = forward(0.5f);
                if (left >= 0 && i - left == pressTick) in.jumpPressed = true;
                r.tick(in);
                if (left < 0 && !r.m.grounded()) left = i;
                if (r.m.jumped()) jumped = true;
                if (r.m.landed()) break;
            }
            return jumped;
        };
        const bool coyoteIn = ledge(4), coyoteEdge = ledge(6), coyoteOut = ledge(12);   // 0.067 / 0.1 / 0.2 s after the drop
        // Buffer: jump, find the landing tick, replay pressing a second jump n ticks before it.
        int landTick = -1;
        {
            Run r(&flat);
            for (int i = 0; i < 120 && landTick < 0; ++i) {
                MotorInput in;
                in.jumpPressed = i == 0;
                r.tick(in);
                if (r.m.landed()) landTick = i;
            }
        }
        auto buffered = [&](int early, int& firedAt) {
            Run r(&flat);
            firedAt = -1;
            for (int i = 0; i < 160; ++i) {
                MotorInput in;
                in.jumpPressed = i == 0 || i == landTick - early;
                r.tick(in);
                if (i > 0 && r.m.jumped() && firedAt < 0) firedAt = i;
            }
            return firedAt >= 0;
        };
        int firedEarly = -1, firedLate = -1;
        const bool bufIn = buffered(6, firedEarly), bufOut = buffered(15, firedLate);   // 0.1 s / 0.25 s before landing
        // No double jump: a press in the air (well after the coyote window) does nothing.
        Run d(&flat);
        int extra = 0;
        for (int i = 0; i < 40; ++i) {
            MotorInput in;
            in.jumpPressed = i == 0 || i == 20;
            d.tick(in);
            if (i > 0 && d.m.jumped()) ++extra;
        }
        record("T351", "Jump: coyote time 0.12 s after walking off a ledge, jump buffer 0.15 s before landing, no air jump",
               "jump fires 0.067 s and 0.1 s after the drop, not 0.2 s after; a press 0.1 s before landing fires on landing, "
               "0.25 s before does not; mid-air press ignored",
               fmt("coyote %d / %d / %d, landing tick %d, buffered press fired at tick %d (landing + %d), late press %d, air jumps %d",
                   coyoteIn ? 1 : 0, coyoteEdge ? 1 : 0, coyoteOut ? 1 : 0, landTick, firedEarly, firedEarly - landTick, bufOut ? 1 : 0, extra),
               coyoteIn && coyoteEdge && !coyoteOut && landTick > 0 && bufIn && firedEarly >= landTick && firedEarly <= landTick + 1 &&
                   !bufOut && extra == 0);
    }

    // ---- T352: step-up 0.35 m passes, 0.5 m blocks, step-down drawn without a pop -------------------------------------
    {
        auto climb = [&](float h, float& endX, float& endY, float& maxDrawnStep, float& maxRawStep) {
            Run r(&flat);
            r.solids.push_back({{1.0f, 0.0f, -2.0f}, {2.5f, h, 2.0f}});   // 1.5 m deep block in front
            float drawnPrev = 0.0f, rawPrev = 0.0f;
            bool groundPrev = true;
            maxDrawnStep = maxRawStep = 0.0f;
            endY = 0.0f;
            for (int i = 0; i < 240; ++i) {
                r.tick(forward(1.0f));
                const float drawn = r.m.body().feetY + r.m.stepOffset(), raw = r.m.body().feetY;
                if (groundPrev && r.m.grounded()) {   // walking (a fall off the far side is not a step)
                    maxDrawnStep = std::max(maxDrawnStep, std::fabs(drawn - drawnPrev));
                    maxRawStep = std::max(maxRawStep, std::fabs(raw - rawPrev));
                }
                groundPrev = r.m.grounded();
                drawnPrev = drawn;
                rawPrev = raw;
                if (i == 100) {   // on top of a passable step at this time (1.3 m/s x 1.67 s)
                    endY = r.m.body().feetY;
                }
            }
            endX = r.m.body().x;
        };
        float x35, y35, d35, raw35, x50, y50, d50, raw50, x30, y30, d30, raw30;
        climb(0.35f, x35, y35, d35, raw35);
        climb(0.50f, x50, y50, d50, raw50);
        climb(0.30f, x30, y30, d30, raw30);   // up and back down off the far side (inside the 0.32 m snap)
        record("T352", "Step-up: 0.35 m steps are climbed, 0.5 m blocks; steps up / down are drawn on a spring (no pop)",
               "0.35: on top (y 0.35) and past the block; 0.5: stopped before it (x < 0.76); on the ground the drawn height changes "
               "<= 4 cm per tick while the collision feet jump 0.3-0.35 m",
               fmt("0.35: y %.2f, end x %.2f, drawn step %.3f (raw %.3f); 0.5: end x %.2f y %.2f; 0.3 up+down: end x %.2f, drawn step %.3f (raw %.3f)",
                   y35, x35, d35, raw35, x50, y50, x30, d30, raw30),
               std::fabs(y35 - 0.35f) < 1e-3f && x35 > 2.6f && x50 < 0.76f && y50 < 0.01f && d35 <= 0.04f && raw35 >= 0.34f && x30 > 2.6f &&
                   d30 <= 0.04f && raw30 >= 0.29f);
    }

    // ---- T353: slope limit 40 deg ---------------------------------------------------------------------------------------
    {
        auto ramp = [&](float deg, float& rise, int& blocked) {
            WalkSpace ws = flatGround();
            const float len = 4.0f, top = len * std::tan(radians(deg));
            WalkFloor f{1.0f, -3.0f, 1.0f + len, 3.0f, 0.0f, top, 1, 1e9f};
            ws.addFloor(f);
            Run r(&ws);
            blocked = 0;
            rise = 0.0f;
            for (int i = 0; i < 300; ++i) {
                r.tick(forward(1.0f));
                if (r.m.slopeBlocked()) ++blocked;
                rise = std::max(rise, r.m.body().feetY);
            }
        };
        float r30, r38, r45, r60;
        int b30, b38, b45, b60;
        ramp(30.0f, r30, b30);
        ramp(38.0f, r38, b38);
        ramp(45.0f, r45, b45);
        ramp(60.0f, r60, b60);
        // a 0.45 m floor step on flat ground is not a slope (climbed by the step rule)
        WalkSpace ws = flatGround();
        ws.addFloor({1.0f, -3.0f, 6.0f, 3.0f, 0.4f, 0.4f, 0, 1e9f});
        const Vec3 gStep = groundSlope(&ws, 1.02f, 0.0f, 0.0f);
        record("T353", "Slope limit 40 deg: ramps up to the limit are walked, steeper ground blocks the uphill part of the step",
               "30 / 38 deg ramps climbed (> 2 m up), 45 / 60 deg: < 0.45 m up and the limit engaged; a 0.4 m floor step reads < tan 40",
               fmt("rise 30 deg %.2f m (blocked %d ticks), 38 deg %.2f (%d), 45 deg %.2f (%d), 60 deg %.2f (%d); step slope %.2f",
                   r30, b30, r38, b38, r45, b45, r60, b60, std::hypot(gStep.x, gStep.z)),
               r30 > 2.0f && r38 > 2.0f && b30 == 0 && b38 == 0 && r45 < 0.45f && r60 < 0.45f && b45 > 0 && b60 > 0 &&
                   std::hypot(gStep.x, gStep.z) < std::tan(radians(40.0f)));
    }

    // ---- T354: spring camera: critically damped, no overshoot (step, ramp-then-stop, arm ease-out) ------------------------
    {
        // scalar spring step response from rest
        float x = 0.0f, v = 0.0f, worst = 0.0f;
        bool monotonic = true;
        float prev = 0.0f;
        for (int i = 0; i < 120; ++i) {
            critDampStep(x, v, 1.0f, 12.0f, kTick);
            worst = std::max(worst, x - 1.0f);
            if (x < prev - 1e-7f) monotonic = false;
            prev = x;
        }
        // same span in 1 step or in 60 ticks: the exact solution does not depend on the cut
        float a = 0.0f, av = 0.0f, b = 0.0f, bv = 0.0f;
        critDampStep(a, av, 1.0f, 12.0f, 1.0f);
        for (int i = 0; i < 60; ++i) critDampStep(b, bv, 1.0f, 12.0f, kTick);
        // third-person pivot: walk 1.5 m/s for 2 s along +x then stop; the camera pivot never passes the final point
        ThirdPersonCam cam;
        cam.reset({0.0f, 1.5f, 0.0f}, 90.0f, -10.0f);
        const ArmCast none = [](const Vec3&, const Vec3&, float len, float) { return len; };
        const ThirdPersonParams P = ThirdPersonParams::soldier();
        float pivOver = -1e9f, pivPrev = 0.0f;
        bool pivMono = true;
        for (int i = 0; i < 240; ++i) {
            const float px = std::min(1.5f * kTick * static_cast<float>(i + 1), 3.0f);
            cam.update({px, 1.5f, 0.0f}, kTick, false, P, none);
            pivOver = std::max(pivOver, cam.pivot().x - 3.0f);
            if (cam.pivot().x < pivPrev - 1e-6f) pivMono = false;
            pivPrev = cam.pivot().x;
        }
        // arm: a wall pulls it in to ~1 m, the wall goes, the arm eases back out to 3.1 m without overshoot
        ThirdPersonCam arm;
        arm.reset({0.0f, 1.5f, 0.0f}, 0.0f, 0.0f);
        bool wall = true;
        const ArmCast cast = [&](const Vec3&, const Vec3&, float len, float) { return wall ? std::min(len, 1.0f) : len; };
        for (int i = 0; i < 30; ++i) arm.update({0.0f, 1.5f, 0.0f}, kTick, false, P, cast);
        const float pulled = arm.arm();
        wall = false;
        float armMax = 0.0f, armPrev = pulled, t90 = -1.0f;
        bool armMono = true;
        for (int i = 0; i < 180; ++i) {
            arm.update({0.0f, 1.5f, 0.0f}, kTick, false, P, cast);
            armMax = std::max(armMax, arm.arm());
            if (arm.arm() < armPrev - 1e-6f) armMono = false;
            armPrev = arm.arm();
            if (t90 < 0.0f && arm.arm() >= pulled + 0.9f * (P.arm - pulled)) t90 = (i + 1) * kTick;
        }
        record("T354", "Spring camera (zeta = 1): no overshoot on a step, after a walk-then-stop, or when the arm eases back out",
               "step: monotonic, never past the goal; one 1 s step == 60 ticks (< 1e-5); pivot never passes the stop point; "
               "arm in 1 m at once, back out monotonic to <= 3.1 m (90 % in 0.3..1.0 s)",
               fmt("step overshoot %.2e, monotonic %d, cut error %.2e; pivot overshoot %.4f m, monotonic %d; arm pulled %.2f, max %.4f, "
                   "monotonic %d, 90 %% after %.2f s",
                   worst, monotonic ? 1 : 0, std::fabs(a - b), pivOver, pivMono ? 1 : 0, pulled, armMax, armMono ? 1 : 0, t90),
               worst <= 1e-6f && monotonic && std::fabs(a - b) < 1e-5f && pivOver <= 1e-4f && pivMono && std::fabs(pulled - 1.0f) < 1e-4f &&
                   armMax <= P.arm + 1e-4f && armMono && t90 >= 0.3f && t90 <= 1.0f);
    }

    // ---- T355: spring arm never enters a wall: 500 random poses near random walls (sphereCast boxes) -------------------
    {
        Lcg rng{0x5eed350u};
        int poses = 0, inside = 0, crossed = 0, ticks = 0;
        float minClear = 1e9f;
        const ThirdPersonParams P = ThirdPersonParams::soldier();
        for (int k = 0; poses < 500 && k < 5000; ++k) {
            // a room corner / corridor: 2..4 wall boxes around the origin, a pivot 0.35..2 m from them
            std::vector<Box> walls;
            const int n = 2 + static_cast<int>(rng.next() * 3.0f);
            for (int w = 0; w < n; ++w) {
                const float ang = rng.range(0.0f, 2.0f * PI), dist = rng.range(0.6f, 2.6f), len = rng.range(0.5f, 5.0f);
                const float thick = rng.range(0.1f, 0.6f), h = rng.range(1.0f, 4.0f);
                const float cx = std::cos(ang) * dist, cz = std::sin(ang) * dist;
                const bool alongX = rng.next() < 0.5f;
                const float hx = alongX ? len * 0.5f : thick * 0.5f, hz = alongX ? thick * 0.5f : len * 0.5f;
                walls.push_back({{cx - hx, 0.0f, cz - hz}, {cx + hx, h, cz + hz}});
            }
            Vec3 pivot{rng.range(-0.5f, 0.5f), 0.0f, rng.range(-0.5f, 0.5f)};
            pivot.y = 1.45f + terrainHeight(pivot.x, pivot.z);
            bool clear = true;
            for (const Box& b : walls)
                if (boxDist(pivot, b) < 0.35f) clear = false;
            if (!clear) continue;
            ++poses;
            const ArmCast cast = [&](const Vec3& o, const Vec3& d, float len, float r) { return sphereCast(nullptr, walls, o, d, len, r, 0.0f); };
            ThirdPersonCam cam;
            cam.reset(pivot, rng.range(0.0f, 360.0f), rng.range(-60.0f, 45.0f));
            for (int t = 0; t < 12; ++t) {
                cam.look(rng.range(-25.0f, 25.0f), rng.range(-8.0f, 8.0f), P);   // mouse flicks
                const bool aim = rng.next() < 0.3f;
                const RigPose pose = cam.update(pivot, kTick, aim, P, cast);
                ++ticks;
                for (const Box& b : walls) {
                    const float dEye = boxDist(pose.eye, b);
                    minClear = std::min(minClear, dEye);
                    if (dEye < pose.nearZ) ++inside;
                    if (segmentHits(cam.pivot(), cam.shoulderPoint(), b) || segmentHits(cam.shoulderPoint(), pose.eye, b)) ++crossed;
                }
            }
        }
        record("T355", "Spring arm collision: over 500 random poses near random walls the camera never enters or looks through a wall",
               "500 poses x 12 ticks with mouse flicks / aim: eye never within the near plane of a wall, pivot -> shoulder -> eye never crosses one",
               fmt("%d poses, %d ticks, eye inside %d, crossings %d, closest eye-to-wall %.3f m", poses, ticks, inside, crossed, minClear),
               poses >= 500 && inside == 0 && crossed == 0);
    }

    // ---- T356: foot slip < 3 cm per stride at every speed (possessed soldier, rendered ankles) -------------------------
    {
        Scene base;
        Garrison g;
        auto owned = g.build(0);
        GarrisonContext c;
        c.solids = &base.staticSolids();
        c.walk = &base.walkSpace();
        c.startHour = 12.0f;
        c.controllables = &base.controllables();
        c.leaves = &base.leaves();
        g.init(c);
        struct Case {
            const char* name;
            float fwd, strafe;
            bool boost, crouch;
        };
        const Case cases[] = {{"slow", 0.35f, 0.0f, false, false}, {"walk", 1.0f, 0.0f, false, false}, {"sprint", 1.0f, 0.0f, true, false},
                              {"crouch", 1.0f, 0.0f, false, true}, {"back", -1.0f, 0.0f, false, false}, {"strafe", 0.0f, 1.0f, false, false},
                              {"diagonal", 1.0f, 1.0f, true, false}};
        std::string obs;
        float worstAll = 0.0f;
        int stancesAll = 0;
        bool movedAll = true;
        float t = 0.0f;
        for (const Case& cs : cases) {
            Soldier* s = g.unit(2);
            // a start on the apron whose 10 m path in the direction of travel keeps 1.2 m from every obstacle
            const float yaw = 90.0f, yr = radians(yaw);
            const Vec3 fw{std::sin(yr), 0.0f, std::cos(yr)}, rt{-std::cos(yr), 0.0f, std::sin(yr)};
            Vec3 dir = fw * cs.fwd + rt * cs.strafe;
            dir = dir * (1.0f / std::max(length(dir), 1e-6f));
            Vec3 at{14.0f, 0.0f, -8.0f};
            bool found = false;
            for (float zz = -26.0f; zz <= 26.0f && !found; zz += 1.0f)
                for (float xx = -26.0f; xx <= 26.0f && !found; xx += 1.0f) {
                    bool ok = std::hypot(xx, zz) > 21.0f && std::hypot(xx + dir.x * 10.0f, zz + dir.z * 10.0f) < 31.0f;
                    for (float d = 0.0f; d <= 10.0f && ok; d += 0.5f)
                        ok = g.planner().clearance(xx + dir.x * d, zz + dir.z * d) >= 1.2f;
                    if (ok) {
                        at = {xx, 0.0f, zz};
                        found = true;
                    }
                }
            s->place(at.x, at.z, yaw);
            WorldContext w;
            w.walk = &base.walkSpace();
            w.hour = 12.0f;
            w.daylight = 1.0f;
            w.dt = kTick;
            ControlInput in;
            if (cs.crouch) {   // stance 1 (crouch) first
                in.actionPressed = true;
                t += kTick;
                w.time = t;
                s->update(w, &in);
                in.actionPressed = false;
            }
            in.forward = cs.fwd;
            in.strafe = cs.strafe;
            in.boost = cs.boost;
            bool was[2] = {false, false};
            Vec3 first[2];
            float worst = 0.0f;
            int stances = 0;
            const Vec3 start = s->position();
            for (int f = 0; f < 180; ++f) {
                t += kTick;
                w.time = t;
                s->update(w, &in);
                for (int leg = 0; leg < 2; ++leg) {
                    const Vec3 a = transformPoint(worldOf(s->rig().ankle[leg]), {0.0f, 0.0f, 0.0f});
                    if (s->planted(leg)) {
                        if (!was[leg]) {
                            first[leg] = a;
                            if (f > 45) ++stances;
                        }
                        if (f > 45) worst = std::max(worst, std::hypot(a.x - first[leg].x, a.z - first[leg].z));   // after 0.75 s
                    }
                    was[leg] = s->planted(leg);
                }
            }
            const float moved = length(s->position() - start);
            if (std::getenv("TS_M3_DEBUG"))
                std::printf("  %s start %.2f %.2f %.2f end %.2f %.2f %.2f stance %d act %s\n", cs.name, start.x, start.y, start.z,
                            s->position().x, s->position().y, s->position().z, s->stance(), activityName(s->activity()));
            movedAll = movedAll && moved > 1.0f && stances >= 2;
            worstAll = std::max(worstAll, worst);
            stancesAll += stances;
            obs += fmt("%s %.2f m/s %.1f cm (%d)  ", cs.name, std::fabs(s->speed()), 100.0f * worst, stances);
            in = ControlInput{};
            for (int f = 0; f < 30; ++f) {   // stop and stand up between the cases
                t += kTick;
                w.time = t;
                if (cs.crouch && f == 0) in.actionPressed = true, in.forward = 0.0f;
                s->update(w, &in);
                in.actionPressed = false;
            }
            if (cs.crouch) {   // wave -> stand
                in.actionPressed = true;
                t += kTick;
                w.time = t;
                s->update(w, &in);
                in.actionPressed = false;
            }
        }
        record("T356", "Locomotion: stride phase driven by distance + world-locked stance feet: foot slip < 3 cm per stride at every speed",
               "slow walk, walk, sprint, crouch walk, backwards, strafe, diagonal sprint: rendered stance ankle drift < 3 cm, >= 2 stances each (>= 21 in all)",
               obs + fmt("worst %.2f cm", 100.0f * worstAll), worstAll < 0.03f && stancesAll >= 21 && movedAll);
    }

    // ---- T357: determinism: the same tick inputs give a bit-identical motor state -------------------------------------
    {
        auto play = [&](uint32_t seed) {
            Run r(&flat);
            r.solids.push_back({{3.0f, 0.0f, -1.0f}, {4.0f, 0.3f, 1.0f}});
            r.solids.push_back({{-4.0f, 0.0f, 2.0f}, {4.0f, 2.0f, 2.4f}});
            Lcg rng{seed};
            for (int i = 0; i < 900; ++i) {
                MotorInput in;
                in.forward = std::round(rng.range(-1.0f, 1.0f));
                in.strafe = std::round(rng.range(-1.0f, 1.0f));
                in.yawDeg = rng.range(0.0f, 360.0f);
                in.sprint = rng.next() < 0.5f;
                in.crouch = rng.next() < 0.2f;
                in.jumpPressed = rng.next() < 0.03f;
                r.tick(in);
            }
            const FootBody& b = r.m.body();
            return std::vector<float>{b.x, b.z, b.feetY, b.vy, r.m.velocity().x, r.m.velocity().z, r.m.stepOffset(), r.m.distance()};
        };
        const std::vector<float> p = play(7u), q = play(7u), o = play(8u);
        record("T357", "Determinism: the motor is a function of the tick inputs (no wall clock): a replay reproduces it bit for bit",
               "two runs of 900 ticks with the same random input: identical state; a different input stream differs",
               fmt("run A pos %.4f %.4f %.4f dist %.3f, run B identical %d, other stream differs %d", p[0], p[1], p[2], p[7], p == q ? 1 : 0,
                   p != o ? 1 : 0),
               p == q && p != o);
    }

    // ---- T358: locomotion blend tree: parameters continuous in speed, phase-by-distance keeps the model foot still ------
    {
        float maxJump = 0.0f;
        GaitParams prev = locomotionGait(0.0f, 0.0f);
        for (float v = 0.01f; v <= 4.0f; v += 0.01f) {
            const GaitParams gp = locomotionGait(v, 0.0f);
            maxJump = std::max({maxJump, std::fabs(gp.stride - prev.stride), std::fabs(gp.duty - prev.duty), std::fabs(gp.sink - prev.sink)});
            prev = gp;
        }
        // model stance foot (no IK lock): over a stance at constant speed, phase += v dt / stride: ankle x in the world
        float drift = 0.0f;
        const float speeds[] = {0.4f, 0.8f, 1.3f, 2.2f, 3.4f};
        for (float c : {0.0f, 1.0f})
            for (float v : speeds) {
                const GaitParams gp = locomotionGait(v, c);
                float phase = 0.02f, bodyX = 0.0f, fx = 0.0f, fy = 0.0f;
                footInPelvis(gp, phase, 0, fx, fy);
                const float world0 = bodyX + fx;
                for (int i = 0; i < 400 && inStance(gp, phase + v * kTick / gp.stride, 0); ++i) {
                    phase += v * kTick / gp.stride;
                    bodyX += v * kTick;
                    footInPelvis(gp, phase, 0, fx, fy);
                    drift = std::max(drift, std::fabs(bodyX + fx - world0));
                }
            }
        const float wWalk = runWeight(1.3f), wRun = runWeight(3.4f);
        const GaitParams cw = locomotionGait(0.8f, 1.0f);
        record("T358", "Locomotion blend tree: walk -> run -> crouch walk parameters continuous in speed; distance-driven phase keeps the stance foot still",
               "largest parameter change between 1 cm/s speed steps < 0.01, run weight 0 at 1.3 m/s and 1 at 3.4 m/s, crouch walk hips >= 0.25 m lower, "
               "model stance foot drift < 1 mm at 0.4..3.4 m/s (stand and crouch)",
               fmt("max step %.4f, run weight %.2f / %.2f, crouch sink %.2f m, stance drift %.4f mm", maxJump, wWalk, wRun, cw.sink, 1000.0f * drift),
               maxJump < 0.01f && wWalk == 0.0f && wRun == 1.0f && cw.sink >= 0.25f && drift < 0.001f);
    }

    // ---- T359: stance blends 0.2 s, crouch capsule, head-room, head bob ---------------------------------------------------
    {
        Run r(&flat);
        MotorInput in;
        in.crouch = true;
        float t50 = -1.0f, tFull = -1.0f, hPrevStep = 0.0f, hPrev = r.m.height();
        for (int i = 0; i < 30; ++i) {
            r.tick(in);
            if (t50 < 0.0f && r.m.crouchBlend() >= 0.5f) t50 = r.t;
            if (tFull < 0.0f && r.m.crouchBlend() >= 1.0f) tFull = r.t;
            hPrevStep = std::max(hPrevStep, std::fabs(r.m.height() - hPrev));
            hPrev = r.m.height();
        }
        const float crouchH = r.m.height();
        // under a 1.5 m overhang the body stays crouched after the key is released
        Run low(&flat);
        for (int i = 0; i < 30; ++i) low.tick(in);
        low.solids.push_back({{-1.0f, 1.5f, -1.0f}, {1.0f, 2.2f, 1.0f}});   // crouched under it now
        for (int i = 0; i < 30; ++i) low.tick(MotorInput{});
        const float lowBlend = low.m.crouchBlend();
        Run spr(&flat);
        MotorInput s = forward();
        s.sprint = true;
        float sFull = -1.0f;
        for (int i = 0; i < 30; ++i) {
            spr.tick(s);
            if (sFull < 0.0f && spr.m.sprintBlend() >= 1.0f) sFull = spr.t;
        }
        float bobWalk = 0.0f, bobRun = 0.0f;
        for (float p = 0.0f; p < 1.0f; p += 0.01f) {
            bobWalk = std::max(bobWalk, -headBobOffset(p, 1.3f).y);
            bobRun = std::max(bobRun, -headBobOffset(p, 3.4f).y);
        }
        const float bobRest = std::fabs(headBobOffset(0.37f, 0.0f).y);
        record("T359", "Stance blends: crouch / sprint over 0.2 s (smooth capsule height), stays crouched under an overhang; head bob",
               "crouch blend 0.5 at ~0.1 s and full at ~0.2 s, capsule 1.8 -> 1.25 m in steps < 7.5 cm (1/8 of the range), sprint full at ~0.2 s, crouched under a 1.5 m overhang, "
               "head bob 0.8..2 cm walking, <= 3 cm sprinting, 0 at rest",
               fmt("crouch 50 %% %.3f s, full %.3f s, height %.2f m (max step %.3f), overhang blend %.2f, sprint full %.3f s, bob walk %.3f run %.3f rest %.3f",
                   t50, tFull, crouchH, hPrevStep, lowBlend, sFull, bobWalk, bobRun, bobRest),
               std::fabs(t50 - 0.1f) < 0.02f && std::fabs(tFull - 0.2f) < 0.02f && std::fabs(crouchH - 1.25f) < 1e-4f && hPrevStep < 0.075f &&
                   lowBlend >= 0.99f && std::fabs(sFull - 0.2f) < 0.02f && bobWalk >= 0.008f && bobWalk <= 0.02f && bobRun <= 0.03f && bobRest == 0.0f);
    }
}
