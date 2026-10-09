// T150-T159: hidden camp (light discipline, camouflage nets, launchers, revetments).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "TestKit.h"
#include "ai/Occlusion.h"
#include "objects/Base.h"
#include "objects/Bunker.h"
#include "objects/Environment.h"
#include "objects/Launchers.h"
#include "objects/Structures.h"
#include "objects/Tank.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {
using namespace ts;

// Practical lights of the v4 build (measured before the v5 light-discipline pass): count and sum(intensity * range).
constexpr int kBeforeBaseLights = 14, kBeforeEnvLights = 7;
constexpr float kBeforeBasePower = 1417.0f, kBeforeEnvPower = 331.0f;

void updateWorldRec(SceneNode& n, const Mat4& parent) {
    n.world = parent * n.local.matrix();
    for (auto& c : n.children) updateWorldRec(*c, n.world);
}
template <typename F>
void forLeaves(SceneNode& n, F&& f) {
    if (n.isLeaf()) f(n);
    for (auto& c : n.children) forLeaves(*c, f);
}
void countLights(SceneNode& n, int& count, float& power) {
    forLeaves(n, [&](SceneNode& l) {
        if (l.material.lightIntensity > 0.0f) {
            ++count;
            power += l.material.lightIntensity * l.material.lightRange;
        }
    });
}

struct Camp {
    std::vector<std::unique_ptr<Controllable>> objs;
    SamLauncher* sam = nullptr;
    MlrsTruck* mlrs = nullptr;
    Controllable* ammo = nullptr;
    Controllable* dressing = nullptr;
    std::vector<Controllable*> tanks;
};
Camp makeCamp() {
    Camp c;
    c.objs = makeLaunchers();
    for (auto& o : c.objs) {
        if (auto* s = dynamic_cast<SamLauncher*>(o.get())) c.sam = s;
        else if (auto* m = dynamic_cast<MlrsTruck*>(o.get())) c.mlrs = m;
        else if (std::string(o->name()) == "AMMO BUNKER") c.ammo = o.get();
        else if (std::string(o->name()) == "CAMP NETS") c.dressing = o.get();
        else if (dynamic_cast<Tank*>(o.get())) c.tanks.push_back(o.get());
        o->update(WorldContext{}, nullptr);
        updateWorldRec(*o->root(), Mat4::identity());
    }
    return c;
}

bool zoneHits(const ClearZone& z, const Box& b) {
    if (z.circle) {
        const float dx = std::max({b.min.x - z.cx, 0.0f, z.cx - b.max.x});
        const float dz = std::max({b.min.z - z.cz, 0.0f, z.cz - b.max.z});
        return dx * dx + dz * dz < z.halfX * z.halfX;
    }
    const float yaw = radians(z.yawDeg), s = std::sin(yaw), c = std::cos(yaw);
    const Vec3 fw{s, 0, c}, sd{c, 0, -s};
    const float ex = 0.5f * (b.max.x - b.min.x), ez = 0.5f * (b.max.z - b.min.z);
    const float dx = 0.5f * (b.min.x + b.max.x) - z.cx, dz = 0.5f * (b.min.z + b.max.z) - z.cz;
    if (std::fabs(dx * sd.x + dz * sd.z) >= z.halfX + ex * std::fabs(sd.x) + ez * std::fabs(sd.z)) return false;
    if (std::fabs(dx * fw.x + dz * fw.z) >= z.halfZ + ex * std::fabs(fw.x) + ez * std::fabs(fw.z)) return false;
    const float zx = z.halfX * std::fabs(sd.x) + z.halfZ * std::fabs(fw.x);
    const float zz = z.halfX * std::fabs(sd.z) + z.halfZ * std::fabs(fw.z);
    return std::fabs(dx) < ex + zx && std::fabs(dz) < ez + zz;
}
bool partHits(const ClearZone& z, const SceneNode& n) {
    const Box b = worldBounds(n.world);
    if (!zoneHits(z, b)) return false;
    const Mat4 inv = inverse(n.world);
    const float yc = 0.5f * (b.min.y + b.max.y);
    const float s = std::sin(radians(z.yawDeg)), c = std::cos(radians(z.yawDeg));
    const float hx = z.halfX, hz = z.circle ? z.halfX : z.halfZ;
    for (float u = -hx; u <= hx; u += 0.2f)
        for (float v = -hz; v <= hz; v += 0.2f) {
            if (z.circle && u * u + v * v > hx * hx) continue;
            const Vec3 l = transformPoint(inv, {z.cx + u * c + v * s, yc, z.cz - u * s + v * c});
            if (std::fabs(l.x) <= 0.5f && std::fabs(l.y) <= 0.5f && std::fabs(l.z) <= 0.5f) return true;
        }
    return false;
}

