// v6 soldiers (WP-B): splines, figure hierarchy, gait / IK, site planner routes, roster, bunker leg, possession,
// heat, work loops, duck reaction, cameras, separation. Test IDs T200-T214.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "TestKit.h"
#include "math/Spline.h"
#include "objects/BaseLayout.h"
#include "objects/Soldier.h"
#include "scene/Control.h"
#include "scene/Gait.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"
#include "ai/Occlusion.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr float kDt = 1.0f / 30.0f;

// A v5 scene without its own garrison: static solids, bunker walk space and the vehicles to plan around.
ts::Scene& baseScene() {
    static ts::Scene* scene = [] {
        ts::SceneOptions o;
        o.garrison = false;
        return new ts::Scene(o);
    }();
    return *scene;
}

struct World {
    ts::Garrison g;
    std::vector<std::unique_ptr<ts::Controllable>> owned;
    float t = 0.0f, hour = 12.0f;
    explicit World(float startHour, uint32_t seed = 0) : hour(startHour) {
        owned = g.build(seed);
        ts::Scene& s = baseScene();
        ts::GarrisonContext c;
        c.solids = &s.staticSolids();
        c.walk = &s.walkSpace();
        c.seed = seed;
        c.startHour = startHour;
        c.controllables = &s.controllables();
        c.leaves = &s.leaves();
        g.init(c);
    }
    ts::WorldContext ctx() const {
        ts::WorldContext w;
        w.time = t; w.dt = kDt; w.walk = &baseScene().walkSpace(); w.hour = hour;
        w.daylight = ts::dayShift(hour) ? 1.0f : 0.0f;
        return w;
    }
    void step(int possessed = -1, const ts::ControlInput* in = nullptr) {
        t += kDt;
        const ts::WorldContext w = ctx();
        for (int i = 0; i < g.count(); ++i) g.unit(i)->update(w, i == possessed ? in : nullptr);
    }
};

void dumpGrid(const ts::Garrison& g) {
    const ts::SitePlanner& p = g.planner();
    std::vector<std::string> rows(ts::SitePlanner::kCells, std::string(ts::SitePlanner::kCells, ' '));
    // connected components (8-neighbours without corner cutting, as the A*): '.' = largest, letters = others
    const int N = ts::SitePlanner::kCells;
    std::vector<int> comp(N * N, -1);
    std::vector<int> sizes;
    for (int s0 = 0; s0 < N * N; ++s0) {
        if (comp[s0] >= 0 || !p.freeCell(s0 % N, s0 / N)) continue;
        const int id = static_cast<int>(sizes.size());
        sizes.push_back(0);
        std::vector<int> stack{s0};
        comp[s0] = id;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back(); sizes[id]++;
            const int ci = c % N, cj = c / N;
            for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) {
                const int ni = ci + di, nj = cj + dj;
                if ((!di && !dj) || !p.freeCell(ni, nj)) continue;
                if (di && dj && (!p.freeCell(ci + di, cj) || !p.freeCell(ci, cj + dj))) continue;
                if (comp[nj * N + ni] < 0) { comp[nj * N + ni] = id; stack.push_back(nj * N + ni); }
            }
        }
    }
    int big = 0;
    for (size_t k = 0; k < sizes.size(); ++k) if (sizes[k] > sizes[big]) big = static_cast<int>(k);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const int c = comp[j * N + i];
            rows[j][i] = c < 0 ? '#' : c == big ? '.' : static_cast<char>('a' + c % 26);
        }
    if (!std::getenv("TS_NOROUTES")) for (const auto& r : g.routes())
        for (float s = 0; s < r.length(); s += 0.5f) {
            const ts::Vec3 q = r.pointAtDistance(s);
            const int i = ts::SitePlanner::cellOf(q.x), j = ts::SitePlanner::cellOf(q.z);
            if (i >= 0 && j >= 0 && i < 72 && j < 72) rows[j][i] = q.y < -1.0f ? 'u' : '*';
        }
    for (int k = 0; k < g.count(); ++k) {
        const ts::Vec3 q = g.unit(k)->position();
        const int i = ts::SitePlanner::cellOf(q.x), j = ts::SitePlanner::cellOf(q.z);
        if (i >= 0 && j >= 0 && i < 72 && j < 72) rows[j][i] = static_cast<char>('1' + k);
    }
    std::printf("     x -36 .. +36 (left to right), z +36 at the top\n");
    for (int j = ts::SitePlanner::kCells - 1; j >= 0; --j) std::printf("%4d %s\n", j - 36, rows[j].c_str());
}

}  // namespace

namespace ts {
namespace {

Mat4 worldOf(const SceneNode* n) {
    Mat4 m = n->local.matrix();
    for (const SceneNode* p = n->parent; p; p = p->parent) m = p->local.matrix() * m;
    return m;
}
Vec3 originOf(const SceneNode* n) { return transformPoint(worldOf(n), {0, 0, 0}); }

int countLeaves(const SceneNode& n, int depth, int& maxDepth, std::vector<const SceneNode*>& leaves) {
    int c = n.isLeaf() ? 1 : 0;
    if (n.isLeaf()) { maxDepth = std::max(maxDepth, depth); leaves.push_back(&n); }
    for (const auto& ch : n.children) c += countLeaves(*ch, depth + 1, maxDepth, leaves);
    return c;
}

int findSoldier(::World& w, Duty d) {
    for (int i = 0; i < w.g.count(); ++i)
        if (w.g.unit(i)->duty() == d) return i;
    return -1;
}
}  // namespace
}  // namespace ts

