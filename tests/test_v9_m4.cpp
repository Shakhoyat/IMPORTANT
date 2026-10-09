// v9 M4 (hinged doors, indoor camera, eye adaptation): T390-T399. CPU only.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/BarracksHall.h"
#include "objects/BaseLayout.h"
#include "objects/Bunker.h"
#include "scene/CameraRig.h"
#include "scene/CharacterMotor.h"
#include "scene/Door.h"
#include "scene/EyeAdapt.h"
#include "scene/Garrison.h"
#include "scene/Interact.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

constexpr float kTick = 1.0f / 60.0f;
namespace H = layout::hall;

bool inside(const Box& b, const Vec3& p, float shrink = 0.001f) {
    return p.x > b.min.x + shrink && p.x < b.max.x - shrink && p.y > b.min.y + shrink && p.y < b.max.y - shrink &&
           p.z > b.min.z + shrink && p.z < b.max.z - shrink;
}

// Points on the leaf (both faces, a grid over width x height) at an angle: none may lie inside a static solid / wall.
int leafInSolids(const DoorKinematics& d, float angle, const std::vector<Box>& boxes, Vec3* where = nullptr) {
    const HingeParams& p = d.params();
    const float phi = radians(d.yawDeg(angle));
    const Vec3 u{std::sin(phi), 0.0f, std::cos(phi)}, n{std::cos(phi), 0.0f, -std::sin(phi)};
    int hits = 0;
    for (int i = 0; i <= 12; ++i)
        for (int j = 0; j <= 10; ++j)
            for (float sn : {p.n0 + 0.002f, p.n1 - 0.002f}) {
                const float su = p.u0 + 0.002f + (p.width - 0.004f) * static_cast<float>(i) / 12.0f;
                const float y = p.hinge.y + p.bottom + 0.002f + (p.height - 0.004f) * static_cast<float>(j) / 10.0f;
                const Vec3 q = Vec3{p.hinge.x, y, p.hinge.z} + u * su + n * sn;
                for (const Box& b : boxes)
                    if (inside(b, q, 0.003f)) {
                        if (where && hits == 0) *where = q;
                        ++hits;
                        break;
                    }
            }
    return hits;
}

struct DoorTrace {
    std::vector<float> angle;
    std::vector<DoorKinematics::State> state;
};

DoorTrace swing(DoorKinematics& d, float seconds, float t0 = 0.0f) {
    DoorTrace tr;
    for (float t = 0.0f; t < seconds; t += kTick) {
        d.step(t0 + t, kTick);
        tr.angle.push_back(d.angleDeg());
        tr.state.push_back(d.state());
    }
    return tr;
}

uint64_t mix(uint64_t h, float v) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    return (h ^ b) * 1099511628211ull;
}

}  // namespace

