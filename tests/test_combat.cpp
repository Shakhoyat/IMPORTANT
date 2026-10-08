// Combat simulation, intruder life cycle and the UAV launcher (T100-T119). CPU only.
#include <cmath>
#include <set>

#include "TestKit.h"
#include "ai/DroneController.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"
#include "scene/Scene.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr float kDt = 1.0f / 60.0f;

bool finiteVec(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Steps a scene with no detections and no input.
void run(Scene& s, float seconds) {
    const Detection none;
    for (float t = 0.0f; t < seconds; t += kDt) s.update(kDt, none);
}

}  // namespace

TS_TEST_GROUP(Combat) {
    // T100: a round launched sideways turns onto a fixed laser spot and detonates next to it.
    {
        Combat c;
        const Vec3 spot{6.0f, terrainHeight(6.0f, 4.0f), 4.0f};
        c.setDesignator(0, spot, true);
        // Launched as the UAV does (towards the spot), but 30 degrees off the line of sight: guidance must correct.
        const Vec3 start{0.0f, 6.0f, 0.0f};
        const Vec3 los = normalize(spot - start);
        const Vec3 side = normalize(cross(los, Vec3{0, 1, 0}));
        c.fireMissile(start, normalize(los * std::cos(radians(30.0f)) + side * std::sin(radians(30.0f))), 0, 0);
        BlastEvent hit;
        bool blasted = false;
        float t = 0.0f;
        for (; t < 8.0f && !blasted; t += kDt) {
            c.update(kDt, t);
            for (const BlastEvent& b : c.takeBlasts()) {
                hit = b;
                blasted = true;
            }
        }
        const float miss = length(hit.position - spot);
        record("T100", "Homing round launched 30 deg off the line of sight detonates within 0.6 m of the laser spot",
               "blast within 0.6 m, before the 6 s self-destruct",
               fmt("blast %s at %.2f s, %.2f m from the spot", blasted ? "yes" : "no", t, miss), blasted && miss < 0.6f && t < 6.0f);
    }
    // T101: tank shell follows the ballistic parabola: horizontal range on flat ground = v^2 sin(2a) / g.
    {
        Combat c;
        const float a = radians(4.0f), v = Combat::kShellSpeed;
        const Vec3 start{-30.0f, 40.0f, 0.0f};  // high above the ground: measure where it falls back to y = 40
        c.fireShell(start, {std::cos(a), std::sin(a), 0.0f}, v, 1);
        float range = -1.0f, prevX = start.x, prevY = start.y;
        for (float t = 0.0f; t < 4.0f && range < 0.0f; t += 1.0f / 240.0f) {
            c.update(1.0f / 240.0f, t);
            if (c.projectiles().empty()) break;
            const Projectile& p = c.projectiles().front();
            if (p.velocity.y < 0.0f && p.position.y <= start.y) {
                const float f = (prevY - start.y) / std::max(prevY - p.position.y, 1e-6f);
                range = (prevX + f * (p.position.x - prevX)) - start.x;
            }
            prevX = p.position.x;
            prevY = p.position.y;
        }
        const float expect = v * v * std::sin(2.0f * a) / 9.81f;
        record("T101", "Tank shell is ballistic: range on level ground matches v^2 sin(2a)/g within 2%",
               fmt("%.1f m (v %.0f m/s, 4 deg)", expect, v), fmt("%.1f m (%.2f%%)", range, 100.0f * (range - expect) / expect),
               range > 0.0f && std::fabs(range - expect) < 0.02f * expect);
    }
    // T102: a 1000 m/s tracer moves ~16 m per frame; the segment test still catches a 0.4 m target.
    {
        Combat c;
        c.setTargets({{7, {10.0f, 5.0f, 0.0f}, 0.4f, true}});
        c.fireTracer({0.0f, 5.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, Combat::kTracerSpeed, 2);
        int hitTarget = -1;
        for (float t = 0.0f; t < 0.5f && hitTarget < 0; t += kDt) {
            c.update(kDt, t);
            for (const BlastEvent& b : c.takeBlasts()) hitTarget = b.directHit;
        }
        record("T102", "A tracer cannot tunnel through a small target (segment vs sphere per step)",
               "direct hit on target 7 (one 16.7 m step crosses the 0.4 m sphere)", fmt("direct hit id %d", hitTarget),
               hitTarget == 7);
    }
    // T103: an explosion makes every effect type, a light that dies out, and a scorch mark.
    {
        Combat c;
        const Vec3 p{3.0f, terrainHeight(3.0f, 3.0f) + 0.2f, 3.0f};
        c.explode(p, 1.0f, 0);
        c.update(kDt, 0.0f);
        std::set<int> types;
        for (const EffectParticle& e : c.particles()) types.insert(static_cast<int>(e.type));
        const bool lightNow = !c.lights().empty();
        const float shakeNear = c.shake(p + Vec3{2, 1, 0});
        for (float t = 0.0f; t < 1.5f; t += kDt) c.update(kDt, t);
        const bool lightLater = !c.lights().empty();
        const bool expected = types.count(0) && types.count(1) && types.count(2) && types.count(3) && types.count(4) &&
                              types.count(6) && types.count(7);
        record("T103", "Explosion: fire, smoke, sparks, flash, shock wave, dust, embers + decaying light + scorch",
               "types {0,1,2,3,4,6,7}, light at t=0 and gone by 1.5 s, 1 scorch, camera shake near the blast",
               fmt("%zu types, light %d -> %d, %zu scorch, shake %.2f", types.size(), lightNow, lightLater,
                   c.scorches().size(), shakeNear),
               expected && lightNow && !lightLater && c.scorches().size() == 1 && shakeNear > 0.1f);
    }
    // T104: particle cap.
    {
        Combat c;
        for (int i = 0; i < 60; ++i) c.explode({0.0f, 1.0f + 0.1f * i, 0.0f}, 1.5f, 0);
        c.update(kDt, 0.0f);
        record("T104", "Particle count never exceeds the cap (oldest smoke dropped first)",
               fmt("<= %zu after 60 large blasts", Combat::kParticleCap), fmt("%zu", c.particles().size()),
               c.particles().size() <= Combat::kParticleCap);
    }
}

TS_TEST_GROUP(Intruders) {
    // T105: armour: a blast inside the radius damages (hit event, alive), repeated hits destroy it after 4-5;
    // a blast three radii away does nothing.
    {
        Scene s;
        s.setMissionEnabled(false);
        run(s, 1.0f);
        const int b1 = s.spawnIntruder({-12.0f, 0.0f, -6.0f}, 0.0f, layout::kObjectives[1], 3u);
        const float r = Combat::kDamageRadiusPerPower;
        int hitsToKill = 0, hitEvents = 0;
        for (int k = 0; k < 8 && s.roverAlive(0); ++k) {
            const Vec3 a = s.intruders()[0]->state.position;
            s.combat().explode(a + Vec3{0.5f * r, 0.15f, 0.0f}, 1.0f, 0);
            if (k == 0) s.combat().explode(s.intruders()[b1]->state.position + Vec3{3.0f * r, 0.15f, 0.0f}, 1.0f, 0);
            for (int f = 0; f < 2; ++f) {
                s.update(kDt, Detection{});
                for (const GameEvent& e : s.events()) hitEvents += e.type == GameEventType::EnemyHit ? 1 : 0;
            }
            ++hitsToKill;
        }
        const bool bUntouched = s.roverAlive(b1) && s.intruders()[b1]->health == kRoverHealth;
        record("T105", "Armoured rover: blasts at 0.5 r damage it (hit events), it dies on the 4th-5th hit; 3 r: no damage",
               "4 or 5 hits to kill, hit events before the kill, far rover untouched, kills 1",
               fmt("hits to kill %d, hit events %d, far rover untouched %d (slot %d, health %.0f), kills %d", hitsToKill, hitEvents,
                   bUntouched, b1, b1 >= 0 ? s.intruders()[b1]->health : -1.0f, s.kills()),
               !s.roverAlive(0) && hitsToKill >= 4 && hitsToKill <= 5 && hitEvents >= 1 && bUntouched && s.kills() == 1);

        // T106: debris pieces fly, then come to rest on the ground.
        float maxHeight = 0.0f;
        for (float t = 0.0f; t < 8.0f; t += kDt) {
            s.update(kDt, Detection{});
            for (const DebrisPiece& p : s.intruders()[0]->debris)
                maxHeight = std::max(maxHeight, p.position.y - terrainHeight(p.position.x, p.position.z));
        }
        int resting = 0, above = 0;
        const auto& pieces = s.intruders()[0]->debris;
        for (const DebrisPiece& p : pieces) {
            resting += p.resting ? 1 : 0;
            above += p.position.y >= terrainHeight(p.position.x, p.position.z) ? 1 : 0;
        }
        record("T106", "Debris: rigid pieces thrown up, all at rest on (not under) the terrain within 8 s",
               "peak > 0.5 m, every piece resting and above the ground",
               fmt("%zu pieces, peak %.2f m, %d resting, %d above ground", pieces.size(), maxHeight, resting, above),
               !pieces.empty() && maxHeight > 0.5f && resting == static_cast<int>(pieces.size()) &&
                   above == static_cast<int>(pieces.size()));

        // T107: wreck burns, is cleared after 25 s and the slot is free again 3 s later (the mission reuses it).
        const bool wreckAt20 = s.intruders()[0]->life == Intruder::Life::Wreck;  // t = 8 s after the blast
        run(s, 18.0f);                                                          // 26 s after
        const bool goneAt26 = s.intruders()[0]->life == Intruder::Life::Gone;
        run(s, 3.0f);                                                           // 29 s after
        const auto life = s.intruders()[0]->life;
        const bool freed = life == Intruder::Life::Waiting || life == Intruder::Life::Alive;
        record("T107", "Wreck life cycle: burning wreck -> removed after 25 s -> slot free 3 s later (next wave reuses it)",
               "wreck at 8 s, gone at 26 s, slot waiting or re-spawned at 29 s",
               fmt("wreck=%d gone=%d freed=%d", wreckAt20, goneAt26, freed), wreckAt20 && goneAt26 && freed);
    }
    // T108: the UAV only requests a launch after holding the target for kLockTime.
    {
        DroneController dc;
        DroneState st;
        Detection d;
        d.found = true;
        d.u = 0.02f;
        d.v = -0.05f;
        const Mat4 cam = translate({0.0f, 6.0f, 8.0f}) * rotateX(radians(60.0f));  // looking down at the ground ahead
        float firstLaunch = -1.0f;
        for (float t = 0.0f; t < 4.0f; t += kDt) {
            dc.update(kDt, t, d, cam, st);
            if (st.launchRequest && firstLaunch < 0.0f) firstLaunch = t;
            st.launchRequest = false;
        }
        record("T108", "Launcher: no round before the target has been tracked for 1.5 s",
               fmt("first launch request at >= %.1f s after acquisition", kLockTime),
               fmt("first request at %.2f s (mode %s)", firstLaunch, droneModeName(st.mode)),
               firstLaunch >= kLockTime && firstLaunch < kLockTime + 0.5f);

        // T109: after a kill the hot wreck is not a new target.
        DroneController dc2;
        DroneState st2;
        Vec3 groundPoint;
        DroneController::imagePointToGround(d.u, d.v, cam, groundPoint);
        dc2.ignoreNear(groundPoint, kWreckIgnoreTime);
        for (float t = 0.0f; t < 3.0f; t += kDt) dc2.update(kDt, t, d, cam, st2);
        record("T109", "Target discrimination: a blob on a known wreck is ignored (no track, no launch)",
               "mode SEARCH, lock 0, no launch request",
               fmt("mode %s, lock %.2f s, request %d", droneModeName(st2.mode), st2.lockTime, st2.launchRequest),
               st2.mode == DroneMode::Search && st2.lockTime == 0.0f && !st2.launchRequest);

        // T110: friend-or-foe: a blob next to a friendly vehicle is never engaged.
        DroneController dc3;
        DroneState st3;
        dc3.setFriendly({groundPoint + Vec3{1.5f, 0.0f, 0.0f}});
        for (float t = 0.0f; t < 3.0f; t += kDt) dc3.update(kDt, t, d, cam, st3);
        record("T110", "Friend-or-foe: blobs within 4.5 m of a friendly vehicle are not targets",
               "mode SEARCH, no launch request", fmt("mode %s, request %d", droneModeName(st3.mode), st3.launchRequest),
               st3.mode == DroneMode::Search && !st3.launchRequest);

        // T111: keep-out box (bunker ramp): track, but never launch at a target inside it.
        DroneController dc4;
        DroneState st4;
        dc4.setKeepOut({groundPoint - Vec3{2, 2, 2}, groundPoint + Vec3{2, 2, 2}});
        bool any = false;
        for (float t = 0.0f; t < 4.0f; t += kDt) {
            dc4.update(kDt, t, d, cam, st4);
            any = any || st4.launchRequest;
        }
        record("T111", "Keep-out: no launch at a target on the bunker's own entrance ramp", "TRACK mode, no launch request",
               fmt("mode %s, any request %d", droneModeName(st4.mode), any), st4.mode == DroneMode::Track && !any);
    }
    // T112: rail rounds: four, each hidden at launch and back after the reload time.
    {
        Scene s;
        run(s, 2.0f);
        int launched = 0;
        while (s.launchRound()) ++launched;
        bool hidden = true;
        for (int i = 0; i < kUavMissiles; ++i) hidden = hidden && !s.uav().missiles[i]->visible;
        const int readyNow = s.roundsReady();
        run(s, kUavReloadTime + 0.2f);
        bool back = true;
        for (int i = 0; i < kUavMissiles; ++i) back = back && s.uav().missiles[i]->visible;
        record("T112", "UAV launcher: 4 rail rounds, hidden at launch, replaced after the " + fmt("%.0f", kUavReloadTime) + " s reload",
               "4 launched, 0 ready, all hidden; 4 ready and visible after " + fmt("%.0f", kUavReloadTime) + " s",
               fmt("launched %d, ready %d, hidden %d; after reload ready %d visible %d", launched, readyNow, hidden,
                   s.roundsReady(), back),
               launched == 4 && readyNow == 0 && hidden && s.roundsReady() == 4 && back);
    }
    // T113: the whole base runs for 120 s with every object idle: no NaN anywhere, all objects present.
    {
        Scene s;
        run(s, 120.0f);
        bool finite = finiteVec(s.drone.position);
        for (const SceneNode* n : s.leaves())
            for (float v : n->world.m) finite = finite && std::isfinite(v);
        for (const auto& c : s.controllables()) finite = finite && finiteVec(c->position());
        int possessable = 0;
        for (const auto& c : s.controllables()) possessable += c->possessable() ? 1 : 0;
        record("T113", "Full base simulated 120 s: finite state, >= 7 possessable objects (UAV + >= 6 new)",
               ">= 7 possessable, all world matrices finite",
               fmt("%zu controllables (%d possessable), %zu leaves, finite %d, kills %d, rounds launched %d",
                   s.controllables().size(), possessable, s.leaves().size(), finite, s.kills(), s.roundsLaunched()),
               finite && possessable >= 7);
    }
}