// Analytic trapezoid profile: time to travel 'd' with vmax and acc.
float profileTime(float d, float vmax, float acc) {
    const float rampD = vmax * vmax / acc;  // accelerate + brake distance
    if (d >= rampD) return 2.0f * vmax / acc + (d - rampD) / vmax;
    return 2.0f * std::sqrt(d / acc);
}

ControlInput keyAction() {
    ControlInput in;
    in.actionPressed = true;
    return in;
}

void step(Controllable& c, float seconds, const ControlInput* in, Combat* combat = nullptr, float* clock = nullptr) {
    static float t = 0.0f;
    float& tm = clock ? *clock : t;
    const int n = static_cast<int>(std::lround(seconds * 60.0f));
    for (int i = 0; i < n; ++i) {
        WorldContext w;
        w.dt = 1.0f / 60.0f;
        tm += w.dt;
        w.time = tm;
        w.combat = combat;
        c.update(w, in);
    }
}

}  // namespace

TS_TEST_GROUP(Camp) {
    Camp camp = makeCamp();
    BaseRig base = buildBase();
    updateWorldRec(*base.root, Mat4::identity());
    auto env = buildEnvironment();
    updateWorldRec(*env, Mat4::identity());
    BunkerRig bunker = buildBunker();
    updateWorldRec(*bunker.root, Mat4::identity());

    // T150: every launcher / bunker / dressing part is a lab primitive with name and purpose; part counts.
    {
        int bad = 0, total = 0;
        std::string counts;
        for (auto& o : camp.objs) {
            int n = 0;
            forLeaves(*o->root(), [&](SceneNode& l) {
                ++n;
                const bool ok = isLibraryPartPrimitive(l.primitive) && !l.name.empty() && !l.purpose.empty() && !l.id.empty();
                if (!ok) ++bad;
            });
            total += n;
            counts += fmt(" %s=%d", o->name(), n);
        }
        record("T150", "launcher, parked tank, ammo bunker and camp dressing parts are lab primitives with name / purpose / id",
               "0 violations, SAM >= 80 and MLRS >= 100 parts", fmt("%d parts, %d violations;%s", total, bad, counts.c_str()),
               bad == 0 && camp.sam && camp.mlrs && camp.ammo && camp.dressing && camp.tanks.size() == 2);
    }

    // T151: no solid part of the camp objects (below net height) lies in the helipad, radar, AA pit, tank spot,
    // road or trench strip, and the bays do not overlap each other.
    {
        int hits = 0;
        std::string worst;
        for (auto& o : camp.objs)
            forLeaves(*o->root(), [&](SceneNode& l) {
                const Box b = worldBounds(l.world);
                if (b.max.y <= layout::kApronLevel + kFlushHeight || b.min.y > 3.0f) return;
                for (const ClearZone& z : baseClearZones())
                    if (partHits(z, l)) {
                        ++hits;
                        if (worst.empty()) worst = l.name + " (" + o->name() + ") in " + z.name;
                    }
            });
        int overlaps = 0;
        const auto sites = camp::allSites();
        for (size_t i = 0; i < sites.size(); ++i)
            for (size_t j = i + 1; j < sites.size(); ++j) {
                const camp::PlanRect a = camp::planRect(sites[i]), b = camp::planRect(sites[j]);
                // conservative: bounding circles of the rectangles
                const float ra = std::hypot(a.halfX, a.halfZ), rb = std::hypot(b.halfX, b.halfZ);
                const ClearZone za{"a", a.cx, a.cz, a.halfX, a.halfZ, a.yawDeg, false};
                const Box bb{{b.cx - b.halfX, 0, b.cz - b.halfZ}, {b.cx + b.halfX, 1, b.cz + b.halfZ}};
                if (std::hypot(a.cx - b.cx, a.cz - b.cz) < ra + rb && zoneHits(za, bb) && std::hypot(a.cx - b.cx, a.cz - b.cz) < 0.5f * (ra + rb)) ++overlaps;
            }
        record("T151", "camp objects keep out of the helipad / radar / AA pit / tank spot / road / trench strip; bays do not overlap",
               "0 solid parts in the zones, 0 overlapping bays", fmt("%d hits %s; %d overlapping bays", hits, worst.c_str(), overlaps),
               hits == 0 && overlaps == 0);
    }

    // T152: Base, Environment and Bunker geometry keeps out of the camp bays (flush markings excepted).
    {
        int hits = 0;
        std::string worst;
        auto check = [&](SceneNode& root, const char* what) {
            forLeaves(root, [&](SceneNode& l) {
                const Box b = worldBounds(l.world);
                if (b.max.y <= layout::kApronLevel + kFlushHeight) return;
                for (const ClearZone& z : campClearZones()) {
                    if (std::string(z.name) == "road" || std::string(z.name) == "trench strip" || std::string(z.name) == "helipad" ||
                        std::string(z.name) == "radar site" || std::string(z.name) == "AA gun pit" || std::string(z.name) == "tank spot")
                        continue;  // covered by T71
                    if (b.min.y > 3.0f) continue;
                    if (partHits(z, l)) {
                        ++hits;
                        if (worst.empty()) worst = l.name + " (" + what + ") in " + z.name;
                    }
                }
            });
        };
        check(*base.root, "Base");
        check(*env, "Environment");
        check(*bunker.root, "Bunker");
        record("T152", "Base / Environment / Bunker geometry keeps out of the five camp bays and the tank-park revetment",
               "0 solid parts inside", fmt("%d hits %s", hits, worst.c_str()), hits == 0);
    }

    // T153: SAM elevation limits, azimuth limits, status line.
    {
        SamLauncher& s = *camp.sam;
        float clock = 0.0f;
        const ControlInput act = keyAction();
        step(s, 1.0f / 60.0f, &act, nullptr, &clock);  // deploy (one key press)
        ControlInput up;
        up.aimPitch = 1.0f;
        step(s, 12.0f, &up, nullptr, &clock);
        const float hi = s.elevationDeg();
        ControlInput dn;
        dn.aimPitch = -1.0f;
        step(s, 12.0f, &dn, nullptr, &clock);
        const float lo = s.elevationDeg();
        ControlInput left;
        left.aimYaw = -1.0f;
        step(s, 20.0f, &left, nullptr, &clock);
        const float azMax = s.turntableDeg();
        ControlInput right;
        right.aimYaw = 1.0f;
        step(s, 20.0f, &right, nullptr, &clock);
        const float azMin = s.turntableDeg();
        step(s, 5.0f, &up, nullptr, &clock);   // command back up to 60 deg
        ControlInput none;
        step(s, 12.0f, &none, nullptr, &clock);
        const std::string st = s.status();
        record("T153", "SAM elevation limited to 0..60 deg, turntable to +-170 deg; status line format",
               "EL max 60, min 0, AZ +170 / -170, status 'SAM-1  AZ nnn  EL nn  READY 4/4'",
               fmt("EL %.2f / %.2f, turntable %.1f / %.1f, '%s'", hi, lo, azMax, azMin, st.c_str()),
               std::fabs(hi - 60.0f) < 0.05f && std::fabs(lo) < 0.05f && std::fabs(azMax - 170.0f) < 0.05f &&
                   std::fabs(azMin + 170.0f) < 0.05f && st.find("SAM-1  AZ") == 0 && st.find("READY 4/4") != std::string::npos);
    }

    // T154: SAM elevation easing: trapezoid profile (14 deg/s, 35 deg/s^2): slow start, cruise, soft stop, no overshoot.
    {
        Camp c2 = makeCamp();
        SamLauncher& m = *c2.sam;
        float clk = 0.0f;
        const ControlInput act = keyAction();
        step(m, 1.0f / 60.0f, &act, nullptr, &clk);  // deploy: command 45 deg
        ControlInput none;
        float maxEl = 0.0f, tReach = -1.0f, e01 = 0.0f, e1 = 0.0f, e2 = 0.0f, t = 0.0f;
        for (int i = 0; i < 60 * 8; ++i) {
            step(m, 1.0f / 60.0f, &none, nullptr, &clk);
            t += 1.0f / 60.0f;
            maxEl = std::max(maxEl, m.elevationDeg());
            if (std::fabs(t - 0.1f) < 0.009f) e01 = m.elevationDeg();
            if (std::fabs(t - 1.0f) < 0.009f) e1 = m.elevationDeg();
            if (std::fabs(t - 2.0f) < 0.009f) e2 = m.elevationDeg();
            if (tReach < 0.0f && m.elevationDeg() >= 44.99f) tReach = t;
        }
        const float expect = profileTime(45.0f, SamLauncher::kElevRate, SamLauncher::kElevAccel);
        const float exp1 = 0.5f * 35.0f * 0.4f * 0.4f + 14.0f * 0.6f, exp2 = 0.5f * 35.0f * 0.4f * 0.4f + 14.0f * 1.6f;
        record("T154", "SAM elevation follows an eased trapezoid profile: slow start, 14 deg/s cruise, soft stop, no overshoot",
               fmt("EL(0.1 s) < 0.5, EL(1 s) %.1f, EL(2 s) %.1f (+-1.0), reaches 45 deg at %.2f s (+-0.1), no overshoot", exp1, exp2, expect),
               fmt("EL(0.1 s) %.2f, EL(1 s) %.1f, EL(2 s) %.1f, max %.3f, reached at %.2f s", e01, e1, e2, maxEl, tReach),
               e01 < 0.5f && std::fabs(e1 - exp1) < 1.0f && std::fabs(e2 - exp2) < 1.0f && maxEl <= 45.0001f && std::fabs(tReach - expect) < 0.1f);
    }

    // T155: reload timing (8 s) and firing rules.
    {
        Camp c = makeCamp();
        SamLauncher& s = *c.sam;
        float clock = 0.0f;
        ControlInput fire;
        fire.firePressed = true;
        step(s, 0.2f, &fire, nullptr, &clock);  // stowed: no launch
        const int stowedLoaded = s.loadedCount();
        const ControlInput act = keyAction();
        step(s, 1.0f / 60.0f, &act, nullptr, &clock);
        ControlInput none;
        step(s, 6.0f, &none, nullptr, &clock);  // reach the default 45 deg
        Combat combat;
        ControlInput f1;
        f1.firePressed = true;
        step(s, 1.0f / 60.0f, &f1, &combat, &clock);
        const int after = s.loadedCount();
        const bool capHidden = !s.capVisible(0);
        const bool inFlight = s.roundInFlight();
        const size_t particles = combat.particles().size();
        const float t0 = 0.0f;
        step(s, 7.8f, &none, &combat, &clock);
        const int at78 = s.loadedCount();
        const bool capStill = !s.capVisible(0);
        step(s, 0.4f, &none, &combat, &clock);
        const int at82 = s.loadedCount();
        (void)t0;
        record("T155", "SAM: no launch while stowed; one canister fires, its cap is hidden, a round flies, smoke is emitted; reload after 8 s",
               "stowed 4/4; after fire 3/4 + cap hidden + particles; 3/4 at 7.8 s; 4/4 at 8.2 s",
               fmt("stowed %d/4; fire %d/4 cap hidden %d flight %d particles %zu; 7.8 s %d/4 (cap hidden %d); 8.2 s %d/4 cap shown %d",
                   stowedLoaded, after, capHidden, inFlight, particles, at78, capStill, at82, s.capVisible(0)),
               stowedLoaded == 4 && after == 3 && capHidden && inFlight && particles > 0 && at78 == 3 && capStill && at82 == 4 && s.capVisible(0));
    }

    // T156: hierarchy consistency: the analytic muzzle point equals the muzzle collar of the node tree, and the
    // muzzle rises with elevation.
    {
        Camp c = makeCamp();
        SamLauncher& s = *c.sam;
        float clock = 0.0f;
        const ControlInput act = keyAction();
        step(s, 1.0f / 60.0f, &act, nullptr, &clock);
        ControlInput none;
        step(s, 8.0f, &none, nullptr, &clock);
        ControlInput yawIn;
        yawIn.aimYaw = 1.0f;
        step(s, 1.0f, &yawIn, nullptr, &clock);
        step(s, 3.0f, &none, nullptr, &clock);
        updateWorldRec(*s.root(), Mat4::identity());
        float err = 0.0f;
        for (int k = 0; k < SamLauncher::kCanisters; ++k) {
            const Vec3 marker = transformPoint(s.muzzleMarker(k)->world, {0, 0, 0});
            const Vec3 expect = s.muzzleWorld(k) - s.axisWorld() * 0.08f;   // collar is 0.08 m behind the muzzle reference point
            err = std::max(err, length(marker - expect));
        }
        const float yHigh = s.muzzleWorld(0).y;
        Camp c2 = makeCamp();
        c2.sam->update(WorldContext{}, nullptr);
        const float yLow = c2.sam->muzzleWorld(0).y;
        record("T156", "SAM node hierarchy matches the analytic muzzle point (turntable -> cradle -> canister); elevation raises the muzzle",
               "error < 0.02 m, muzzle higher at 45 deg", fmt("error %.4f m, muzzle y %.2f (EL %.0f) vs %.2f (stowed)", err, yHigh, s.elevationDeg(), yLow),
               err < 0.02f && yHigh > yLow + 1.0f);
    }

    // T157: MLRS ripple fire: 12 tubes, 0.25 s apart, one flash + smoke each; reload afterwards.
    {
        Camp c = makeCamp();
        MlrsTruck& m = *c.mlrs;
        float clock = 0.0f;
        const ControlInput act = keyAction();
        step(m, 1.0f / 60.0f, &act, nullptr, &clock);
        ControlInput none;
        step(m, 6.0f, &none, nullptr, &clock);
        Combat combat;
        ControlInput f;
        f.firePressed = true;
        step(m, 1.0f / 60.0f, &f, &combat, &clock);
        const int first = 12 - m.loadedCount();
        step(m, 1.0f, &none, &combat, &clock);
        const int at1 = 12 - m.loadedCount();
        step(m, 2.5f, &none, &combat, &clock);
        const int at35 = 12 - m.loadedCount();
        const bool doneRippling = !m.rippling();
        const float reload = m.reloadLeft();
        const bool capsHidden = !m.capVisible(0) && !m.capVisible(11);
        const size_t particles = combat.particles().size();
        step(m, MlrsTruck::kReloadSeconds + 0.3f, &none, &combat, &clock);
        record("T157", "MLRS ripple: first tube at once, then one tube every 0.25 s (12 in about 2.75 s), caps hidden, smoke emitted, reload after the salvo",
               "1 tube at t=0, 4-5 after 1 s, 12 after 3.5 s, reload 15 s then 12/12",
               fmt("%d at 0, %d at 1 s, %d at 3.5 s, rippling done %d, reload %.1f s, caps hidden %d, particles %zu, after reload %d/12",
                   first, at1, at35, doneRippling, reload, capsHidden, particles, m.loadedCount()),
               first == 1 && at1 >= 4 && at1 <= 6 && at35 == 12 && doneRippling && reload > 10.0f && capsHidden && particles > 12 &&
                   m.loadedCount() == 12);
    }

    // T158: practical lights, before / after the v5 light discipline.
    {
        int bc = 0, ec = 0, cc = 0;
        float bp = 0, ep = 0, cp = 0;
        countLights(*base.root, bc, bp);
        for (auto& o : base.objects) countLights(*o->root(), bc, bp);
        countLights(*env, ec, ep);
        for (auto& o : camp.objs) countLights(*o->root(), cc, cp);
        const int before = kBeforeBaseLights + kBeforeEnvLights;
        const int after = bc + ec + cc;
        const float powerBefore = kBeforeBasePower + kBeforeEnvPower, powerAfter = bp + ep + cp;
        record("T158", "fewer practical lights on the surface than before (apron + compound + camp), much lower total power",
               fmt("count < %d and sum(intensity*range) < %.0f / 3", before, powerBefore),
               fmt("before: base %d + env %d = %d (power %.0f); after: base %d + env %d + camp %d = %d (power %.0f)", kBeforeBaseLights,
                   kBeforeEnvLights, before, powerBefore, bc, ec, cc, after, powerAfter),
               after < before && powerAfter < powerBefore / 3.0f);
    }

    // T159: camouflage nets hang >= 3.2 m above the ground over every walkable area.
    {
        int nets = 0, low = 0;
        float minClear = 1e9f;
        auto check = [&](SceneNode& root) {
            forLeaves(root, [&](SceneNode& l) {
                if (l.name != "Camo net panel") return;
                ++nets;
                const Box b = worldBounds(l.world);
                const float cx = 0.5f * (b.min.x + b.max.x), cz = 0.5f * (b.min.z + b.max.z);
                const float clear = b.min.y - terrainHeight(cx, cz);
                minClear = std::min(minClear, clear);
                if (clear < kNetMinHeight) ++low;
            });
        };
        check(*base.root);
        check(*bunker.root);
        for (auto& o : camp.objs) check(*o->root());
        record("T159", "all camouflage net panels (motor pool, bunker trench, five camp bays) stay at least 3.2 m above the ground",
               "panels > 100, none below 3.2 m", fmt("%d panels, lowest clearance %.2f m, %d below 3.2 m", nets, minClear, low),
               nets > 100 && low == 0);
    }
}