TS_TEST_GROUP(V9M4) {
    const HingeParams hallP = hallDoorHinge();

    // ---- T390: door state machine: Closed -> Opening -> Open (latched) -> Closing -> Closed; reversal; auto open / close --
    {
        DoorKinematics d(hallP);
        std::string seq = d.stateName();
        auto note = [&](const DoorTrace& tr) {
            for (DoorKinematics::State s : tr.state) {
                const char* n = s == DoorKinematics::State::Closed ? "CLOSED" : s == DoorKinematics::State::Opening ? "OPENING"
                                : s == DoorKinematics::State::Open ? "OPEN" : "CLOSING";
                if (seq.size() < std::strlen(n) || seq.compare(seq.size() - std::strlen(n), std::string::npos, n) != 0) seq += std::string(">") + n;
            }
        };
        d.toggle();
        note(swing(d, 3.0f));
        const bool openLatched = d.state() == DoorKinematics::State::Open && d.angleDeg() == hallP.openDeg && d.velocityDeg() == 0.0f;
        d.toggle();
        note(swing(d, 3.0f));
        const bool closedLatched = d.state() == DoorKinematics::State::Closed && d.angleDeg() == 0.0f;
        // reversal mid-swing: open 0.3 s, then close
        DoorKinematics r(hallP);
        r.toggle();
        swing(r, 0.3f);
        const float midAngle = r.angleDeg();
        r.toggle();
        const DoorTrace back = swing(r, 3.0f);
        float peak = midAngle;
        for (float a : back.angle) peak = std::max(peak, a);
        const bool reversed = r.state() == DoorKinematics::State::Closed && peak < midAngle + 25.0f;
        // auto: a request opens it, it closes closeDelay after the last request (nobody in the sweep)
        DoorKinematics a(hallP);
        a.request(0.0f);
        const DoorTrace ao = swing(a, hallP.closeDelay - 0.1f);
        const bool stillOpen = a.targetOpen() && a.autoOpened();
        swing(a, 3.5f, hallP.closeDelay - 0.1f);
        const bool autoClosed = a.state() == DoorKinematics::State::Closed;
        // a manual open stays open past the delay
        DoorKinematics m(hallP);
        m.toggle();
        swing(m, 8.0f);
        const bool manualStays = m.state() == DoorKinematics::State::Open;
        record("T390", "Hinged door state machine: E toggles, the leaf latches on its stops, reverses mid-swing, auto-opens for a "
                       "walker and closes itself closeDelay later; a door the player opened stays open",
               "CLOSED>OPENING>OPEN>CLOSING>CLOSED, latched at 180 / 0 deg, reversal at 0.3 s comes back without a further swing "
               "(< 25 deg past the reversal point), auto open then closed by delay + 3.5 s, manual open still open after 8 s",
               fmt("%s, open latched %d, closed latched %d, reversal at %.1f deg peaks %.1f -> %s, auto open %d -> closed %d, manual %d",
                   seq.c_str(), openLatched, closedLatched, midAngle, peak, r.stateName(), stillOpen, autoClosed, manualStays),
               seq == "CLOSED>OPENING>OPEN>CLOSING>CLOSED" && openLatched && closedLatched && reversed && stillOpen && autoClosed &&
                   manualStays && !ao.angle.empty());
    }

    // ---- T391: the hinge spring: critically damped -- monotone, no overshoot, the analytic rise time, any tick rate -----
    {
        // Unclamped spring (the stops never act): the exact step response must stay below the goal and match
        // 1 - (1 + w t) e^(-w t).
        HingeParams p = hallP;
        DoorKinematics d(p);
        d.toggle();
        float prev = 0.0f, t95 = -1.0f, t = 0.0f, maxErr = 0.0f;
        bool monotone = true, below = true;
        while (t < 4.0f) {
            d.step(t, kTick);
            t += kTick;
            const float a = d.angleDeg();
            if (a + 1e-4f < prev) monotone = false;
            if (a > p.openDeg + 1e-4f) below = false;
            if (t95 < 0.0f && a >= 0.95f * p.openDeg) t95 = t;
            if (d.state() != DoorKinematics::State::Open) {
                const float w = p.omega, exact = p.openDeg * (1.0f - (1.0f + w * t) * std::exp(-w * t));
                maxErr = std::max(maxErr, std::fabs(a - exact));
            }
            prev = a;
        }
        const float t95Exact = critRiseTime(p.omega, 0.95f);
        // the same swing ticked at 30 / 144 Hz reaches the same angle at t = 0.5 s
        auto at = [&](float hz) {
            DoorKinematics q(p);
            q.toggle();
            const float dt = 1.0f / hz;
            for (int k = 0; k < static_cast<int>(std::lround(0.5f * hz)); ++k) q.step(k * dt, dt);
            return q.angleDeg();
        };
        const float a30 = at(30.0f), a60 = at(60.0f), a144 = at(144.0f);
        // closing from open: monotone down to 0, never below
        DoorKinematics c(p);
        c.snap(p.openDeg);
        c.toggle();
        float cprev = p.openDeg, cmin = p.openDeg, tClose = -1.0f;
        bool cmono = true;
        for (float tc = 0.0f; tc < 4.0f; tc += kTick) {
            c.step(tc, kTick);
            cmono = cmono && c.angleDeg() <= cprev + 1e-4f;
            cmin = std::min(cmin, c.angleDeg());
            cprev = c.angleDeg();
            if (tClose < 0.0f && c.state() == DoorKinematics::State::Closed) tClose = tc + kTick;
        }
        record("T391", "Hinge spring (zeta = 1): monotone opening and closing, no overshoot past either stop, matches the exact "
                       "response 1 - (1 + w t) e^(-w t), tick-rate independent",
               "hall door w 4.2 1/s: 95 % open after 4.74 / w = 1.13 s (+- 1 tick), error vs the exact curve < 0.01 deg, 30 / 60 / 144 Hz "
               "within 0.05 deg at 0.5 s, closing never below 0",
               fmt("95 %% after %.3f s (exact %.3f), max error %.4f deg, monotone %d, below the stop %d, at 0.5 s %.3f / %.3f / %.3f deg, "
                   "closing monotone %d min %.3f, latched closed after %.2f s",
                   t95, t95Exact, maxErr, monotone, below, a30, a60, a144, cmono, cmin, tClose),
               std::fabs(t95 - t95Exact) <= kTick + 1e-4f && maxErr < 0.01f && monotone && below && std::fabs(a30 - a60) < 0.05f &&
                   std::fabs(a144 - a60) < 0.05f && cmono && cmin >= 0.0f && tClose > 0.0f);
    }

    // ---- T392: the leaf in the real buildings: through the whole swing it never passes through a wall, jamb or fitting;
    //      closed it fills the opening, open it rests against the hall's outer wall (where the routes keep clear) -----------
    {
        Scene s;
        const WalkSpace& ws = s.walkSpace();
        std::vector<Box> boxes = s.staticSolids();
        boxes.insert(boxes.end(), ws.walls().begin(), ws.walls().end());
        DoorPortal* hall = s.hallDoor();
        DoorPortal* gen = s.generatorDoor();
        int hallHits = 0, genHits = 0;
        Vec3 firstHit;
        float worstAngle = -1.0f;
        if (hall && gen) {
            for (float a = 0.0f; a <= hall->door().params().openDeg + 1e-3f; a += 2.0f) {
                const int h = leafInSolids(hall->door(), a, boxes, &firstHit);
                if (h && worstAngle < 0.0f) worstAngle = a;
                hallHits += h;
            }
            for (float a = 0.0f; a <= gen->door().params().openDeg + 1e-3f; a += 2.0f) genHits += leafInSolids(gen->door(), a, boxes, &firstHit);
        }
        // closed: the leaf boxes span the opening (x 22.86 .. 24.14) inside the wall (z -8.82 .. -8.6); open: in front of
        // the outer wall face (z > -8.6), east of the opening
        Box closedB{{1e9f, 0, 1e9f}, {-1e9f, 0, -1e9f}}, openB = closedB;
        if (hall) {
            for (const Box& b : hall->door().panelBoxes(0.0f)) {
                closedB.min.x = std::min(closedB.min.x, b.min.x), closedB.max.x = std::max(closedB.max.x, b.max.x);
                closedB.min.z = std::min(closedB.min.z, b.min.z), closedB.max.z = std::max(closedB.max.z, b.max.z);
            }
            for (const Box& b : hall->door().restOpenBoxes()) {
                openB.min.x = std::min(openB.min.x, b.min.x), openB.max.x = std::max(openB.max.x, b.max.x);
                openB.min.z = std::min(openB.min.z, b.min.z), openB.max.z = std::max(openB.max.z, b.max.z);
            }
        }
        const float a0 = H::kDoorX - 0.5f * H::kDoorW, a1 = H::kDoorX + 0.5f * H::kDoorW;
        const bool fills = closedB.min.x > a0 && closedB.min.x < a0 + 0.02f && closedB.max.x < a1 && closedB.max.x > a1 - 0.02f &&
                           closedB.min.z > H::kIZ1 && closedB.max.z < H::kZ1;
        const bool rests = openB.min.z > H::kZ1 && openB.max.z < H::kZ1 + 0.1f && openB.min.x >= a1 && openB.max.x < a1 + 1.4f;
        record("T392", "Door leaves in the real buildings: no wall / jamb / fitting in the swing (hall 0..180 deg, generator room "
                       "0..90 deg); the closed hall leaf fills the opening, the open one rests against the outer wall",
               "0 leaf sample points inside a solid at any angle (2 deg steps, 286 points per angle); closed x within 2 cm of both "
               "jambs inside the wall, open in front of the wall face east of the opening",
               fmt("hall %d, generator %d hits%s; closed x %.3f..%.3f z %.3f..%.3f, open x %.3f..%.3f z %.3f..%.3f", hallHits, genHits,
                   worstAngle >= 0.0f ? fmt(" (first at %.0f deg, %.2f %.2f %.2f)", worstAngle, firstHit.x, firstHit.y, firstHit.z).c_str() : "",
                   closedB.min.x, closedB.max.x, closedB.min.z, closedB.max.z, openB.min.x, openB.max.x, openB.min.z, openB.max.z),
               hall && gen && hallHits == 0 && genHits == 0 && fills && rests);
    }

    // ---- T393: a body in the swing stops the leaf (it never swings into a person), it goes on once the way is clear;
    //      an auto-opened door does not close on someone standing in its sweep -----------------------------------------
    {
        DoorKinematics d(hallP);
        DoorBody body{{H::kDoorX + 0.2f, H::kFloorY - 0.12f, H::kZ1 + 0.75f}, 0.25f, 1.8f, 42};   // in front, within the sweep
        d.setBodies({body});
        d.toggle();
        bool everHit = false, blocked = false;
        int by = -1;
        float stopAngle = 0.0f;
        for (float t = 0.0f; t < 3.0f; t += kTick) {
            d.step(t, kTick);
            everHit = everHit || d.hits(body, d.angleDeg());
            if (d.blocked() && !blocked) {
                blocked = true;
                by = d.blockedBy();
                stopAngle = d.angleDeg();
            }
        }
        const float held = d.angleDeg();
        d.setBodies({});   // the person steps away
        swing(d, 3.0f, 3.0f);
        const bool resumed = d.state() == DoorKinematics::State::Open;
        // auto close waits for the sweep to be clear
        DoorKinematics a(hallP);
        a.request(0.0f);
        swing(a, 2.0f);
        a.setBodies({{{H::kDoorX + 0.6f, H::kFloorY - 0.12f, H::kZ1 + 0.5f}, 0.25f, 1.8f, 7}});
        swing(a, 6.0f, 2.0f);
        const bool waited = a.targetOpen();
        a.setBodies({});
        swing(a, 4.0f, 8.0f);
        const bool closedAfter = a.state() == DoorKinematics::State::Closed;
        record("T393", "Bodies and the leaf: a person in the swing stops it (blockedBy = his id), the leaf never enters his "
                       "cylinder, it opens on when he steps away; an auto-opened door waits for its sweep to clear before closing",
               "blocked by id 42 part-way, no overlap at any tick, open after he left; auto close held while someone stands in "
               "the sweep, closed after he left",
               fmt("blocked %d by %d at %.1f deg (held %.1f), overlap %d, then %s; auto: held open %d, closed after %d", blocked, by,
                   stopAngle, held, everHit, d.stateName(), waited, closedAfter),
               blocked && by == 42 && stopAngle > 1.0f && stopAngle < hallP.openDeg - 1.0f && !everHit && resumed && waited && closedAfter);
    }

    // ---- T394: the player through the hall door and back, first and third person: never clips; the indoor arm glides ----
    {
        Scene s;
        const WalkSpace* ws = &s.walkSpace();
        DoorPortal* hall = s.hallDoor();
        const std::vector<Box>& statics = s.staticSolids();
        int eyeInFp = 0, eyeInTp = 0, bodyInLeaf = 0, ticks = 0;
        float maxArmStep = 0.0f, minZ = 1e9f, maxZBack = -1e9f, minArm = 1e9f, maxArm = 0.0f;
        bool closedBlocks = false;
        if (hall) {
            DoorKinematics& d = hall->door();
            for (int run = 0; run < 3; ++run) {   // 0: first person, 1: third person (orbiting camera), 2: third, leaf half open
                d.setBodies({});
                d.snap(run == 2 ? 95.0f : d.params().openDeg);
                if (run == 2) d.setTarget(true, true), d.snap(95.0f);
                CharacterMotor m;
                m.foot.radius = 0.25f;
                m.reset(H::kDoorX - 0.15f, H::kZ1 + 3.0f, layout::kApronLevel);
                ThirdPersonCam tp;
                IndoorArmBlend blend;
                float prevArm = -1.0f, yaw = 180.0f;
                bool goingIn = true;
                for (int k = 0; k < 60 * 14; ++k) {
                    const FootBody& b = m.body();
                    const Vec3 feet{b.x, b.feetY, b.z};
                    if (goingIn && b.z < H::kIZ1 - 2.2f) goingIn = false, yaw = 0.0f;
                    if (!goingIn && b.z > H::kZ1 + 3.0f) break;
                    std::vector<Box> solids;
                    for (const Box& bx : statics)
                        if (std::fabs(bx.min.x + bx.max.x - 2.0f * b.x) * 0.5f < 4.0f + 0.5f * (bx.max.x - bx.min.x) &&
                            std::fabs(bx.min.z + bx.max.z - 2.0f * b.z) * 0.5f < 4.0f + 0.5f * (bx.max.z - bx.min.z))
                            solids.push_back(bx);
                    const std::vector<Box> leaf = d.panelBoxes();
                    std::vector<Box> all = solids;
                    all.insert(all.end(), leaf.begin(), leaf.end());
                    MotorInput in;
                    in.forward = 1.0f;
                    in.yawDeg = yaw;
                    m.step(in, ws, all, kTick);
                    ++ticks;
                    minZ = std::min(minZ, m.body().z);
                    if (!goingIn) maxZBack = std::max(maxZBack, m.body().z);
                    const Vec3 f2{m.body().x, m.body().feetY, m.body().z};
                    if (d.hits({f2, 0.2f, 1.8f, 0}, d.angleDeg())) ++bodyInLeaf;
                    std::vector<Box> check = all;
                    check.insert(check.end(), ws->walls().begin(), ws->walls().end());
                    if (run == 0) {
                        const Vec3 eye = f2 + Vec3{0.0f, 1.52f, 0.0f};
                        for (const Box& bx : check)
                            if (inside(bx, eye)) ++eyeInFp;
                    } else {
                        const bool indoor = ws->ceilingHeight(f2.x, f2.z, f2.y) < 1e8f;
                        blend.step(indoor, kTick);
                        const ThirdPersonParams p = indoorArmParams(ThirdPersonParams::soldier(), blend.blend);
                        if (prevArm >= 0.0f) maxArmStep = std::max(maxArmStep, std::fabs(p.arm - prevArm));
                        prevArm = p.arm;
                        minArm = std::min(minArm, p.arm);
                        maxArm = std::max(maxArm, p.arm);
                        const Vec3 pivot = f2 + Vec3{0.0f, 1.45f, 0.0f};
                        if (!tp.valid()) tp.reset(pivot, yaw, -10.0f);
                        tp.look(5.0f, run == 2 ? 0.3f * std::sin(0.05f * k) * 10.0f : 0.0f, p);   // keeps orbiting
                        const ArmCast cast = [&](const Vec3& o, const Vec3& dir, float len, float r) {
                            return sphereCast(ws, all, o, dir, len, r, f2.y);
                        };
                        const RigPose pose = tp.update(pivot, kTick, false, p, cast);
                        for (const Box& bx : check)
                            if (inside(bx, pose.eye)) ++eyeInTp;
                    }
                    (void)feet;
                }
            }
            // a closed door stops the walker at the leaf
            d.snap(0.0f);
            d.setTarget(false, true);
            CharacterMotor m;
            m.foot.radius = 0.25f;
            m.reset(H::kDoorX, H::kZ1 + 2.0f, layout::kApronLevel);
            for (int k = 0; k < 240; ++k) {
                std::vector<Box> all = statics;
                for (const Box& bx : d.panelBoxes()) all.push_back(bx);
                MotorInput in;
                in.forward = 1.0f;
                in.yawDeg = 180.0f;
                m.step(in, ws, all, kTick);
            }
            closedBlocks = m.body().z > H::kZ1 + 0.2f;
        }
        record("T394", "Player through the hall doorway and back (motor + walls + door leaf boxes), first person and third person with "
                       "an orbiting spring arm (door open and half open): no eye inside a wall / solid / leaf, no body in the leaf; the "
                       "indoor arm blends 3.1 -> 1.9 m without a jump; a closed door stops the walker",
               "in to z < -11 and out again in all 3 runs, 0 eye-in-box ticks, 0 body-leaf overlaps, arm change per tick < 0.06 m "
               "(v8: 1.2 m in one frame), closed door holds the walker outside",
               fmt("%d ticks, deepest z %.2f, back out to z %.2f, eye in box fp %d tp %d, body in leaf %d, arm %.2f..%.2f max step %.4f m, "
                   "closed door stops %d",
                   ticks, minZ, maxZBack, eyeInFp, eyeInTp, bodyInLeaf, minArm, maxArm, maxArmStep, closedBlocks),
               hall && minZ < H::kIZ1 - 2.0f && maxZBack > H::kZ1 + 2.5f && eyeInFp == 0 && eyeInTp == 0 && bodyInLeaf == 0 &&
                   maxArmStep < 0.06f && minArm < 1.95f && maxArm > 3.05f && closedBlocks);
    }

    // ---- T395 / T398: residents pass the (initially closed) hall door; the run is deterministic ------------------------
    {
        struct Out {
            int crossings = 0, opens = 0, closes = 0, overlaps = 0, blockedTicks = 0;
            float longestWait = 0.0f, maxOpenWhileFar = 0.0f;
            uint64_t hash = 1469598103934665603ull;
            bool startClosed = false;
        };
        auto runScene = [&](float seconds) {
            Out o;
            SceneOptions opt = SceneOptions::v6(7, 10.5f, false);
            opt.residents = Garrison::kMaxResidents;
            Scene s(opt);
            Garrison* g = s.garrison();
            DoorPortal* hall = s.hallDoor();
            if (!g || !hall) return o;
            DoorKinematics& d = hall->door();
            o.startClosed = d.state() == DoorKinematics::State::Closed;
            const int n = g->count();
            std::vector<float> side(static_cast<size_t>(n), 0.0f), wait(static_cast<size_t>(n), 0.0f);
            for (int i = 0; i < n; ++i) side[static_cast<size_t>(i)] = d.sideOf(g->unit(i)->position());
            DoorKinematics::State prev = d.state();
            for (float t = 0.0f; t < seconds; t += kTick) {
                s.update(kTick, Detection{});
                if (prev == DoorKinematics::State::Closed && d.state() == DoorKinematics::State::Opening) ++o.opens;
                if (prev != DoorKinematics::State::Closed && d.state() == DoorKinematics::State::Closed) ++o.closes;
                prev = d.state();
                if (d.blocked()) ++o.blockedTicks;
                o.hash = mix(o.hash, d.angleDeg());
                for (int i = 0; i < n; ++i) {
                    const Soldier* u = g->unit(i);
                    const Vec3 p = u->position();
                    o.hash = mix(mix(o.hash, p.x), p.z);
                    if (d.hits({p, 0.15f, 1.8f, i}, d.angleDeg())) {
                        ++o.overlaps;
                        if (std::getenv("TS_M4_DEBUG"))
                            std::printf("DBG t %.2f soldier %d at %.2f %.2f %.2f act %s door %s %.1f vel %.1f blocked %d by %d | %s\n", t, i, p.x, p.y, p.z,
                                        activityName(u->activity()), d.stateName(), d.angleDeg(), d.velocityDeg(), d.blocked(), d.blockedBy(),
                                        u->debugState().c_str());
                    }
                    if (std::getenv("TS_M4_DEBUG") && d.blocked() && i == 0)
                        std::printf("DBG blocked t %.2f door %s %.1f by %d at %.2f %.2f act %s | %s\n", t, d.stateName(), d.angleDeg(), d.blockedBy(),
                                    d.blockedBy() >= 0 ? g->unit(d.blockedBy())->position().x : 0.0f,
                                    d.blockedBy() >= 0 ? g->unit(d.blockedBy())->position().z : 0.0f,
                                    d.blockedBy() >= 0 ? activityName(g->unit(d.blockedBy())->activity()) : "-",
                                    d.blockedBy() >= 0 ? g->unit(d.blockedBy())->debugState().c_str() : "");
                    const float sd = d.sideOf(p);
                    const size_t k = static_cast<size_t>(i);
                    if ((sd > 0.0f) != (side[k] > 0.0f) && d.inDoorway(p, 0.8f)) ++o.crossings;
                    side[k] = sd;
                    const Vec3 c = d.doorwayCentre();
                    const bool nearDoor = std::hypot(p.x - c.x, p.z - c.z) < 3.0f;
                    // waits the door causes (stopped for the leaf, or holding the leaf up); the PT-bar queueing of v8 is not counted
                    const bool forDoor = u->waitingForDoor() || d.blockedBy() == i;
                    wait[k] = (nearDoor && forDoor) ? wait[k] + kTick : 0.0f;
                    o.longestWait = std::max(o.longestWait, wait[k]);
                }
            }
            return o;
        };
        const Out a = runScene(300.0f);
        record("T395", "Residents and the hall door (seed 7, 10:30, 6 residents, 300 s): they walk between the hall and the PT area "
                       "through the door that starts closed; it opens for them (route crosses the doorway within 4.5 m) and closes "
                       "behind them",
               "door starts closed, >= 2 crossings, every auto open followed by a close (closes >= opens - 1), no leaf-body "
               "overlap (> 10 cm), nobody waits for the door longer than 6 s",
               fmt("start closed %d, %d crossings, %d opens, %d closes, %d overlap ticks, %d blocked ticks, longest wait %.1f s",
                   a.startClosed, a.crossings, a.opens, a.closes, a.overlaps, a.blockedTicks, a.longestWait),
               a.startClosed && a.crossings >= 2 && a.opens >= 1 && a.closes >= a.opens - 1 && a.overlaps == 0 && a.longestWait < 6.0f);
        const Out b = runScene(120.0f), c = runScene(120.0f);
        // eye adaptation part of the determinism: the same tick sequence gives the same bias / adapted luminance bits
        Scene es;
        auto eyeRun = [&]() {
            LocalExposureBias bias;
            float L = -3.0f;
            uint64_t h = 1469598103934665603ull;
            for (int k = 0; k < 600; ++k) {
                const float z = H::kZ1 + 3.0f - 7.0f * static_cast<float>(k) / 600.0f;   // walk through the doorway
                bias.step(localExposureBiasTarget(&es.walkSpace(), {H::kDoorX, H::kFloorY + 1.5f, z}), kTick);
                L = adaptLogLuminance(L, z < H::kIZ1 ? -4.7f : -3.0f, kTick);
                h = mix(mix(h, bias.ev()), L);
            }
            return h;
        };
        const uint64_t e1 = eyeRun(), e2 = eyeRun();
        record("T398", "Determinism under --seed: two runs of the scene (seed 7, 120 s: door angle, every soldier's position per tick) "
                       "and of the eye adaptation (bias + adapted luminance per tick) are bit-identical",
               "equal FNV hashes", fmt("scene %016llx / %016llx, eye %016llx / %016llx", static_cast<unsigned long long>(b.hash),
                                       static_cast<unsigned long long>(c.hash), static_cast<unsigned long long>(e1),
                                       static_cast<unsigned long long>(e2)),
               b.hash == c.hash && e1 == e2 && b.hash != 1469598103934665603ull);
    }

    // ---- T396: eye adaptation time constants and asymmetry -------------------------------------------------------------
    {
        const EyeAdaptParams p;
        auto stepResponse = [&](float from, float to, float frac, float dt) {
            float L = from, t = 0.0f;
            while (std::fabs(L - from) < frac * std::fabs(to - from) && t < 20.0f) {
                L = adaptLogLuminance(L, to, dt, p);
                t += dt;
            }
            return t;
        };
        const float up63 = stepResponse(-5.0f, -3.0f, 0.632f, kTick), down63 = stepResponse(-3.0f, -5.0f, 0.632f, kTick);
        const float up90 = stepResponse(-5.0f, -3.0f, 0.9f, kTick), down90 = stepResponse(-3.0f, -5.0f, 0.9f, kTick);
        // frame-rate independence (constant input): 1 s of adaptation at 30 / 60 / 144 Hz
        auto oneSecond = [&](float hz) {
            float L = -3.0f;
            const int n = static_cast<int>(std::lround(hz));
            for (int k = 0; k < n; ++k) L = adaptLogLuminance(L, -5.0f, 1.0f / hz, p);
            return L;
        };
        const float l30 = oneSecond(30.0f), l60 = oneSecond(60.0f), l144 = oneSecond(144.0f);
        // exposure range: outdoors +-1.5 EV; with the hall's bias the eye opens up to 1.5 + 1.25 EV; never down past -1.5
        const float evDark = exposureEv(-2.8f, -8.0f, 0.0f, p), evDarkIn = exposureEv(-2.8f, -8.0f, kHallExposureBiasEv, p);
        const float evBright = exposureEv(-2.8f, 2.0f, kHallExposureBiasEv, p), evMid = exposureEv(-2.8f, -3.3f, kHallExposureBiasEv, p);
        record("T396", "Eye adaptation: asymmetric time constants (brighter view 0.35 s, darker view 1.1 s: light adaptation fast, dark "
                       "adaptation slow), frame-rate independent; the local bias widens only the opening-up side of the range",
               "63 % at tau (+- 1 tick), 90 % at 2.3 tau, darker / brighter ratio 3.1, 30 / 60 / 144 Hz within 1e-4 after 1 s; EV "
               "clamp +1.5 outdoors, +2.75 in the hall, -1.5 for a bright view, unclamped in between",
               fmt("brighter 63 %% %.3f s / 90 %% %.3f s, darker 63 %% %.3f s / 90 %% %.3f s, ratio %.2f; 1 s at 30/60/144 Hz %.5f %.5f %.5f; "
                   "EV dark %+.2f, dark in hall %+.2f, bright %+.2f, mid %+.2f",
                   up63, up90, down63, down90, down63 / up63, l30, l60, l144, evDark, evDarkIn, evBright, evMid),
               std::fabs(up63 - p.tauUp) <= kTick + 1e-4f && std::fabs(down63 - p.tauDown) <= kTick + 1e-4f &&
                   std::fabs(up90 - adaptTime(p.tauUp, 0.9f)) <= kTick + 1e-4f && std::fabs(down90 - adaptTime(p.tauDown, 0.9f)) <= kTick + 1e-4f &&
                   down63 / up63 > 2.9f && std::fabs(l30 - l60) < 1e-4f && std::fabs(l144 - l60) < 1e-4f && evDark == 1.5f &&
                   std::fabs(evDarkIn - (1.5f + kHallExposureBiasEv)) < 1e-6f && evBright == -1.5f && std::fabs(evMid - 0.5f) < 1e-5f);
    }

    // ---- T397: local exposure bias of the indoor light volumes: 0 outdoors and in the bunker, the hall's bias deep inside,
    //      a smooth ramp over the feather; walking in it rises slowly (dark adaptation), walking out it falls fast ----------
    {
        Scene s;
        const WalkSpace* ws = &s.walkSpace();
        const float out = localExposureBiasTarget(ws, {H::kDoorX, H::kFloorY + 1.5f, H::kZ1 + 2.0f});
        const float deep = localExposureBiasTarget(ws, {23.0f, H::kFloorY + 1.5f, -11.8f});
        const float edge = localExposureBiasTarget(ws, {H::kDoorX, H::kFloorY + 1.5f, H::kIZ1 - 0.3f});
        const float bunker = localExposureBiasTarget(ws, {0.0f, layout::kBunkerFloorY + 1.5f, -4.0f});
        // walk in at 1.3 m/s from 2 m outside, stop 3 m inside; then walk out
        LocalExposureBias b;
        float t90 = -1.0f, t = 0.0f, prev = 0.0f;
        bool mono = true;
        float z = H::kZ1 + 2.0f;
        for (; t < 6.0f; t += kTick) {
            z = std::max(z - 1.3f * kTick, H::kIZ1 - 3.0f);
            b.step(localExposureBiasTarget(ws, {H::kDoorX, H::kFloorY + 1.5f, z}), kTick);
            mono = mono && b.ev() >= prev - 1e-6f;
            prev = b.ev();
            if (t90 < 0.0f && b.ev() >= 0.9f * kHallExposureBiasEv) t90 = t + kTick;
        }
        const float enterTime = (2.0f + 0.22f) / 1.3f;   // reaching the volume's face
        float t10 = -1.0f;
        for (float u = 0.0f; u < 4.0f; u += kTick) {
            b.step(0.0f, kTick);
            if (t10 < 0.0f && b.ev() <= 0.1f * kHallExposureBiasEv) t10 = u + kTick;
        }
        record("T397", "Local exposure bias (indoor light volumes): 0 outdoors and in the bunker (its v8 night look stays), the hall's "
                       "+1.25 EV deep inside, ramping over 0.8 m; walking in it opens up over ~2-3 s, stepping out it closes in < 1 s",
               "out 0, bunker 0, deep 1.25, edge strictly between, monotone rise, 90 % within 3.5 s of reaching the volume, back to 10 % "
               "within 1 s outside",
               fmt("out %.2f, bunker %.2f, deep %.2f, edge %.2f; 90 %% %.2f s after entering the volume (walk %.2f s), monotone %d, fall to "
                   "10 %% %.2f s",
                   out, bunker, deep, edge, t90 - enterTime, enterTime, mono, t10),
               out == 0.0f && bunker == 0.0f && std::fabs(deep - kHallExposureBiasEv) < 1e-5f && edge > 0.0f && edge < kHallExposureBiasEv &&
                   mono && t90 > 0.0f && t90 - enterTime < 3.5f && t10 > 0.0f && t10 < 1.0f);
    }

    // ---- T399: the prompts at the hall door: "[E] Open door" while it is shut (the handle is nearer than the room), the
    //      move through it once open, "[E] Close door" at the open leaf; the handle is on the player's side -----------------
    {
        Scene s;
        DoorPortal* hall = s.hallDoor();
        std::string closedPrompt, openPrompt, leafPrompt, insidePrompt;
        bool handleSides = false;
        if (hall) {
            DoorKinematics& d = hall->door();
            const WalkSpace& ws = s.walkSpace();
            const std::vector<Box>& solids = s.staticSolids();
            const LineOfSight los = [&](const Vec3& a, const Vec3& b) { return segmentClear(&ws, solids, a, b); };
            auto pick = [&](const Vec3& feet, float yawDeg, bool in) {
                Interactable door;
                door.name = "door";
                door.point = d.handlePoint(feet);
                door.reach = 2.5f;
                door.action = d.targetOpen() ? InteractAction::CloseDoor : InteractAction::OpenDoor;
                Interactable hallIt;
                hallIt.name = "Barracks";
                hallIt.action = in ? InteractAction::ExitBuilding : InteractAction::EnterBuilding;
                hallIt.point = {H::kDoorX, H::kFloorY + 1.1f, in ? H::kZ1 + 0.3f : H::kIZ1 - 0.3f};
                hallIt.reach = 3.0f;
                InteractView v;
                v.player = feet;
                v.eye = feet + Vec3{0.0f, 1.55f, 0.0f};
                v.forward = {std::sin(radians(yawDeg)), 0.0f, std::cos(radians(yawDeg))};
                std::vector<Interactable> items{door, hallIt};
                const InteractPick p = pickInteractable(items, v, los);
                return p.index < 0 ? std::string("-") : interactPrompt(items[static_cast<size_t>(p.index)]);
            };
            const Vec3 front{H::kDoorX - 0.3f, layout::kApronLevel, H::kZ1 + 1.6f};
            d.snap(0.0f);
            d.setTarget(false, true);
            closedPrompt = pick(front, 180.0f, false);
            const Vec3 hIn = d.handlePoint({H::kDoorX, 0.0f, H::kZ1 - 2.0f}), hOut = d.handlePoint(front);
            handleSides = hOut.z > H::kZ1 && hIn.z < H::kZ1 && std::fabs(hIn.x - hOut.x) < 1e-4f;
            d.setTarget(true, true);
            d.snap(d.params().openDeg);
            openPrompt = pick(front, 180.0f, false);
            leafPrompt = pick({H::kDoorX + 1.5f, layout::kApronLevel, H::kZ1 + 1.0f}, 180.0f, false);
            d.snap(0.0f);
            d.setTarget(false, true);
            insidePrompt = pick({H::kDoorX + 0.2f, H::kFloorY, H::kIZ1 - 1.4f}, 0.0f, true);
        }
        record("T399", "Hall door prompts with the real geometry: shut -> \"[E] Open door\" (from outside and from inside), open -> "
                       "\"[E] Enter Barracks\" through the doorway, \"[E] Close door\" at the open leaf; the handle is on the player's side",
               "[E] Open door / [E] Enter Barracks / [E] Close door / [E] Open door, handles on both faces",
               fmt("%s / %s / %s / %s, handles %d", closedPrompt.c_str(), openPrompt.c_str(), leafPrompt.c_str(), insidePrompt.c_str(),
                   handleSides),
               closedPrompt == "[E] Open door" && openPrompt == "[E] Enter Barracks" && leafPrompt == "[E] Close door" &&
                   insidePrompt == "[E] Open door" && handleSides);
    }
}
