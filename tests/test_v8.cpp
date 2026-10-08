// v8 (daylight, open world, barracks life, natural rover motion): T320-T327. CPU only.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "TestKit.h"
#include "ai/HideController.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/CameraRig.h"
#include "scene/Control.h"
#include "scene/DayCycle.h"
#include "scene/Gait.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

constexpr float kDt = 1.0f / 30.0f;
namespace H = layout::hall;

bool inHall(const Vec3& p, float margin = 0.0f) {
    return p.x > H::kX0 - margin && p.x < H::kX1 + margin && p.z > H::kZ0 - margin && p.z < H::kZ1 + margin;
}

bool finitePose(const Pose& p) {
    const float v[] = {p.hip[0], p.hip[1], p.knee[0], p.knee[1], p.ankle[0], p.ankle[1], p.shoulder[0], p.shoulder[1],
                       p.elbow[0], p.elbow[1], p.spinePitch, p.spineYaw, p.pelvisY, p.pelvisRoll, p.headYaw, p.headPitch,
                       p.bodyPitch, p.bodyRoll};
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

}  // namespace

TS_TEST_GROUP(V8) {
    // ---- T320: daylight atmosphere: whiter horizon band than the zenith, clear-day haze, the night untouched -----------
    {
        const LightRig noon = DayCycle::rigAt(12.0f), night = DayCycle::rigAt(23.0f);
        const Atmosphere& a = noon.atmo;
        const float zenBlueRatio = a.skyZenith.z / a.skyZenith.x, horBlueRatio = a.skyHorizon.z / a.skyHorizon.x;
        const Atmosphere def{};
        const bool nightSame = night.atmo.skyHorizon.x == def.skyHorizon.x && night.atmo.fogDensity == def.fogDensity &&
                               night.atmo.skyZenith.z == def.skyZenith.z;
        record("T320", "Daylight sky model: horizon band whiter than the zenith, clear-day haze, night preset unchanged",
               "noon horizon blue/red < zenith blue/red, fog <= 0.005 / vol <= 0.002, night = v5 literals",
               fmt("blue/red zenith %.2f horizon %.2f, fog %.4f vol %.4f, night same %d", zenBlueRatio, horBlueRatio, a.fogDensity,
                   a.volFogDensity, nightSame ? 1 : 0),
               horBlueRatio < zenBlueRatio && a.fogDensity <= 0.005f && a.volFogDensity <= 0.002f && nightSame);
    }

    // ---- T321: barracks hall: a door the planner walks through, a floor with a ceiling, a windowed indoor volume --------
    {
        Scene s;
        const WalkSpace& ws = s.walkSpace();
        float fy = 0.0f;
        const bool floorIn = ws.pickFloor(23.0f, -11.5f, layout::kApronLevel, fy) && std::fabs(fy - H::kFloorY) < 1e-4f;
        const float ceil = ws.ceilingHeight(23.0f, -11.5f, H::kFloorY);
        const float ground = groundHeight(&ws, 23.0f, -11.5f, layout::kApronLevel);   // beyond the 20 m terrain disc
        int hallVol = -1;
        for (size_t i = 0; i < ws.indoorVolumes().size(); ++i)
            if (ws.indoorVolumes()[i].min.x > 18.0f) hallVol = static_cast<int>(i);
        const IndoorLight hl = hallVol >= 0 ? ws.indoorLights()[static_cast<size_t>(hallVol)] : IndoorLight{};
        const IndoorLight bunker = ws.indoorLights().empty() ? IndoorLight{} : ws.indoorLights().front();
        // planner: inside, the doorway and outside of the door are free cells (the soldiers can walk in)
        SceneOptions o;
        o.residents = 1;
        Scene g(o);
        const SitePlanner& pl = g.garrison()->planner();
        auto freeAt = [&](float x, float z) { return pl.freeCell(SitePlanner::cellOf(x), SitePlanner::cellOf(z)); };
        const bool door = freeAt(H::kDoorX, -7.5f) && freeAt(H::kDoorX, -8.5f) && freeAt(H::kDoorX, -9.5f) && freeAt(21.5f, -12.5f);
        const bool walls = !freeAt(H::kX0 + 0.1f, -12.5f) && !freeAt(H::kX1 - 0.1f, -12.5f) && !freeAt(21.5f, H::kZ1 - 0.1f);
        record("T321", "Barracks hall: walkable floor + 2.7 m ceiling, door the site planner passes, windowed indoor light",
               "floor at the slab top (also via groundHeight on the apron), ceiling 2.7 m, door cells free, walls blocked, "
               "hall volume sky 0.24 key 1, bunker volume sky 0.012 key 0",
               fmt("floor %d (%.2f), ground %.2f, ceiling %.2f, door %d, walls %d, hall sky %.3f key %.0f, bunker sky %.3f key %.0f",
                   floorIn ? 1 : 0, fy, ground, ceil - H::kFloorY, door ? 1 : 0, walls ? 1 : 0, hl.sky, hl.key, bunker.sky, bunker.key),
               floorIn && std::fabs(ground - H::kFloorY) < 1e-4f && std::fabs(ceil - H::kFloorY - H::kCeiling) < 1e-3f && door && walls &&
                   hallVol >= 0 && std::fabs(hl.sky - 0.24f) < 1e-4f && hl.key == 1.0f && bunker.key == 0.0f);
    }

    // ---- T322 / T323 / T324: residents -------------------------------------------------------------------------------
    {
        SceneOptions o = SceneOptions::v6(7, 10.5f, false);
        o.residents = Garrison::kMaxResidents;
        Scene s(o);
        Garrison* g = s.garrison();
        const int n = g ? g->count() : 0;
        std::set<int> acts[Garrison::kMaxResidents];
        bool stayed = true, unarmed = true;
        float minSep = 1e9f;
        for (float t = 0.0f; t < 420.0f; t += kDt) {
            s.update(kDt, Detection{});
            for (int i = Garrison::kSoldiers; i < n; ++i) {
                const Soldier* r = g->unit(i);
                acts[i - Garrison::kSoldiers].insert(static_cast<int>(r->activity()));
                // the hall or the PT area north of it (and the way between), never across the camp
                const Vec3 p = r->position();
                if (!(p.x > H::kX0 - 2.5f && p.x < H::kX1 + 2.5f && p.z > H::kZ0 - 2.5f && p.z < H::kZ1 + 5.5f)) stayed = false;
                if (r->rig().rifle->visible || r->rig().rifleBack->visible) unarmed = false;
                for (int j = i + 1; j < n; ++j) {
                    const Soldier* q = g->unit(j);
                    if (std::fabs(q->position().y - p.y) > 0.5f) continue;   // upper / lower bunk
                    if (r->activity() == Activity::Walk || q->activity() == Activity::Walk)
                        minSep = std::min(minSep, std::hypot(q->position().x - p.x, q->position().z - p.z));
                }
            }
        }
        const std::set<Activity> barracks = {Activity::Sleep, Activity::PushUps, Activity::PullUps, Activity::SitUps, Activity::Talk,
                                             Activity::Sit, Activity::Work};
        int varied = 0;
        std::set<int> all;
        for (int r = 0; r < n - Garrison::kSoldiers; ++r) {
            int k = 0;
            for (int a : acts[r])
                if (barracks.count(static_cast<Activity>(a))) {
                    ++k;
                    all.insert(a);
                }
            if (k >= 2) ++varied;
        }
        record("T322", "Barracks residents by day: 6 unarmed residents stay at the hall / PT area and vary their activities",
               "14 soldiers, every resident >= 2 barracks activities in 7 min, all 7 activities seen, none leaves, walkers >= 0.45 m apart",
               fmt("%d soldiers, %d / %d varied, %zu activities, stayed %d, unarmed %d, min walker gap %.2f m", n, varied,
                   n - Garrison::kSoldiers, all.size(), stayed ? 1 : 0, unarmed ? 1 : 0, minSep),
               n == Garrison::kSoldiers + Garrison::kMaxResidents && varied == Garrison::kMaxResidents && all.size() == barracks.size() &&
                   stayed && unarmed && minSep >= 0.45f);

        // T323: every planned route of the residents keeps the body clear of every part
        float worst = 1e9f;
        for (const CatmullRomPath& r : g->routes()) {
            if (r.empty()) continue;
            const Vec3 a = r.pointAtDistance(0.0f), b = r.pointAtDistance(r.length());
            if (!inHall(a, 6.0f) && !inHall(b, 6.0f)) continue;
            worst = std::min(worst, g->planner().minClearance(r));
        }
        record("T323", "Resident routes (A* -> string pulling -> Catmull-Rom) keep clear of the hall's walls, bunks and table",
               "smallest clearance >= kBodyClearance (0.28 m)", fmt("smallest clearance %.2f m", worst),
               worst >= SitePlanner::kBodyClearance - 1e-3f && worst < 1e8f);

        // T324: at night every resident is asleep in a bunk within 120 s of the shift change
        if (DayCycle* c = &s.dayCycle()) c->setHour(23.0f);
        int asleep = 0;
        for (float t = 0.0f; t < 120.0f; t += kDt) s.update(kDt, Detection{});
        for (int i = Garrison::kSoldiers; i < n; ++i)
            if (g->unit(i)->activity() == Activity::Sleep && inHall(g->unit(i)->position())) ++asleep;
        record("T324", "Night in the barracks: every resident goes to his own bunk and sleeps",
               "6 / 6 asleep in the hall 120 s after 23:00", fmt("%d / %d asleep", asleep, n - Garrison::kSoldiers),
               asleep == n - Garrison::kSoldiers);
    }

    // ---- T325: barracks poses: finite, lying poses horizontal, the push-up body inclined, pull-up hands over the head ---
    {
        bool ok = true;
        for (float t = 0.0f; t < 6.0f; t += 0.1f) {
            const Pose ps[] = {sleepPose(t), pushUpPose(t), pullUpPose(t), sitUpPose(t), talkPose(t, true), talkPose(t, false)};
            for (const Pose& p : ps) ok = ok && finitePose(p);
        }
        const Pose sl = sleepPose(1.0f), pu = pushUpPose(0.0f), pd = pushUpPose(0.9f), up = pullUpPose(0.0f), su = sitUpPose(0.55f);
        const Pose half = blendPose(Pose{}, sl, 0.5f);
        record("T325", "Barracks poses: finite; sleep and sit-ups lie on the back, push-ups face down and inclined, pull-up arms overhead",
               "sleep / sit-up pitch -90, push-up 60..80 and the chest lower at the bottom, pull-up shoulders > 140, blend halves the pitch",
               fmt("finite %d, sleep %.0f, sit-up %.0f (crunch %.0f), push-up %.0f -> %.0f (pelvis %.2f -> %.2f), pull-up %.0f, blend %.0f",
                   ok ? 1 : 0, sl.bodyPitch, su.bodyPitch, su.spinePitch, pu.bodyPitch, pd.bodyPitch, pu.pelvisY, pd.pelvisY,
                   up.shoulder[0], half.bodyPitch),
               ok && sl.bodyPitch == -90.0f && su.bodyPitch == -90.0f && su.spinePitch > 30.0f && pu.bodyPitch > 60.0f &&
                   pd.bodyPitch < 80.0f && pd.pelvisY < pu.pelvisY && up.shoulder[0] > 140.0f && std::fabs(half.bodyPitch + 45.0f) < 1e-3f);
    }

    // ---- T326: natural rover motion: speed wanders, the track weaves, hesitations; deterministic per seed ------------
    {
        Scene planner;
        const HideController grid = planner.hideController();
        auto drive = [&](const RoverPersonality& p, uint32_t seed, std::vector<Vec3>& track, std::vector<float>& speeds, int& hesitations) {
            HideController hc = grid;
            RoverState r;
            r.position = {-14.0f, terrainHeight(-14.0f, 0.0f), 0.0f};
            r.yaw = 3.14159265f * 0.5f;
            hc.setRoute({}, {14.0f, 0.0f, 0.0f}, seed, p);
            bool was = false;
            hesitations = 0;
            for (float t = 0.0f; t < 24.0f; t += kDt) {
                const Vec3 before = r.position;
                hc.update(kDt, std::vector<Vec3>{{0, 60, 60}}, r);
                if (hc.hesitating() && !was) ++hesitations;
                was = hc.hesitating();
                (void)before;
                speeds.push_back(hc.speedFactor());
                if (std::fmod(t, 0.5f) < kDt) track.push_back(r.position);
            }
        };
        RoverPersonality plain, natural;
        natural.speedWander = 0.3f;
        natural.weave = 0.35f;
        natural.weaveWavelength = 4.0f;
        natural.hesitate = 0.08f;
        std::vector<Vec3> t0, t1, t2;
        std::vector<float> s0, s1, s2;
        int h0 = 0, h1 = 0, h2 = 0;
        drive(plain, 99u, t0, s0, h0);
        drive(natural, 99u, t1, s1, h1);
        drive(natural, 99u, t2, s2, h2);
        auto spread = [](const std::vector<float>& v) {   // std of the speed modulation
            double m = 0.0, q = 0.0;
            for (float x : v) { m += x; q += x * x; }
            if (v.size() < 2) return 0.0;
            m /= static_cast<double>(v.size());
            return std::sqrt(std::max(0.0, q / static_cast<double>(v.size()) - m * m));
        };
        float sep = 0.0f, same = 0.0f;
        const size_t nT = std::min(t0.size(), t1.size());
        for (size_t i = 0; i < nT; ++i) sep = std::max(sep, std::hypot(t0[i].x - t1[i].x, t0[i].z - t1[i].z));
        for (size_t i = 0; i < std::min(t1.size(), t2.size()); ++i) same = std::max(same, std::hypot(t1[i].x - t2[i].x, t1[i].z - t2[i].z));
        const float progress = t1.empty() ? 0.0f : t1.back().x - t1.front().x;
        record("T326", "Natural rover motion: wandering speed, weaving track and hesitations; the same seed repeats exactly",
               "speed factor spread > 0.05 (plain: 0), track differs > 0.25 m, >= 1 hesitation, same seed identical, still crosses > 6 m",
               fmt("speed factor spread %.3f vs %.3f, max deviation %.2f m, %d hesitations, repeat diff %.4f m, progress %.1f m", spread(s1),
                   spread(s0), sep, h1, same, progress),
               spread(s1) > 0.05 && spread(s0) < 1e-6 && sep > 0.25f && h1 >= 1 && same < 1e-5f && progress > 6.0f);
    }

    // ---- T327: third-person helpers: camera-relative walk and the spring arm pulled in by a wall --------------------
    {
        ControlInput raw;
        // camera looks east (yaw 90), body faces north (0): W turns the body towards the camera heading
        const ControlInput w = thirdPersonSoldierInput(raw, 1.0f, 0.0f, 90.0f, 0.0f, false, 0.1f);
        // S walks towards the camera: desired heading 270 -> turn left (positive delta = right turn is lower yaw)
        const ControlInput aim = thirdPersonSoldierInput(raw, 0.0f, 1.0f, 30.0f, 0.0f, true, 0.1f);
        // spring arm: a wall 1.2 m behind the shoulder -> the arm pulls in short of it
        ThirdPersonCam cam;
        cam.reset({0.0f, 1.5f, 0.0f}, 0.0f, 0.0f);
        const Box wall{{-3.0f, 0.0f, -1.6f}, {3.0f, 3.0f, -1.4f}};
        const std::vector<Box> solids{wall};
        const ArmCast cast = [&](const Vec3& o, const Vec3& d, float len, float r) { return sphereCast(nullptr, solids, o, d, len, r, 0.0f); };
        RigPose pose;
        for (int i = 0; i < 30; ++i) pose = cam.update({0.0f, 1.5f, 0.0f}, 1.0f / 30.0f, false, ThirdPersonParams::soldier(), cast);
        record("T327", "Third person: W turns the body to the camera heading, aiming strafes, a wall pulls the spring arm in",
               "turn towards yaw 90 (aimYawDelta < 0), aiming: strafe 1 / no forward, camera stays in front of the wall (z > -1.4)",
               fmt("W: forward %.2f yaw delta %.1f, aim: strafe %.2f forward %.2f, arm %.2f m, eye z %.2f", w.forward, w.aimYawDelta, aim.strafe,
                   aim.forward, cam.arm(), pose.eye.z),
               w.forward > 0.0f && w.aimYawDelta < 0.0f && aim.strafe == 1.0f && aim.forward == 0.0f && pose.eye.z > -1.4f &&
                   cam.arm() < ThirdPersonParams::soldier().arm);
    }
}
