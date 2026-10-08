// v6 random rover routes and day rules (T215-T224): via-point chains, route diversity, personalities, objective
// switches, the spawn ring, day gating of the waves, the dawn withdrawal, the day lights, the seeded UAV patrol
// and per-mark friendly radii. CPU only. The day rules are pinned (Scene::pinDayRules) so the tests do not
// depend on the solar model; T220 also checks a scene started at noon whenever the day cycle reports day there.
#include <cmath>
#include <set>

#include "TestKit.h"
#include "ai/Squad.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/DayCycle.h"
#include "scene/Mission.h"
#include "scene/Scene.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr float kDt = 1.0f / 30.0f;

float bearingDiff(float a, float b) {
    float d = a - b;
    while (d > PI) d -= 2.0f * PI;
    while (d < -PI) d += 2.0f * PI;
    return std::fabs(d);
}

float horiz(const Vec3& a, const Vec3& b) { return length(Vec3{a.x - b.x, 0.0f, a.z - b.z}); }

// A scene with the mission off and the initial rover removed (rovers are placed by hand).
void clearField(Scene& s) {
    s.setMissionEnabled(false);
    s.destroyIntruder(0, s.intruders()[0]->state.position + Vec3{0, 50, 0}, 0.0f);
}

DayRules dayRules() {
    DayRules r;
    r.wavesAllowed = false;
    r.beamsOn = false;
    r.practicalScale = 0.0f;
    r.daylight = 1.0f;
    return r;
}

}  // namespace