TS_TEST_GROUP(Soldiers) {
    using namespace ts;
    if (std::getenv("TS_GARRISON_SWEEP")) {   // robustness: dusk and dawn shift changes over several seeds
        for (uint32_t seed : {0u, 1u, 7u, 42u, 99u, 1234u})
            for (float h0 : {17.3f, 6.1f}) {
                ::World w(h0, seed);
                float minDist = 99.0f, stillT[8] = {};
                int overlaps = 0, stuck = 0, maxWaitIdx = -1;
                for (int f = 0; f < static_cast<int>(300.0f / kDt); ++f) {
                    w.hour = h0 + 0.6f * w.t / 300.0f;
                    w.step();
                    for (int i = 0; i < 8; ++i) {
                        const Vec3 a = w.g.unit(i)->position();
                        for (int j = i + 1; j < 8; ++j) {
                            const Vec3 b = w.g.unit(j)->position();
                            if (std::fabs(a.y - b.y) > 1.0f) continue;
                            const float d = std::hypot(a.x - b.x, a.z - b.z);
                            minDist = std::fmin(minDist, d);
                            if (d < 0.5f) {
                                if (overlaps % 20 == 0)
                                    std::printf("   overlap t %.1f %d/%d d %.2f (%.1f %.1f) %s | %s\n", w.t, i, j, d, a.x, a.z,
                                                w.g.unit(i)->status().c_str(), w.g.unit(j)->status().c_str());
                                ++overlaps;
                            }
                        }
                        stillT[i] = w.g.unit(i)->activity() == Activity::Wait ? stillT[i] + kDt : 0.0f;
                        if (stillT[i] > 20.0f) {
                            if (stuck < 3) {
                                std::printf("   stuck t %.1f %d (%.2f %.2f %.2f) %s [%s]\n", w.t, i, a.x, a.y, a.z, w.g.unit(i)->status().c_str(), w.g.unit(i)->debugState().c_str());
                                for (int j = 0; j < 8; ++j)
                                    if (j != i) {
                                        const Vec3 b = w.g.unit(j)->position();
                                        if (std::hypot(a.x - b.x, a.z - b.z) < 3.0f)
                                            std::printf("      near %d (%.2f %.2f %.2f) %s [%s]\n", j, b.x, b.y, b.z, w.g.unit(j)->status().c_str(), w.g.unit(j)->debugState().c_str());
                                    }
                            }
                            ++stuck; stillT[i] = 0.0f; maxWaitIdx = i;
                        }
                    }
                }
                int settled = 0;
                for (int i = 0; i < 8; ++i) settled += w.g.unit(i)->onDayShift() == dayShift(w.hour) ? 1 : 0;
                std::printf("SWEEP seed %u start %.1f: min %.2f overlaps %d stuck %d (%d) roster ok %d\n", seed, h0, minDist, overlaps, stuck, maxWaitIdx, settled);
                for (int i = 0; i < 8; ++i)
                    if (w.g.unit(i)->travelling() || w.g.unit(i)->activity() == Activity::Wait)
                        std::printf("   still moving %d (%.1f %.1f %.1f) %s\n", i, w.g.unit(i)->position().x, w.g.unit(i)->position().y, w.g.unit(i)->position().z, w.g.unit(i)->status().c_str());
            }
    }
    if (std::getenv("TS_GARRISON_DUMP")) {
        World day(12.0f), night(23.0f);
        dumpGrid(day.g);
        dumpGrid(night.g);
        for (int i = 0; i < 8; ++i) std::printf("%s | %s\n", day.g.roster(i, true).c_str(), day.g.roster(i, false).c_str());
        for (float l : day.g.routeLengths()) std::printf("route %.1f m\n", l);
    }

    // ---- T200: centripetal Catmull-Rom: interpolation, G1 joints, arc-length table, curvature ---------------------
    {
        std::vector<Vec3> ring;
        const float R = 8.0f;
        for (int k = 0; k < 12; ++k) ring.push_back({R * std::cos(k * PI / 6.0f), 0.0f, R * std::sin(k * PI / 6.0f)});
        CatmullRomPath circle(ring, true);
        std::vector<Vec3> zig = {{0, 0, 0}, {4, 0, 1}, {5, 0, 6}, {11, 0, 6.5f}, {12, 0, 0.5f}, {20, 0, 3}};
        CatmullRomPath open(zig, false);
        float interp = 0.0f, joint = 0.0f;
        for (int k = 0; k < static_cast<int>(zig.size()); ++k) interp = std::fmax(interp, length(open.point(static_cast<float>(k)) - zig[k]));
        for (int k = 1; k + 1 < static_cast<int>(zig.size()); ++k)
            joint = std::fmax(joint, degrees(std::acos(clampf(dot(open.tangent(k - 1e-4f), open.tangent(k + 1e-4f)), -1.0f, 1.0f))));
        double dense = 0.0;
        Vec3 prev = open.point(0.0f);
        for (int k = 1; k <= 20000; ++k) {
            const Vec3 p = open.point(open.segments() * k / 20000.0f);
            dense += length(p - prev);
            prev = p;
        }
        const float lenErr = std::fabs(open.length() - static_cast<float>(dense)) / static_cast<float>(dense);
        float spacing = 0.0f;
        for (float s = 0.0f; s + 0.5f < open.length(); s += 0.5f)
            spacing = std::fmax(spacing, std::fabs(length(open.pointAtDistance(s + 0.5f) - open.pointAtDistance(s)) - 0.5f));
        // analytic kappa against the circumradius of three nearby curve points; mean kappa over the closed ring = 2 pi / L
        float kErr = 0.0f;
        double kSum = 0.0;
        int kN = 0;
        for (const CatmullRomPath* c : {&circle, &open})
            for (float s = 0.3f; s + 0.3f < c->length(); s += 0.37f) {
                const Vec3 a = c->pointAtDistance(s - 0.05f), b = c->pointAtDistance(s), d = c->pointAtDistance(s + 0.05f);
                const float area2 = length(cross(b - a, d - a));
                const float kNum = 2.0f * area2 / (length(b - a) * length(d - b) * length(d - a));
                const float kAna = c->curvatureAtDistance(s);
                kErr = std::fmax(kErr, std::fabs(kAna - kNum) / std::fmax(kNum, 0.05f));
                if (c == &circle) { kSum += kAna; ++kN; }
            }
        kErr = std::fmax(kErr, std::fabs(static_cast<float>(kSum / kN) * circle.length() / (2.0f * PI) - 1.0f));
        const float circ = std::fabs(circle.length() / (2.0f * PI * R) - 1.0f);
        record("T200", "Centripetal Catmull-Rom: passes the control points, G1 joints, arc-length table, curvature",
               "interp < 1e-4 m, joint kink < 0.1 deg, length err < 0.2 %, 0.5 m steps +-1 cm, kappa vs 3-point estimate (and mean on a ring) within 3 %",
               fmt("interp %.1e m, kink %.3f deg, length err %.3f %%, step err %.3f m, kappa err %.1f %%, circumference err %.2f %%",
                   interp, joint, 100 * lenErr, spacing, 100 * kErr, 100 * circ),
               interp < 1e-4f && joint < 0.1f && lenErr < 0.002f && spacing < 0.01f && kErr < 0.03f && circ < 0.01f);
    }

    // ---- T201: figure hierarchy: primitives only, 34..75 leaves (v7 hero pass, was ~36), depth >= 6, capsules, unique ids, safe names -------
    {
        ::World w(12.0f);
        int minLeaves = 999, maxLeaves = 0, minDepth = 99, bad = 0, capsuleBad = 0, nameBad = 0, rifleBad = 0;
        std::vector<std::string> ids;
        for (int i = 0; i < w.g.count(); ++i) {
            const Soldier* s = w.g.unit(i);
            int depth = 0;
            std::vector<const SceneNode*> leaves;
            const int n = countLeaves(*s->rig().root, 0, depth, leaves);
            minLeaves = std::min(minLeaves, n); maxLeaves = std::max(maxLeaves, n); minDepth = std::min(minDepth, depth);
            int rifles = 0;
            for (const SceneNode* l : leaves) {
                if (!isLibraryPartPrimitive(l->primitive)) ++bad;   // v10 M3: library primitives (tapered-box torso, hemisphere helmet)
                ids.push_back(l->id);
                if (l->name == "Rifle receiver" && l->visible) ++rifles;
            }
            if (rifles != 1) ++rifleBad;   // held (guards) or slung (workers), never both
            for (int leg = 0; leg < 2; ++leg) {   // thigh cylinder radius == hip joint sphere radius
                const SceneNode* hipNode = s->rig().hip[leg];
                float cyl = -1, sph = -2;
                for (const auto& c : hipNode->children) {
                    if (c->name == "Thigh") cyl = c->local.scale.x;
                    if (c->name == "Hip joint") sph = c->local.scale.x;
                }
                if (std::fabs(cyl - sph) > 1e-6f) ++capsuleBad;
            }
            const char* nm = s->name();
            if (slotOfName(nm) >= 0 || nameHas(nm, "GATE") || nameHas(nm, "DOOR") || nameHas(nm, "BARRIER")) ++nameBad;
        }
        std::sort(ids.begin(), ids.end());
        const bool unique = std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        record("T201", "Soldier figure: primitives only, 34..75 leaves (v7 hero detail), depth >= 6, capsule limbs, unique ids, no slot keywords",
               "34..75 leaves, depth >= 6, 0 other primitives, capsule radii equal, ids unique, names free of slot keywords, 1 rifle drawn",
               fmt("leaves %d..%d, depth %d, other prims %d, capsule mismatches %d, ids unique %d, bad names %d (e.g. '%s'), rifle errors %d",
                   minLeaves, maxLeaves, minDepth, bad, capsuleBad, unique ? 1 : 0, nameBad, w.g.unit(0)->name(), rifleBad),
               minLeaves >= 34 && maxLeaves <= 75 && minDepth >= 6 && bad == 0 && capsuleBad == 0 && unique && nameBad == 0 && rifleBad == 0);
    }

    // ---- T202: foot plant: stance ankles stay locked in the world while the soldier walks ---------------------------
    {
        ::World w(12.0f);
        Soldier* s = w.g.unit(0);
        s->place(20.0f, -8.0f, 0.0f);
        ControlInput in;
        in.forward = 1.0f;
        float drift = 0.0f, maxDrift = 0.0f;
        int plants = 0;
        bool was[2] = {false, false};
        Vec3 first[2];
        int startF[2] = {0, 0};
        for (int f = 0; f < 150; ++f) {
            w.step(0, &in);
            for (int leg = 0; leg < 2; ++leg) {
                const Vec3 a = originOf(s->rig().ankle[leg]);
                if (s->planted(leg)) {
                    if (!was[leg]) { first[leg] = a; startF[leg] = f; ++plants; }
                    drift = std::hypot(a.x - first[leg].x, a.z - first[leg].z);
                    if (std::getenv("TS_GARRISON_DEBUG") && leg == 0)
                        std::printf("  f %d leg %d ankle (%.3f %.3f %.3f) lock (%.3f %.3f) drift %.3f phase %.3f speed %.2f\n", f, leg, a.x, a.y, a.z,
                                    s->plantPoint(leg).x, s->plantPoint(leg).z, drift, s->gaitPhase(), s->speed());
                    if (startF[leg] > 15) maxDrift = std::fmax(maxDrift, drift);   // stances after the 0.3 s gait blend-in
                }
                was[leg] = s->planted(leg);
            }
        }
        const float moved = std::hypot(s->position().x - 20.0f, s->position().z + 8.0f);
        record("T202", "Gait foot plant: the stance ankle stays fixed in the world (two-link IK on the locked point)",
               "walked > 5 m, >= 6 plants, horizontal drift < 1.5 cm in every stance after the 0.3 s start blend",
               fmt("walked %.1f m, %d plants, max drift %.2f cm", moved, plants, 100 * maxDrift),
               moved > 5.0f && plants >= 6 && maxDrift < 0.015f);
    }

    // ---- T203: two-link leg IK ---------------------------------------------------------------------------------------
    {
        const GaitParams g;
        float err = 0.0f;
        int reached = 0, total = 0;
        for (float x = -0.6f; x <= 0.6f; x += 0.05f)
            for (float y = -0.88f; y <= -0.3f; y += 0.04f) {
                float h, k, fx, fy;
                const bool ok = legIK(x, y, g.thigh, g.shin, h, k);
                ++total;
                if (!ok) continue;
                ++reached;
                legFK(h, k, g.thigh, g.shin, fx, fy);
                err = std::fmax(err, std::hypot(fx - x, fy - y));
            }
        float h, k;
        const bool far = legIK(0.0f, -1.5f, g.thigh, g.shin, h, k);
        int cycleMiss = 0;
        for (const GaitParams& gp : {GaitParams::walk(), GaitParams::run()})
            for (int i = 0; i < 200; ++i)
                for (int leg = 0; leg < 2; ++leg) {
                    float x, y;
                    footInPelvis(gp, i / 200.0f, leg, x, y);
                    if (!legIK(x, y, gp.thigh, gp.shin, h, k) || k < 0.0f) ++cycleMiss;
                }
        record("T203", "Two-link leg IK: FK(IK(p)) = p for reachable ankle targets, clamps beyond reach, gait targets reachable",
               "FK error < 1e-4 m, out-of-reach -> false (straight leg), every walk/run cycle target reachable with knee >= 0",
               fmt("%d/%d reachable, FK error %.1e m, far target -> %s knee %.1f deg, cycle misses %d", reached, total, err,
                   far ? "true" : "false", degrees(k), cycleMiss),
               err < 1e-4f && !far && cycleMiss == 0);
    }

    // ---- T204: gait coordination: legs in anti-phase, arms against the legs, two pelvis bobs per cycle ----------------
    {
        const GaitParams g = GaitParams::walk();
        double hh = 0, h0 = 0, h1 = 0, s0 = 0, hs = 0, ss = 0;
        int bobPeaks = 0;
        float prevY = 0, prevD = 0;
        const int n = 400;
        std::vector<Pose> poses;
        for (int i = 0; i < n; ++i) poses.push_back(gaitPose(g, static_cast<float>(i) / n));
        double m0 = 0, m1 = 0, ms = 0;
        for (const Pose& p : poses) { m0 += p.hip[0]; m1 += p.hip[1]; ms += p.shoulder[0]; }
        m0 /= n; m1 /= n; ms /= n;
        for (int i = 0; i < n; ++i) {
            const Pose& p = poses[i];
            hh += (p.hip[0] - m0) * (p.hip[1] - m1); h0 += (p.hip[0] - m0) * (p.hip[0] - m0); h1 += (p.hip[1] - m1) * (p.hip[1] - m1);
            hs += (p.hip[0] - m0) * (p.shoulder[0] - ms); s0 += (p.shoulder[0] - ms) * (p.shoulder[0] - ms);
            const float d = p.pelvisY - prevY;
            if (i > 1 && prevD > 0 && d <= 0) ++bobPeaks;
            prevD = d; prevY = p.pelvisY;
        }
        ss = hh / std::sqrt(h0 * h1);
        const double armLeg = hs / std::sqrt(h0 * s0);
        record("T204", "Gait coordination: legs in anti-phase, each arm swings against its leg, pelvis bobs twice per cycle",
               "corr(hip L, hip R) < -0.8, corr(hip L, shoulder L) < -0.8, 2 bob peaks",
               fmt("corr legs %.2f, corr leg/arm %.2f, bob peaks %d", ss, armLeg, bobPeaks),
               ss < -0.8 && armLeg < -0.8 && bobPeaks == 2);
    }

    // ---- T205: route clearance: every planned duty route keeps the body clear of every part ---------------------------
    {
        ::World w(12.0f);
        const SitePlanner& pl = w.g.planner();
        float worst = 9.0f, worstStop = 9.0f, maxR = 0.0f;
        int samples = 0;
        for (const CatmullRomPath& r : w.g.routes()) {
            worst = std::fmin(worst, pl.minClearance(r));
            if (std::getenv("TS_GARRISON_DEBUG"))
                for (float s = 0.0f; s <= r.length(); s += 0.1f) {
                    const Vec3 q = r.pointAtDistance(s);
                    if (q.y > -0.5f && !(std::fabs(q.x) < 1.95f && q.z > 3.5f && q.z < 13.6f) && !(std::fabs(q.x) < 1.2f && q.z < 14.4f && q.z > 3.5f) && pl.clearance(q.x, q.z) < 0.28f)
                        std::printf("  route len %.1f s %.2f at (%.2f %.2f %.2f) clearance %.2f\n", r.length(), s, q.x, q.y, q.z, pl.clearance(q.x, q.z));
                }
            for (float s = 0.0f; s <= r.length(); s += 0.25f, ++samples) {
                const Vec3 q = r.pointAtDistance(s);
                maxR = std::fmax(maxR, std::hypot(q.x, q.z));
            }
        }
        // stops: the soldiers start on their posts (day + night)
        ::World n(23.0f);
        for (::World* x : {&w, &n})
            for (int i = 0; i < x->g.count(); ++i) {
                const Vec3 p = x->g.unit(i)->position();
                if (p.y > -1.0f && x->g.unit(i)->activity() != Activity::Walk && x->g.unit(i)->activity() != Activity::Run)
                    worstStop = std::fmin(worstStop, pl.clearance(p.x, p.z));
            }
        const int routes = static_cast<int>(w.g.routes().size());
        record("T205", "Site planner routes (A* -> string pulling -> Catmull-Rom) keep clear of every obstacle footprint",
               "every route sample >= 0.28 m from all parts, posts >= 0.45 m, inside the HESCO wall, >= 14 routes",
               fmt("%d routes, %d samples, min clearance %.2f m, posts %.2f m, max radius %.1f m, %d free cells",
                   routes, samples, worst, worstStop, maxR, pl.freeCount()),
               worst >= SitePlanner::kBodyClearance - 1e-3f && worstStop >= 0.45f && maxR < layout::kPerimeterRadius - 1.0f && routes >= 14);
    }

    // ---- T206: duty roster: day all outdoors, night 3 off duty + technician in the bunker, 4 guards on posts -------
    {
        ::World d(12.0f), n(23.0f);
        int dayUnder = 0, nightUnder = 0, offDuty = 0, nightGuards = 0, dayGuards = 0;
        for (int i = 0; i < 8; ++i) {
            dayUnder += d.g.unit(i)->underground() ? 1 : 0;
            nightUnder += n.g.unit(i)->underground() ? 1 : 0;
            offDuty += n.g.roster(i, false).rfind("off duty", 0) == 0 ? 1 : 0;
            offDuty += d.g.roster(i, true).rfind("off duty", 0) == 0 ? 10 : 0;
            nightGuards += n.g.unit(i)->activity() == Activity::Guard ? 1 : 0;
            dayGuards += d.g.unit(i)->activity() == Activity::Guard ? 1 : 0;
        }
        record("T206", "Duty roster: by day everyone is outdoors on duty; at night 3 off duty + the technician are in the bunker",
               "day underground 0, night underground 4, off-duty rosters 3 (night) / 0 (day), night guards 4",
               fmt("day underground %d, night underground %d, off duty %d, night guards %d, day guards %d (%s)", dayUnder, nightUnder,
                   offDuty, nightGuards, dayGuards, n.g.roster(3, false).c_str()),
               dayUnder == 0 && nightUnder == 4 && offDuty == 3 && nightGuards == 4);
    }

    // ---- T207: shift change into the bunker: ramp, blast doors (wait while closed), tunnel and rooms ---------------
    {
        ::World w(17.5f);
        Controllable* doors = nullptr;
        for (const auto& c : baseScene().controllables())
            if (nameHas(c->name(), "BLAST DOORS")) doors = c.get();
        ControlInput toggle;
        toggle.actionPressed = true;
        WorldContext dw;
        dw.dt = 0.0f;
        doors->update(dw, &toggle);   // start closing
        dw.dt = 5.0f;
        doors->update(dw, nullptr);   // closed
        w.hour = 18.0f;               // dusk passed: night shift
        const WalkSpace& ws = baseScene().walkSpace();
        int waited = 0, insideWhileClosed = 0, wallHits = 0, arrived = 0;
        for (int f = 0; f < static_cast<int>(240.0f / kDt); ++f) {
            const bool closed = doors->status().find("CLOSED") != std::string::npos;
            if (closed && f * kDt > 100.0f) {   // open after 100 s
                doors->update(dw, &toggle);
            }
            if (!closed && doors->status().find("OPEN ") == std::string::npos) { dw.dt = kDt; doors->update(dw, nullptr); }
            w.step();
            for (int i = 0; i < 8; ++i) {
                const Soldier* s = w.g.unit(i);
                const Vec3 p = s->position();
                if (s->activity() == Activity::Wait && p.z > 4.0f && p.z < 6.5f && p.y < -3.0f) ++waited;
                if (closed && p.z < 3.6f && p.y < -3.0f && f * kDt < 100.0f && std::fabs(p.x) < 2.0f && p.z > -0.5f) {
                    // somebody passed the closed doors (nobody starts in the bunker at 17:30)
                    ++insideWhileClosed;
                }
                if (p.y < -1.0f) {
                    float x = p.x, z = p.z;
                    ws.collide(x, z, p.y, p.y + 1.8f, 0.2f, {});
                    if (std::hypot(x - p.x, z - p.z) > 0.02f) {
                        ++wallHits;
                        if (std::getenv("TS_GARRISON_DEBUG"))
                            std::printf("  wall t %.1f soldier %d at (%.2f %.2f %.2f) pushed (%.2f %.2f) %s\n", w.t, i, p.x, p.y, p.z, x, z, s->status().c_str());
                    }
                }
            }
        }
        for (int i = 0; i < 8; ++i) {
            if (w.g.unit(i)->underground() && !w.g.unit(i)->travelling()) ++arrived;
            if (std::getenv("TS_GARRISON_DEBUG"))
                std::printf("  end %d (%.2f %.2f %.2f) %s\n", i, w.g.unit(i)->position().x, w.g.unit(i)->position().y, w.g.unit(i)->position().z, w.g.unit(i)->status().c_str());
        }
        // leave the doors open for the other tests
        if (doors->status().find("OPEN") == std::string::npos) { doors->update(dw, &toggle); dw.dt = 5.0f; doors->update(dw, nullptr); }
        record("T207", "Shift change into the bunker: down the ramp, wait at the closed blast doors, through the tunnel to the rooms",
               "frames waiting at the doors > 0, nobody passes while closed, no wall contact (r 0.2 m), 4 soldiers settled indoors",
               fmt("waiting frames %d, passed closed doors %d, wall contacts %d, settled indoors %d", waited, insideWhileClosed, wallHits, arrived),
               waited > 0 && insideWhileClosed == 0 && wallHits == 0 && arrived == 4);
    }

    // ---- T208: possession: WASD walk, Shift run, collision, stance cycle, release back to duty ------------------------
    {
        ::World w(12.0f);
        Soldier* s = w.g.unit(2);
        s->place(20.0f, -8.0f, 90.0f);   // facing +X, open apron
        ControlInput in;
        in.forward = 1.0f;
        for (int f = 0; f < 90; ++f) w.step(2, &in);
        const float walked = s->position().x - 20.0f;
        in.boost = true;
        float runSpeed = 0.0f;
        for (int f = 0; f < 60; ++f) { w.step(2, &in); runSpeed = std::fmax(runSpeed, s->speed()); }
        // walk into the ammunition bunker mound: blocked
        s->place(10.5f, 20.5f, 0.0f);
        in.boost = false;
        for (int f = 0; f < 150; ++f) w.step(2, &in);
        const float pen = w.g.planner().clearance(s->position().x, s->position().z);
        const float stuckZ = s->position().z;
        in = ControlInput{};
        std::string seq;
        for (int k = 0; k < 3; ++k) {
            in.actionPressed = true;
            w.step(2, &in);
            in.actionPressed = false;
            w.step(2, &in);
            seq += activityName(s->activity());
            seq += ">";
        }
        for (int f = 0; f < 45; ++f) w.step();
        const bool back = s->travelling();
        record("T208", "Possessed soldier: W walks, Shift runs, solids stop him, \\ cycles crouch / wave / stand, release resumes duty",
               "walk 3 s > 3.3 m, run > 3.0 m/s, stops in front of the mound (z < 22.5, clearance > 0.15), CROUCHED>WAVING>IDLE, travels after release",
               fmt("walked %.2f m, run %.2f m/s, stopped at z %.2f (clearance %.2f), %s, travelling %d", walked, runSpeed, stuckZ, pen,
                   seq.c_str(), back ? 1 : 0),
               walked > 3.3f && runSpeed > 3.0f && stuckZ < 22.5f && pen > 0.15f && seq == "CROUCHED>WAVING>IDLE>" && back);
    }

    // ---- T209: thermal signature: warm face and hands, clothed torso, never a target core ---------------------------
    {
        ::World w(12.0f);
        float maxHeat = 0.0f, face = 0.0f, hand = 0.0f, torso = 0.0f;
        for (int i = 0; i < 8; ++i) {
            int depth = 0;
            std::vector<const SceneNode*> leaves;
            countLeaves(*w.g.unit(i)->rig().root, 0, depth, leaves);
            for (const SceneNode* l : leaves) {
                maxHeat = std::fmax(maxHeat, l->material.heat);
                if (l->name == "Head") face = l->material.heat;
                if (l->name == "Hand") hand = l->material.heat;
                if (l->name == "Torso") torso = l->material.heat;
            }
        }
        const float fr = w.g.unit(0)->friendlyRadius();
        record("T209", "Soldier heat: face / hands 0.82, torso 0.70, hottest part below the 0.90 target core; friendly radius 1.2 m",
               "face 0.82, hands 0.82, torso 0.70, max < 0.90, friendlyRadius 1.2",
               fmt("face %.2f, hands %.2f, torso %.2f, max %.2f, friendly %.1f m", face, hand, torso, maxHeat, fr),
               std::fabs(face - 0.82f) < 1e-4f && std::fabs(hand - 0.82f) < 1e-4f && std::fabs(torso - 0.70f) < 1e-4f && maxHeat < 0.90f &&
                   std::fabs(fr - 1.2f) < 1e-4f);
    }

    // ---- T210: work loops: mechanic wrench at two vehicles, logistics crate carried from the depot to the ammo bunker
    {
        ::World w(12.0f);
        const int mech = findSoldier(w, Duty::Mechanic), logi = findSoldier(w, Duty::Logistics);
        std::vector<std::string> mechStops;
        int wrenchFrames = 0, crateFrames = 0, crateMismatch = 0, pickUps = 0, drops = 0;
        bool carried = w.g.unit(logi)->carrying();
        for (int f = 0; f < static_cast<int>(400.0f / kDt); ++f) {
            w.step();
            const Soldier* m = w.g.unit(mech);
            const std::string st = m->status();
            if (std::getenv("TS_GARRISON_DEBUG") && f % 150 == 0)
                std::printf("  mech t %.0f (%.1f %.1f) %s\n", w.t, m->position().x, m->position().z, st.c_str());
            const size_t at = st.find(" AT ");
            if (at != std::string::npos && m->activity() == Activity::Work) {
                const std::string lab = st.substr(at + 4, st.find("  ", at + 4) - at - 4);
                if (mechStops.empty() || mechStops.back() != lab) mechStops.push_back(lab);
                if (m->rig().wrench->visible) ++wrenchFrames;
            }
            const Soldier* l = w.g.unit(logi);
            if (l->carrying() != carried) { (l->carrying() ? pickUps : drops)++; carried = l->carrying(); }
            if (l->carrying()) ++crateFrames;
            if (l->rig().crate->visible != l->carrying()) ++crateMismatch;
        }
        std::string list;
        for (const auto& x : mechStops) list += x + ";";
        record("T210", "Work loops: the mechanic works at both vehicles (wrench shown), logistics picks up and sets down crates",
               ">= 3 mechanic stop visits over 2 sites, wrench visible while working, >= 1 pick-up and >= 1 drop, crate shown iff carried",
               fmt("mechanic stops %s wrench frames %d, crate pick-ups %d drops %d, carried frames %d, mismatches %d", list.c_str(),
                   wrenchFrames, pickUps, drops, crateFrames, crateMismatch),
               mechStops.size() >= 3 && wrenchFrames > 0 && pickUps >= 1 && drops >= 1 && crateMismatch == 0);
    }

    // ---- T211: blast reaction: duck (crouch, arms over the helmet), then back to duty --------------------------------
    {
        ::World w(12.0f);
        Soldier* s = w.g.unit(1);
        for (int f = 0; f < 10; ++f) w.step();
        const float pelvis0 = s->rig().pelvis->local.translation.y;
        const Vec3 p = s->position();
        const bool near = s->onBlast(p + Vec3{4.0f, 0.0f, 0.0f}, 1.0f);
        const bool far = s->onBlast(p + Vec3{60.0f, 0.0f, 0.0f}, 1.0f);
        w.step();
        const bool ducked = s->activity() == Activity::Duck;
        for (int f = 0; f < 15; ++f) w.step();
        const float drop = pelvis0 - s->rig().pelvis->local.translation.y;
        const float arms = s->pose().shoulder[0];
        for (int f = 0; f < 100; ++f) w.step();
        const bool recovered = s->activity() != Activity::Duck;
        record("T211", "Blast reaction: a near blast makes the soldier duck (no damage model), a far one is ignored, then back to duty",
               "near -> true + DUCK, far -> false, pelvis drops > 0.3 m within 0.5 s, arms raised > 100 deg, recovered after 3.5 s",
               fmt("near %d far %d duck %d, pelvis drop %.2f m, shoulder %.0f deg, recovered %d", near, far, ducked, drop, arms, recovered),
               near && !far && ducked && drop > 0.3f && arms > 100.0f && recovered);
    }

    // ---- T212: possession cameras: third person behind and above, first person at the eyes, indoor framing -----------
    {
        ::World w(23.0f);
        Soldier* out = w.g.unit(0);
        const Vec3 p = out->position();
        const float yr = radians(out->yawDeg());
        const Vec3 f{std::sin(yr), 0, std::cos(yr)};
        const CameraHint tp = out->camera(false), fp = out->camera(true);
        const bool tpOk = dot(p - tp.eye, f) > 2.0f && tp.eye.y > p.y + 1.8f && dot(tp.target - tp.eye, f) > 0.0f;
        const bool fpOk = std::fabs(fp.eye.y - p.y - soldier::kEyeHeight) < 0.05f && dot(fp.target - fp.eye, f) > 5.0f;
        const Soldier* in = nullptr;
        for (int i = 0; i < 8; ++i)
            if (w.g.unit(i)->underground()) in = w.g.unit(i);
        const CameraHint ip = in->camera(false);
        const float ceiling = baseScene().walkSpace().ceilingHeight(in->position().x, in->position().z, in->position().y);
        const bool inOk = ip.eye.y < ceiling - 0.1f;
        record("T212", "Possession cameras: chase view behind / above, first-person view at eye height, indoor chase under the ceiling",
               "third person > 2 m behind, above the head; first person at 1.62 m looking ahead; indoor eye below the ceiling",
               fmt("tp back %.2f m up %.2f m, fp eye %.2f m, indoor eye %.2f / ceiling %.2f", dot(p - tp.eye, f), tp.eye.y - p.y,
                   fp.eye.y - p.y, ip.eye.y, ceiling),
               tpOk && fpOk && inOk);
    }

    // ---- T213: speed law on the curved routes and staggered shift departures --------------------------------------
    {
        ::World w(12.0f);
        const int pat = findSoldier(w, Duty::Patrol);
        float worst = 0.0f, slowCurve = 9.0f, fastStraight = 0.0f;
        for (int f = 0; f < static_cast<int>(120.0f / kDt); ++f) {
            w.step();
            const Soldier* s = w.g.unit(pat);
            const float v = s->speed();
            if (!s->travelling()) continue;
            const float k = s->routeCurvature();
            const float cap = std::fmax(0.25f, 1.3f * 1.02f / (1.0f + 1.5f * k));
            worst = std::fmax(worst, v - cap - 0.05f);
            if (std::getenv("TS_GARRISON_DEBUG") && v > cap + 0.05f)
                std::printf("  speed t %.2f v %.2f k %.2f cap %.2f left %.2f %s [%s]\n", w.t, v, k, cap, s->routeLeft(), s->status().c_str(), s->debugState().c_str());
            if (k > 0.5f) slowCurve = std::fmin(slowCurve, v);
            if (k < 0.05f) fastStraight = std::fmax(fastStraight, v);
        }
        // staggered departures at dusk
        ::World d(17.5f);
        d.hour = 18.0f;
        std::vector<float> leave;
        bool moving[8] = {};
        for (int f = 0; f < static_cast<int>(30.0f / kDt); ++f) {
            d.step();
            for (int i = 0; i < 8; ++i)
                if (!moving[i] && d.g.unit(i)->onDayShift() == false) { moving[i] = true; leave.push_back(d.t); }
        }
        std::sort(leave.begin(), leave.end());
        float minGap = 99.0f;
        for (size_t i = 1; i < leave.size(); ++i) minGap = std::fmin(minGap, leave[i] - leave[i - 1]);
        const int changes = d.g.takeShiftChanges();
        record("T213", "Route speed law v = vmax / (1 + 1.5 kappa) and staggered shift departures at dusk",
               "speed never above the law (floor 0.25 m/s, +0.05 m/s tolerance), curves slower than straights, 8 departures >= 2 s apart",
               fmt("excess %.2f m/s, curve min %.2f / straight max %.2f m/s, departures %zu, min gap %.2f s, events %d", worst, slowCurve,
                   fastStraight, leave.size(), minGap, changes),
               worst <= 0.0f && slowCurve < fastStraight && leave.size() == 8 && minGap >= 2.0f && changes == 8);
    }

    // ---- T214: 300 s with a dusk shift change: soldiers never overlap (separation, yielding) -------------------------
    {
        ::World w(17.3f, 42u);
        float minDist = 99.0f;
        int overlaps = 0, stuck = 0;
        float stillT[8] = {};
        for (int f = 0; f < static_cast<int>(300.0f / kDt); ++f) {
            w.hour = 17.3f + 0.6f * w.t / 300.0f;   // crosses 17.6 (dusk shift change) at 150 s
            w.step();
            for (int i = 0; i < 8; ++i) {
                const Vec3 a = w.g.unit(i)->position();
                for (int j = i + 1; j < 8; ++j) {
                    const Vec3 b = w.g.unit(j)->position();
                    if (std::fabs(a.y - b.y) > 1.0f) continue;
                    const float d = std::hypot(a.x - b.x, a.z - b.z);
                    minDist = std::fmin(minDist, d);
                    if (d < 0.5f) ++overlaps;
                }
                stillT[i] = w.g.unit(i)->activity() == Activity::Wait ? stillT[i] + kDt : 0.0f;
                if (stillT[i] > 20.0f) { ++stuck; stillT[i] = 0.0f; }
            }
        }
        int settled = 0;
        for (int i = 0; i < 8; ++i) settled += w.g.unit(i)->onDayShift() ? 0 : 1;
        record("T214", "300 s of duty with a dusk shift change (seed 42): soldiers keep apart and nobody stays blocked",
               "min distance >= 0.5 m (0 overlap frames), no wait > 20 s, all 8 on the night roster",
               fmt("min distance %.2f m, overlap frames %d, long waits %d, night roster %d", minDist, overlaps, stuck, settled),
               overlaps == 0 && stuck == 0 && settled == 8);
    }
}
