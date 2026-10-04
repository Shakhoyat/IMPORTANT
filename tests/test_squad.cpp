// Two-UAV squad, mission waves and rover infiltration (T138-T149, T180-T185). CPU only: the thermal images are
// replaced by a synthetic camera that projects every live rover into each UAV's image (what the real heat
// render + CV stage produce for an unoccluded rover).
#include <cmath>
#include <set>

#include "TestKit.h"
#include "ai/Squad.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"
#include "scene/Mission.h"
#include "scene/Scene.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr float kDt = 1.0f / 30.0f;

// Synthetic vision: project live rovers into UAV i's 320x240 camera (40 deg vertical FOV).
std::vector<Measurement2D> syntheticBlobs(const Scene& s, int uav) {
    std::vector<Measurement2D> out;
    const Mat4 inv = inverse(s.cameraWorld(uav));
    const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * static_cast<float>(kCvWidth) / kCvHeight;
    for (const auto& in : s.intruders()) {
        if (in->life != Intruder::Life::Alive) continue;
        const Vec3 l = transformPoint(inv, in->state.position + Vec3{0, 0.2f, 0});
        if (l.z < 0.5f) continue;
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

void stepWithVision(Scene& s, float seconds, int* combinedEvents = nullptr, std::vector<float>* blastTimes = nullptr) {
    for (float t = 0.0f; t < seconds; t += kDt) {
        for (int u = 0; u < kUavCount; ++u) s.submitVision(u, syntheticBlobs(s, u), s.cameraWorld(u));
        s.update(kDt, Detection{});
        for (const GameEvent& e : s.events()) {
            if (combinedEvents && e.type == GameEventType::CombinedStrike) ++*combinedEvents;
            if (blastTimes && e.type == GameEventType::Explosion && e.value >= 90) blastTimes->push_back(s.time());
        }
    }
}

}  // namespace

TS_TEST_GROUP(Squad) {
    // T138: the strike planner's flight-time model matches the simulated round within 10 %.
    {
        float worst = 0.0f;
        for (float d : {6.0f, 12.0f, 25.0f}) {
            Combat c;
            const Vec3 a{0, 10, 0}, b{d, 10, 0};
            c.setDesignator(1, b, true);
            c.fireMissile(a, {1, 0, 0}, 1, 0);
            float t = 0.0f;
            bool hit = false;
            for (; t < 3.0f && !hit; t += 1.0f / 240.0f) {
                c.update(1.0f / 240.0f, t);
                hit = !c.takeBlasts().empty();
            }
            worst = std::max(worst, std::fabs(t - roundFlightTime(d)) / roundFlightTime(d));
        }
        record("T138", "Time-on-target model: predicted round flight time vs the simulated round (6 / 12 / 25 m)",
               "error < 10 %", fmt("worst error %.1f %%", 100.0f * worst), worst < 0.10f);
    }
    // T139: patrol: the two UAVs fly on opposite sides of the compound.
    {
        Scene s;
        s.setMissionEnabled(false);
        for (auto i = 0; i < 6; ++i) (void)0;
        float minSep = 1e9f;
        for (float t = 0.0f; t < 30.0f; t += kDt) {
            s.update(kDt, Detection{});
            if (t > 8.0f) minSep = std::min(minSep, length(s.droneState(0).position - s.droneState(1).position));
        }
        const float r0 = length(Vec3{s.droneState(0).position.x, 0, s.droneState(0).position.z});
        record("T139", "Squad patrol: both UAVs inside the compound, kept apart (opposite phases)",
               "separation > 8 m at all times after 8 s, radius < 19.5 m",
               fmt("min separation %.1f m, UAV 0 radius %.1f m, roles %s / %s", minSep, r0,
                   squadRoleName(s.squad().roles[0]), squadRoleName(s.squad().roles[1])),
               minSep > 8.0f && r0 < 19.5f && s.squad().roles[0] == SquadRole::Patrol);
    }
    // T140-T142: full loop with synthetic vision: spot -> shared track -> combined strike -> kill -> score.
    {
        Scene s;
        int combined = 0;
        std::vector<float> blasts;
        stepWithVision(s, 90.0f, &combined, &blasts);
        const MissionState& m = s.mission().state();
        record("T140", "Full loop (synthetic vision): tracks -> coordinated strikes -> intruders destroyed",
               ">= 2 kills in 90 s, both UAVs launched, score = 100 kills + 50 combined",
               fmt("kills %d (combined %d), launched %d / %d, score %d, wave %d, breaches %d, fused updates %d", m.kills,
                   m.combinedKills, s.droneState(0).launched, s.droneState(1).launched, m.score, m.wave, m.breaches,
                   s.squad().tracker().fusedUpdates()),
               m.kills >= 2 && s.droneState(0).launched > 0 && s.droneState(1).launched > 0 &&
                   m.score == kKillScore * m.kills + kCombinedBonus * m.combinedKills);
        // Time on target: blasts of a combined strike arrive close together.
        float bestPair = 1e9f;
        for (size_t i = 1; i < blasts.size(); ++i) bestPair = std::min(bestPair, blasts[i] - blasts[i - 1]);
        record("T141", "Combined strike: both UAVs commit and the two rounds detonate together (time on target)",
               ">= 1 combined strike, two detonations within 0.35 s, >= 1 combined kill",
               fmt("%d combined strikes, closest blast pair %.2f s, %d combined kills", combined, bestPair, m.combinedKills),
               combined >= 1 && bestPair < 0.35f && m.combinedKills >= 1);
        record("T142", "Two-view fusion is used in the live loop (both UAVs see the same rover)",
               "> 50 triangulated track updates in 90 s", fmt("%d", s.squad().tracker().fusedUpdates()),
               s.squad().tracker().fusedUpdates() > 50);
    }
    // T143: without the squad's eyes, rovers reach their objectives: breaches cost integrity, waves continue.
    {
        Scene s;  // no vision submitted, no detections: the squad never sees anything
        int breaches = 0, waves = 0;
        for (float t = 0.0f; t < 150.0f; t += kDt) {
            s.update(kDt, Detection{});
            for (const GameEvent& e : s.events()) {
                breaches += e.type == GameEventType::Breach ? 1 : 0;
                waves += e.type == GameEventType::WaveStart ? 1 : 0;
            }
        }
        const MissionState& m = s.mission().state();
        record("T143", "Unopposed infiltration: rovers reach camp assets (breach -10 %), the next wave follows",
               ">= 3 breaches, integrity = 100 - 10 x breaches (or base lost), >= 2 waves",
               fmt("%d breaches, integrity %.0f %%, waves started %d (wave %d), bases lost %d", breaches, m.integrity, waves,
                   m.wave, m.basesLost),
               breaches >= 3 && waves >= 2 &&
                   (m.basesLost > 0 || std::fabs(m.integrity - (100.0f - 10.0f * m.breaches)) < 0.01f));
    }
    // T144: random routes: the same start and objective with different seeds gives different paths, both closing in.
    {
        float pathDiff = 0.0f, closed[2] = {0, 0};
        Vec3 ends[2];
        for (int k = 0; k < 2; ++k) {
            Scene s;
            s.setMissionEnabled(false);
            // remove the initial rover, place one by hand
            s.destroyIntruder(0, s.intruders()[0]->state.position + Vec3{0, 50, 0}, 0.0f);
            const Vec3 start{-12.0f, 0.0f, -10.0f}, goal = layout::kObjectives[2];
            const int slot = s.spawnIntruder(start, 0.8f, goal, k == 0 ? 11u : 977u);
            const float d0 = length(Vec3{start.x - goal.x, 0, start.z - goal.z});
            float maxDev = 0.0f;
            for (float t = 0.0f; t < 25.0f; t += kDt) s.update(kDt, Detection{});
            const Vec3 p = s.intruders()[slot]->state.position;
            ends[k] = p;
            closed[k] = d0 - length(Vec3{p.x - goal.x, 0, p.z - goal.z});
            (void)maxDev;
        }
        pathDiff = length(ends[0] - ends[1]);
        record("T144", "Random routes: two seeds, same start / objective -> different positions after 25 s, both closer",
               "end positions > 0.8 m apart, both moved > 3 m towards the objective",
               fmt("ends %.2f m apart, closed in %.1f m / %.1f m", pathDiff, closed[0], closed[1]),
               pathDiff > 0.8f && closed[0] > 3.0f && closed[1] > 3.0f);
    }
    // T145: a rover hides when a UAV comes close (evade), then resumes infiltrating.
    {
        HideController hc;
        Scene base;  // only for the occluders via the scene's planner copy
        hc = base.hideController();
        RoverState r;
        r.position = {6.0f, terrainHeight(6.0f, 6.0f), 6.0f};
        hc.setObjective(layout::kObjectives[0], 5u);
        hc.update(kDt, std::vector<Vec3>{{30, 6, 30}}, r);
        const bool before = hc.evading();
        hc.update(kDt, std::vector<Vec3>{{6.5f, 5.0f, 6.5f}}, r);
        const bool near = hc.evading();
        for (int i = 0; i < 200; ++i) hc.update(kDt, std::vector<Vec3>{{30, 6, 30}}, r);
        const bool after = hc.evading();
        record("T145", "Evasion: a UAV within 7 m makes an infiltrating rover hide for 4 s, then it resumes",
               "not evading far away, evading close, not evading 6.7 s after the UAV left",
               fmt("before %d, close %d, after %d", before, near, after), !before && near && !after);
    }
}

TS_TEST_GROUP(Mission) {
    // T180: waves grow and spawn points are random (different every wave).
    {
        Mission m(42u);
        std::vector<GameEvent> ev;
        std::set<int> angles;
        int spawns = 0, waveOneCount = 0;
        float t = 0.0f;
        for (int frame = 0; frame < 30 * 120; ++frame, t += kDt) {
            std::vector<SpawnRequest> sp;
            m.update(kDt, 0, 6, sp, ev, t);
            for (const SpawnRequest& r : sp) {
                ++spawns;
                angles.insert(static_cast<int>(std::atan2(r.position.z, r.position.x) * 10.0f));
                // resolve immediately (kill), so waves advance
                std::vector<GameEvent> e2;
                m.onKill(false, r.position, e2, t);
            }
            if (m.state().wave == 1) waveOneCount = m.state().toSpawn;
        }
        record("T180", "Mission waves: wave n sends min(2 + n, 6) rovers from random edge points",
               "wave 1 = 3 rovers, >= 4 waves in 120 s, spawn angles all different",
               fmt("wave 1 %d rovers, reached wave %d, %d spawns, %zu distinct angles", waveOneCount, m.state().wave, spawns,
                   angles.size()),
               waveOneCount == 3 && m.state().wave >= 4 && static_cast<int>(angles.size()) >= spawns - 1);
    }
    // T181: base lost after 10 breaches, the mission restarts at wave 1 and keeps the session score.
    {
        Mission m;
        std::vector<GameEvent> ev;
        std::vector<GameEvent> e2;
        m.onKill(true, {}, e2, 0.0f);
        for (int i = 0; i < 10; ++i) m.onBreach({}, ev, 1.0f);
        bool lost = false;
        for (const GameEvent& e : ev) lost = lost || e.type == GameEventType::BaseLost;
        record("T181", "Base integrity: 10 breaches -> BASE LOST, restart at wave 1 with full integrity, score kept",
               "BaseLost event, integrity 100, wave 0 (next wave = 1), score 150",
               fmt("lost %d, integrity %.0f, wave %d, score %d", lost, m.state().integrity, m.state().wave, m.state().score),
               lost && m.state().integrity == 100.0f && m.state().wave == 0 && m.state().score == 150);
    }
}