TS_TEST_GROUP(Paths) {
    Scene planner;  // its planner grid answers the free-cell questions
    const HideController& grid = planner.hideController();
    MissionConfig cfg;
    cfg.randomRoutes = true;
    cfg.legacyFirst = false;
    cfg.freeCell = [&grid](const Vec3& p) { return grid.freeAt(p); };

    // Many draws of the random-route stream (T215, T217-T219).
    Mission m(deriveSeed(42u, 1));
    m.configure(cfg);
    std::vector<SpawnRequest> draws;
    for (int i = 0; i < 2000; ++i) draws.push_back(m.nextSpawn());

    // T215: via chains: 0-3 via points, all in a +-70 deg sector round the spawn bearing, on free cells, ordered
    // outermost first; a rover on such a chain passes every via point and then closes in on its objective.
    {
        int counts[4] = {0, 0, 0, 0}, bad = 0;
        for (const SpawnRequest& r : draws) {
            counts[std::min<size_t>(3, r.via.size())]++;
            const float a = std::atan2(r.position.z, r.position.x);
            float lastR = 1e9f;
            for (const Vec3& v : r.via) {
                const float vr = std::sqrt(v.x * v.x + v.z * v.z);
                if (bearingDiff(std::atan2(v.z, v.x), a) > radians(kViaSectorDeg) + 1e-3f || !grid.freeAt(v) ||
                    vr < kViaMinR - 1e-3f || vr > kViaMaxR + 1e-3f || vr > lastR + 1e-4f)
                    ++bad;
                lastR = vr;
            }
        }
        // Drive one 2-via chain.
        const SpawnRequest* pick = nullptr;
        for (const SpawnRequest& r : draws)
            if (r.via.size() == 2 && r.switchAfter < 0.0f) { pick = &r; break; }
        int passed = -1;
        float closed = 0.0f;
        if (pick) {
            Scene s;
            clearField(s);
            SpawnRequest r = *pick;
            r.personality.pauseInCover = 0.0f;
            const int slot = s.spawnIntruder(r);
            const float d0 = horiz(s.intruders()[slot]->state.position, r.objective);
            float best = d0;
            for (float t = 0.0f; t < 120.0f && s.roverAlive(slot); t += kDt) {
                s.update(kDt, Detection{});
                if (!s.roverAlive(slot)) { best = 0.0f; break; }
                passed = s.intruders()[slot]->hide.viaReached();
                if (passed >= 2) best = std::min(best, horiz(s.intruders()[slot]->state.position, r.objective));
            }
            if (!s.roverAlive(slot)) passed = 2;  // breached: it went through both via points first
            closed = d0 - best;
        }
        record("T215", "Random routes: 0-3 via points in a +-70 deg sector (free cells, outermost first); a rover passes them",
               "every count 0..3 drawn, 0 bad via points, the 2-via rover passes both and closes in > 3 m",
               fmt("counts %d/%d/%d/%d, %d bad, via passed %d, closed in %.1f m", counts[0], counts[1], counts[2], counts[3],
                   bad, passed, closed),
               counts[0] > 0 && counts[1] > 0 && counts[2] > 0 && counts[3] > 0 && bad == 0 && passed == 2 && closed > 3.0f);
    }

    // T216: route diversity: the same spawn and objective with three different random routes gives three
    // clearly different tracks (mean pairwise separation along the way).
    {
        const Vec3 start{-12.0f, 0.0f, -10.0f}, goal = layout::kObjectives[2];
        std::vector<Vec3> track[3];
        for (int k = 0; k < 3; ++k) {
            Scene s;
            clearField(s);
            SpawnRequest r = draws[17 + 5 * k];
            r.position = start;
            r.yaw = 0.8f;
            r.objective = goal;
            r.switchAfter = -1.0f;
            r.personality.pauseInCover = 0.0f;
            const int slot = s.spawnIntruder(r);
            for (float t = 0.0f; t < 30.0f; t += kDt) {
                s.update(kDt, Detection{});
                if (std::fmod(t, 1.0f) < kDt) track[k].push_back(s.intruders()[slot]->state.position);
            }
        }
        float minMean = 1e9f;
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b) {
                float sum = 0.0f;
                const size_t n = std::min(track[a].size(), track[b].size());
                for (size_t i = 0; i < n; ++i) sum += horiz(track[a][i], track[b][i]);
                minMean = std::min(minMean, n ? sum / n : 0.0f);
            }
        record("T216", "Route diversity: one spawn / objective, three random routes -> three different tracks (30 s)",
               "smallest mean pairwise separation > 1.0 m", fmt("smallest mean separation %.2f m", minMean), minMean > 1.0f);
    }

    // T217: personalities: ranges, every noise lattice 3..7 drawn; a faster rover covers more ground.
    {
        float sMin = 9, sMax = 0, pMax = 0, nwMin = 99, nwMax = 0;
        std::set<int> lattices;
        for (const SpawnRequest& r : draws) {
            const RoverPersonality& p = r.personality;
            sMin = std::min(sMin, p.speedScale);
            sMax = std::max(sMax, p.speedScale);
            pMax = std::max(pMax, p.pauseInCover);
            nwMin = std::min(nwMin, p.noiseWeight);
            nwMax = std::max(nwMax, p.noiseWeight);
            lattices.insert(p.noiseCells);
        }
        float dist[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            HideController hc = grid;
            RoverPersonality p;
            p.speedScale = k == 0 ? 0.8f : 1.25f;
            RoverState r;
            r.position = {-14.0f, terrainHeight(-14.0f, 0.0f), 0.0f};
            r.yaw = PI * 0.5f;
            hc.setRoute({}, {14.0f, 0.0f, 0.0f}, 99u, p);
            const Vec3 p0 = r.position;
            for (float t = 0.0f; t < 10.0f; t += kDt) hc.update(kDt, std::vector<Vec3>{{0, 60, 60}}, r);
            dist[k] = horiz(r.position, p0);
        }
        const bool latticesOk = lattices == std::set<int>{3, 4, 5, 6, 7};
        record("T217", "Rover personalities: speed 0.8-1.25, pause-in-cover <= 0.5, noise lattice 3-7; speed shows",
               "ranges kept, all 5 lattices, the x1.25 rover drives > 1.3x as far as the x0.8 one in 10 s",
               fmt("speed %.2f..%.2f, pause max %.2f, noise w %.0f..%.0f, %zu lattices, 10 s: %.1f m vs %.1f m", sMin, sMax, pMax,
                   nwMin, nwMax, lattices.size(), dist[0], dist[1]),
               sMin >= 0.8f && sMax <= 1.25f && pMax <= 0.5f && latticesOk && dist[1] > 1.3f * dist[0] && dist[0] > 2.0f);
    }

    // T218: objective switch: p = 0.3 of the routes, to a different asset; in the scene the rover re-targets.
    {
        int switches = 0, same = 0;
        for (const SpawnRequest& r : draws) {
            if (r.switchAfter < 0.0f) continue;
            ++switches;
            same += horiz(r.altObjective, r.objective) < 0.1f ? 1 : 0;
        }
        const float frac = static_cast<float>(switches) / draws.size();
        Scene s;
        clearField(s);
        SpawnRequest r = draws[0];
        r.via.clear();
        r.objective = layout::kObjectives[0];
        r.altObjective = layout::kObjectives[4];
        r.switchAfter = 3.0f;
        const int slot = s.spawnIntruder(r);
        bool before = false, after = false;
        for (float t = 0.0f; t < 4.0f; t += kDt) {
            s.update(kDt, Detection{});
            if (t < 2.5f) before = horiz(s.intruders()[slot]->hide.objective(), layout::kObjectives[0]) < 0.01f;
        }
        after = horiz(s.intruders()[slot]->hide.objective(), layout::kObjectives[4]) < 0.01f &&
                horiz(s.intruders()[slot]->objective, layout::kObjectives[4]) < 0.01f;
        record("T218", "Objective switch: ~30 % of the random routes change to another camp asset part-way",
               "fraction 0.25-0.35, never the same asset, the rover re-targets at its switch time",
               fmt("fraction %.3f, %d same, objective before %d after %d", frac, same, before, after),
               frac > 0.25f && frac < 0.35f && same == 0 && before && after);
    }

    // T219: spawn ring: never within 18 deg of the gate road, radius 16.8 +- 1.2 m, uniform bearing (8 sectors);
    // without random routes the mission's draws are exactly the v5 ones.
    {
        int north = 0, radiusBad = 0, bins[8] = {};
        for (const SpawnRequest& r : draws) {
            const float fromNorth = std::fabs(std::atan2(r.position.x, r.position.z));
            north += fromNorth < radians(kNorthGapDeg) ? 1 : 0;
            const float rr = std::sqrt(r.position.x * r.position.x + r.position.z * r.position.z);
            radiusBad += std::fabs(rr - layout::kSpawnRadius) > kSpawnJitter + 1e-3f ? 1 : 0;
            const float a = std::atan2(r.position.x, r.position.z) + PI;  // 0..2pi from south
            bins[std::min(7, static_cast<int>(a / (PI / 4.0f)))]++;
        }
        // Bins 3 and 4 touch north (they lose 18 of their 45 deg); the others expect n * 45 / 324.
        float worst = 0.0f;
        for (int b = 0; b < 8; ++b) {
            const float expect = draws.size() * ((b == 3 || b == 4) ? 27.0f : 45.0f) / 324.0f;
            worst = std::max(worst, std::fabs(bins[b] - expect) / expect);
        }
        Mission a(0x5eed1234u), b(0x5eed1234u);
        b.configure(MissionConfig{});
        bool legacy = true;
        for (int i = 0; i < 50; ++i) {
            const SpawnRequest x = a.nextSpawn(), y = b.nextSpawn();
            legacy = legacy && x.position.x == y.position.x && x.position.z == y.position.z && x.seed == y.seed &&
                     !x.randomRoute && x.via.empty();
        }
        record("T219", "Spawn ring: uniform bearing outside the north gap, radius 16.8 +- 1.2 m; legacy draws unchanged",
               "0 in the north gap, 0 off the ring, every 45 deg sector within 15 % of uniform, v5 stream identical",
               fmt("%d north, %d off ring, worst sector deviation %.1f %%, legacy %d", north, radiusBad, 100.0f * worst, legacy),
               north == 0 && radiusBad == 0 && worst < 0.15f && legacy);
    }

    // T220: day gating: no waves and no spawns while the rules say day; the next night starts a wave.
    {
        Scene s;
        clearField(s);
        s.setMissionEnabled(true);
        s.pinDayRules(dayRules());
        int spawnsDay = 0, wavesDay = 0;
        for (float t = 0.0f; t < 40.0f; t += kDt) {
            s.update(kDt, Detection{});
            for (const GameEvent& e : s.events()) wavesDay += e.type == GameEventType::WaveStart ? 1 : 0;
            spawnsDay = std::max(spawnsDay, s.aliveIntruders());
        }
        s.unpinDayRules();
        int wavesNight = 0;
        for (float t = 0.0f; t < 5.0f; t += kDt) {
            s.update(kDt, Detection{});
            for (const GameEvent& e : s.events()) wavesNight += e.type == GameEventType::WaveStart ? 1 : 0;
        }
        const int aliveNight = s.aliveIntruders();
        // A scene that starts at noon (when the day cycle says waves are not allowed then): no rover at start.
        const bool noonIsDay = !DayCycle(12.0f, false).wavesAllowed();
        int aliveNoon = -1;
        if (noonIsDay) {
            SceneOptions o;
            o.startHour = 12.0f;
            o.garrison = false;
            Scene noon(o);
            aliveNoon = noon.aliveIntruders();
        }
        record("T220", "Day gating: rover waves only at night (pinned day: none in 40 s; night again: a wave starts)",
               "0 waves / rovers by day, wave + rover within 5 s of night, noon start has no rover (if the clock says day)",
               fmt("day: %d waves, %d rovers; night: %d waves, %d rovers; noon start %s %d", wavesDay, spawnsDay, wavesNight,
                   aliveNight, noonIsDay ? "rovers" : "(clock stub: n/a)", aliveNoon),
               wavesDay == 0 && spawnsDay == 0 && wavesNight >= 1 && aliveNight >= 1 && (!noonIsDay || aliveNoon == 0));
    }

    // T221: dawn withdrawal: surviving rovers leave through the fence ring (Withdraw, not a breach), integrity and
    // score unchanged, slots free again.
    {
        Scene s;
        clearField(s);
        const Vec3 starts[3] = {{-10.0f, 0, -8.0f}, {9.0f, 0, -9.0f}, {-12.0f, 0, 5.0f}};
        for (int k = 0; k < 3; ++k) s.spawnIntruder(starts[k], 0.0f, layout::kObjectives[k], 31u + k);
        for (float t = 0.0f; t < 3.0f; t += kDt) s.update(kDt, Detection{});
        const int aliveAtDawn = s.aliveIntruders();
        const float integrity = s.mission().state().integrity;
        const int score = s.mission().state().score;
        s.pinDayRules(dayRules());
        int withdraws = 0, breaches = 0;
        float maxR = 0.0f, lastT = 0.0f;
        for (float t = 0.0f; t < 100.0f && s.aliveIntruders() > 0; t += kDt) {
            s.update(kDt, Detection{});
            for (const GameEvent& e : s.events()) {
                if (e.type == GameEventType::Withdraw) {
                    ++withdraws;
                    maxR = std::max(maxR, std::sqrt(e.position.x * e.position.x + e.position.z * e.position.z));
                }
                breaches += e.type == GameEventType::Breach ? 1 : 0;
            }
            lastT = t;
        }
        const MissionState& m = s.mission().state();
        record("T221", "Dawn withdrawal: alive rovers retreat to the fence ring -> Withdraw events, not breaches",
               "3 alive at dawn, 3 Withdraw at r > 16 m, 0 breaches, integrity / score unchanged, all slots free",
               fmt("%d alive, %d withdrawn (max r %.1f m, last at %.0f s), %d breaches, integrity %.0f -> %.0f, score %d -> %d, "
                   "%d alive after",
                   aliveAtDawn, withdraws, maxR, lastT, breaches, integrity, m.integrity, score, m.score, s.aliveIntruders()),
               aliveAtDawn == 3 && withdraws == 3 && maxR > 16.0f && breaches == 0 && m.integrity == integrity &&
                   m.score == score && s.aliveIntruders() == 0 && m.withdrawn == 3);
    }

    // T222: day lights: no searchlights (L2 off, both cones hidden, no UAV 1 dynamic light), beacon off.
    {
        Scene s;
        s.setMissionEnabled(false);
        s.update(kDt, Detection{});
        auto beamsVisible = [&s] {
            int n = 0;
            for (const SceneNode* l : s.leaves())
                n += (l->name == "Light cone" && l->visible) ? 1 : 0;
            return n;
        };
        const bool nightSpot = s.lights.spot.enabled && s.lights.beacon.enabled;
        const int nightCones = beamsVisible();
        const size_t nightLights = s.combat().lights().size();
        s.pinDayRules(dayRules());
        s.update(kDt, Detection{});
        const bool daySpot = s.lights.spot.enabled, dayBeacon = s.lights.beacon.enabled;
        const int dayCones = beamsVisible();
        const size_t dayLights = s.combat().lights().size();
        s.unpinDayRules();
        s.update(kDt, Detection{});
        const bool back = s.lights.spot.enabled && beamsVisible() == 2;
        record("T222", "Day lights: searchlight L2, both beam cones and UAV 1's light off by day, beacon off",
               "night: spot + beacon on, 2 cones, 1 extra light; day: all off, 0 cones, 0 lights; night again: on",
               fmt("night spot/beacon %d, cones %d, lights %zu; day spot %d beacon %d cones %d lights %zu; back %d", nightSpot,
                   nightCones, nightLights, daySpot, dayBeacon, dayCones, dayLights, back),
               nightSpot && nightCones == 2 && nightLights == 1 && !daySpot && !dayBeacon && dayCones == 0 &&
                   dayLights == 0 && back);
    }

    // T223: seeded patrol: seed 0 flies the v5 orbit exactly; two seeds differ, stay inside the fence, and make
    // investigate detours.
    {
        // seed < 0: setPatrolSeed is never called (the v5 squad as constructed)
        auto fly = [](long seed, float seconds, float* maxR, int* detours) {
            Squad sq;
            if (seed >= 0) sq.setPatrolSeed(static_cast<uint32_t>(seed));
            const bool manual[2] = {false, false};
            const int ready[2] = {2, 2};
            std::vector<LaunchOrder> orders;
            std::vector<GameEvent> events;
            float t = 0.0f, r = 0.0f;
            for (; t < seconds; t += kDt) {
                sq.update(kDt, t, SquadFrame{}, manual, ready, orders, events);
                for (const DroneState& d : sq.drones) r = std::max(r, std::sqrt(d.position.x * d.position.x + d.position.z * d.position.z));
            }
            if (maxR) *maxR = r;
            if (detours) *detours = sq.detours();
            return std::vector<Vec3>{sq.drones[0].position, sq.drones[1].position};
        };
        const auto v5 = fly(-1, 60.0f, nullptr, nullptr), zero = fly(0, 60.0f, nullptr, nullptr);
        bool exact = true;
        for (int i = 0; i < 2; ++i)
            exact = exact && v5[i].x == zero[i].x && v5[i].y == zero[i].y && v5[i].z == zero[i].z;
        float r7 = 0, r8 = 0;
        int d7 = 0, d8 = 0;
        const auto a = fly(7, 200.0f, &r7, &d7), b = fly(8, 200.0f, &r8, &d8), a2 = fly(7, 200.0f, nullptr, nullptr);
        const float diff = horiz(a[0], b[0]) + horiz(a[1], b[1]);
        const bool repro = a[0].x == a2[0].x && a[1].z == a2[1].z;
        record("T223", "Seeded patrol: seed 0 = the v5 orbit bit for bit; seeds 7 / 8 fly different, reproducible orbits",
               "seed 0 identical, seeds differ > 2 m after 200 s, same seed identical, r < 19 m, >= 3 detours each",
               fmt("seed 0 %s, 7 vs 8 differ %.1f m, repro %d, max r %.1f / %.1f m, detours %d / %d", exact ? "identical" : "DIFFERS",
                   diff, repro, r7, r8, d7, d8),
               exact && diff > 2.0f && repro && r7 < 19.0f && r8 < 19.0f && d7 >= 3 && d8 >= 3);
    }

    // T224: friendly marks carry their own radius (soldier 1.2 m vs vehicle 4.5 m); the scene marks every
    // controllable with friendlyRadius() > 0 (not UAV 0 itself).
    {
        Squad sq;
        sq.setFriendly(std::vector<FriendlyMark>{{{5.0f, 0.0f, 5.0f}, 1.2f}, {{-8.0f, 0.0f, 0.0f}, 4.5f}});
        const bool soldierNear = sq.friendlyAt({5.9f, 0.0f, 5.0f}), soldierFar = sq.friendlyAt({7.0f, 0.0f, 5.0f});
        const bool vehicleNear = sq.friendlyAt({-8.0f, 0.0f, 4.0f}), vehicleFar = sq.friendlyAt({-8.0f, 0.0f, 5.0f});
        Scene s;
        s.setMissionEnabled(false);
        s.update(kDt, Detection{});
        size_t expected = 0;
        bool radiiOk = true;
        for (size_t i = 1; i < s.controllables().size(); ++i)
            if (s.controllables()[i]->friendlyRadius() > 0.0f) ++expected;
        const auto& marks = s.squad().friendlyMarks();
        size_t k = 0;
        for (size_t i = 1; i < s.controllables().size() && k < marks.size(); ++i) {
            const float r = s.controllables()[i]->friendlyRadius();
            if (r <= 0.0f) continue;
            radiiOk = radiiOk && marks[k].r == r && horiz(marks[k].p, s.controllables()[i]->position()) < 1e-4f;
            ++k;
        }
        record("T224", "Friendly marks: per-mark radius (soldier 1.2 m, vehicle 4.5 m), one per controllable with r > 0",
               "0.9 m from a soldier friendly, 2 m not; 4 m from a vehicle friendly, 5 m not; scene marks = controllables",
               fmt("soldier %d/%d, vehicle %d/%d, %zu marks for %zu controllables (radii match %d)", soldierNear, soldierFar,
                   vehicleNear, vehicleFar, marks.size(), expected, radiiOk),
               soldierNear && !soldierFar && vehicleNear && !vehicleFar && marks.size() == expected && radiiOk);
    }
}
