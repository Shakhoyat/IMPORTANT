// Ground vehicles: main battle tank (tracks, skid steer, turret, recoil, terrain, collision) and the
// anti-aircraft Gatling gun (spin-up, rate of fire, limits). Test IDs T80-T89.
#include <cmath>

#include "TestKit.h"
#include "objects/AaGun.h"
#include "objects/BaseLayout.h"
#include "objects/Tank.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Sim {
    float t = 0.0f;
    ts::Combat* combat = nullptr;
    const std::vector<ts::Box>* obstacles = nullptr;
    void step(ts::Controllable& c, const ts::ControlInput* in) {
        ts::WorldContext w;
        w.time = t; w.dt = kDt; w.combat = combat; w.obstacles = obstacles;
        c.update(w, in);
        t += kDt;
    }
    void run(ts::Controllable& c, const ts::ControlInput* in, float seconds) {
        const int n = static_cast<int>(seconds / kDt + 0.5f);
        for (int i = 0; i < n; ++i) step(c, in);
    }
};

int countNodes(const ts::SceneNode& n) {
    int c = n.isLeaf() ? 1 : 0;
    for (const auto& ch : n.children) c += countNodes(*ch);
    return c;
}

}  // namespace

TS_TEST_GROUP(Vehicles) {
    using namespace ts;

    // ---- T80: every track link lies on the closed track path ---------------------------------------------
    {
        Tank tank;
        Sim sim;
        ControlInput in;
        in.forward = 1.0f; in.turn = 0.3f;
        float worst = 0.0f;
        for (int i = 0; i < 300; ++i) {
            sim.step(tank, &in);
            for (int s = 0; s < 2; ++s)
                for (int k = 0; k < Tank::kLinksPerSide; ++k) {
                    const Vec3 p = tank.link(s, k)->local.translation;
                    worst = std::fmax(worst, tankpath::distance(p.z, p.y));
                }
        }
        record("T80", "all 80 track links stay on the track path while driving", "max distance < 0.01 m",
               fmt("max distance %.5f m, parts: %d (tracks 80)", worst, countNodes(*tank.root())), worst < 0.01f);
    }

    // ---- T81: links advance by speed * dt ------------------------------------------------------------------
    {
        Tank tank;
        Sim sim;
        ControlInput in;
        in.forward = 1.0f;
        sim.run(tank, &in, 3.0f);   // reach top speed
        const float p0 = tank.trackPhase(0);
        double integral = 0.0;
        const int n = 120;
        for (int i = 0; i < n; ++i) { sim.step(tank, &in); integral += tank.leftSpeed() * kDt; }
        const float adv = tank.trackPhase(0) - p0;
        const float P = tankpath::perimeter();
        float z, y, phi;
        tankpath::point(tank.trackPhase(0), z, y, phi);
        const Vec3 l0 = tank.link(0, 0)->local.translation;
        const float err = std::fabs(l0.z - z) + std::fabs(l0.y - y);
        record("T81", "track belt advances by speed*dt (link 0 sits at its arc-length position)",
               "advance == integral(speed dt) within 0.1 %, link 0 within 1 mm",
               fmt("advance %.4f m vs %.4f m (v=%.2f m/s, perimeter %.3f m), link error %.5f m", adv, integral, tank.leftSpeed(), P, err),
               std::fabs(adv - integral) < 0.001 * integral && err < 0.001f && tank.leftSpeed() > 7.9f);
    }

    // ---- T82: equal track speeds -> straight line ------------------------------------------------------------
    {
        Tank tank;
        Sim sim;
        tank.place(0.0f, -5.0f, 30.0f);
        ControlInput in;
        in.forward = 1.0f;
        const float yaw0 = tank.yawDeg();
        const Vec3 p0 = tank.position();
        sim.run(tank, &in, 3.0f);
        const Vec3 p1 = tank.position();
        const Vec3 d = p1 - p0;
        const float heading = std::atan2(d.x, d.z) * 180.0f / PI;
        record("T82", "skid steer: equal track speeds drive a straight line", "yaw unchanged, travel along the heading",
               fmt("yaw change %.4f deg, path bearing %.2f deg (heading 30), distance %.2f m", tank.yawDeg() - yaw0, heading, length(Vec3{d.x, 0, d.z})),
               std::fabs(tank.yawDeg() - yaw0) < 0.01f && std::fabs(heading - 30.0f) < 0.5f);
    }

    // ---- T83: opposite speeds -> yaw rate (vr - vl) / track width --------------------------------------------
    {
        Tank tank;
        Sim sim;
        tank.place(0.0f, -5.0f, 0.0f);
        ControlInput in;
        in.turn = 1.0f;
        sim.run(tank, &in, 2.0f);
        const Vec3 p0 = tank.position();
        const float yawA = tank.yawDeg();
        sim.run(tank, &in, 1.0f);
        float dyaw = tank.yawDeg() - yawA;
        if (dyaw < -180.0f) dyaw += 360.0f;
        const float expect = (tank.rightSpeed() - tank.leftSpeed()) / Tank::kTrackSeparation * 180.0f / PI;
        const Vec3 p1 = tank.position();
        record("T83", "skid steer: opposite track speeds pivot with yaw rate (vr - vl) / track width",
               "yaw rate within 2 % of the formula, no translation",
               fmt("%.2f deg/s vs %.2f deg/s (vl %.2f, vr %.2f), drift %.3f m", dyaw, expect, tank.leftSpeed(), tank.rightSpeed(), length(p1 - p0)),
               std::fabs(dyaw - expect) < 0.02f * std::fabs(expect) && length(Vec3{p1.x - p0.x, 0, p1.z - p0.z}) < 0.05f && expect < 0.0f);
    }

    // ---- T84: turret and gun slew rates and limits --------------------------------------------------------
    {
        Tank tank;
        Sim sim;
        tank.place(0.0f, -5.0f, 0.0f);
        ControlInput in;
        in.aimYaw = 1.0f;
        const float y0 = tank.turretYawWorldDeg();
        sim.run(tank, &in, 1.0f);
        float dy = y0 - tank.turretYawWorldDeg();
        in.aimYaw = 0.0f; in.aimPitch = 1.0f;
        sim.run(tank, &in, 1.0f);
        const float p1s = tank.gunPitchDeg();
        sim.run(tank, &in, 3.0f);
        const float pMax = tank.gunPitchDeg();
        in.aimPitch = -1.0f;
        sim.run(tank, &in, 3.0f);
        const float pMin = tank.gunPitchDeg();
        in.aimPitch = 0.0f; in.aimPitchDelta = 500.0f;
        sim.step(tank, &in);
        sim.step(tank, &in);
        const float pMouse = tank.gunPitchDeg();
        record("T84", "turret slews 40 deg/s, gun 20 deg/s, elevation limited to -8..+20 deg",
               "yaw 40+-2 deg in 1 s (right arrow = clockwise), pitch 20 deg/s, limits hold",
               fmt("yaw %.1f deg/s, pitch after 1 s %.1f, max %.2f, min %.2f, huge mouse delta (2 frames) -> %.2f, rate limited", dy, p1s, pMax, pMin, pMouse),
               std::fabs(dy - 40.0f) < 2.0f && std::fabs(p1s - 20.0f) < 2.0f && pMax <= 20.001f && pMax > 19.9f && pMin >= -8.001f &&
                   pMin < -7.9f && pMouse <= 20.001f && pMouse - pMin < 0.7f);
        // stabilisation: turn the hull, the turret's world yaw stays
        ControlInput turn;
        turn.turn = 1.0f;
        const float ty = tank.turretYawWorldDeg();
        sim.run(tank, &turn, 2.0f);
        record("T84b", "turret stays stabilised when the hull turns while possessed", "turret world yaw unchanged",
               fmt("hull yaw %.1f deg, turret world yaw %.3f -> %.3f", tank.yawDeg(), ty, tank.turretYawWorldDeg()),
               std::fabs(tank.turretYawWorldDeg() - ty) < 0.01f && std::fabs(tank.yawDeg()) > 20.0f);
    }

    // ---- T85: fire, recoil, reload ---------------------------------------------------------------------------
    {
        Tank tank;
        Combat combat;
        Sim sim;
        sim.combat = &combat;
        tank.place(0.0f, -5.0f, 0.0f);
        ControlInput fire;
        fire.firePressed = true;
        sim.step(tank, &fire);
        const size_t shells = combat.projectiles().size();
        float speed = shells ? length(combat.projectiles().back().velocity) : 0.0f;
        ControlInput idle;
        float peak = tank.recoil();
        for (int i = 0; i < 12; ++i) { sim.step(tank, &idle); peak = std::fmax(peak, tank.recoil()); }
        sim.run(tank, &idle, 0.7f);
        const float after = tank.recoil();
        sim.step(tank, &fire);   // too early (reload)
        const int roundsEarly = tank.rounds();
        sim.run(tank, &idle, 3.0f);
        const float reloadLeft = tank.reloadLeft();
        sim.step(tank, &fire);
        record("T85", "main gun: 120 m/s shell, 0.35 m recoil returning in 0.6 s, 3.5 s reload, 18 rounds",
               "1 shell at 120 m/s, recoil peak 0.35 -> 0, 2nd shot blocked early and allowed after 3.5 s",
               fmt("shells %zu at %.1f m/s, recoil peak %.3f, after %.4f, rounds after early press %d, reload left %.2f, rounds now %d",
                   shells, speed, peak, after, roundsEarly, reloadLeft, tank.rounds()),
               shells == 1 && std::fabs(speed - 120.0f) < 1.0f && std::fabs(peak - 0.35f) < 0.02f && after < 0.005f &&
                   roundsEarly == Tank::kStartRounds - 1 && tank.rounds() == Tank::kStartRounds - 2);
    }

    // ---- T86: hull height follows the terrain ------------------------------------------------------------------
    {
        Tank tank;
        Sim sim;
        float worst = 0.0f, maxSlope = 0.0f;
        const float xs[4] = {6.0f, -8.0f, 3.0f, -3.0f}, zs[4] = {-5.0f, 6.0f, 9.0f, -10.0f};
        for (int k = 0; k < 4; ++k) {
            tank.place(xs[k], zs[k], 20.0f + 50.0f * k);
            sim.run(tank, nullptr, 2.0f);
            const float yaw = tank.yawDeg() * PI / 180.0f;
            const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, l{std::cos(yaw), 0, -std::sin(yaw)};
            float sum = 0.0f;
            for (int a = -1; a <= 1; a += 2)
                for (int c = -1; c <= 1; c += 2) {
                    const Vec3 q = Vec3{xs[k], 0, zs[k]} + f * (2.6f * a) + l * (1.1f * c);
                    sum += terrainHeight(q.x, q.z);
                    maxSlope = std::fmax(maxSlope, std::fabs(terrainHeight(q.x, q.z) - terrainHeight(xs[k], zs[k])));
                }
            worst = std::fmax(worst, std::fabs(tank.position().y - sum * 0.25f));
        }
        record("T86", "hull height follows the terrain (mean of the 4 track corners)", "within 3 cm on 4 sites",
               fmt("worst error %.4f m (terrain relief under the hull up to %.2f m)", worst, maxSlope), worst < 0.03f);
    }

    // ---- T87: collision stops the tank, perimeter holds ---------------------------------------------------------
    {
        Tank tank;
        Sim sim;
        std::vector<Box> boxes{Box{{-3.0f, -1.0f, 8.0f}, {3.0f, 3.0f, 9.0f}}};
        sim.obstacles = &boxes;
        tank.place(0.0f, -3.0f, 0.0f);
        ControlInput in;
        in.forward = 1.0f;
        sim.run(tank, &in, 4.0f);
        const float frontZ = tank.position().z + 3.1f;
        const float vStop = tank.speed();
        Tank t2;
        Sim s2;
        t2.place(28.0f, 0.0f, 90.0f);
        s2.run(t2, &in, 6.0f);
        const Vec3 p = t2.position();
        const float r = std::sqrt(p.x * p.x + p.z * p.z);
        record("T87", "collision against an obstacle box stops the tank; perimeter limit r <= 31 m",
               "hull front <= wall face 8.0 m, speed 0; radius <= 31.01",
               fmt("front edge z %.3f m, speed %.2f m/s; perimeter run r = %.3f m", frontZ, vStop, r),
               frontZ <= 8.01f && frontZ > 7.0f && std::fabs(vStop) < 0.01f && r <= layout::kPerimeterRadius - 3.0f + 0.01f);
    }

    // ---- T88: AA gun spin-up and rate of fire -------------------------------------------------------------------------
    {
        AaGun gun;
        Sim sim;
        ControlInput fire;
        fire.fire = true;
        float tFirst = -1.0f;
        for (int i = 0; i < 120; ++i) {
            sim.step(gun, &fire);
            if (tFirst < 0.0f && gun.roundsFired() > 0) tFirst = (i + 1) * kDt;
        }
        const int c0 = gun.roundsFired();
        const float a0 = gun.spinAngleDeg();
        sim.run(gun, &fire, 2.0f);
        const int c1 = gun.roundsFired();
        ControlInput idle;
        const float spinFull = gun.spin();
        sim.run(gun, &idle, 0.75f);
        const float spinHalf = gun.spin();
        sim.run(gun, &idle, 1.5f);
        record("T88", "AA gun: 0.5 s spin-up, then 30 rounds/s, spin coasts down in 1.5 s",
               "first round at 0.5 s (+-0.05), 60 rounds in 2 s (+-2), spin 5 -> 2.5 -> 0 rev/s",
               fmt("first round at %.3f s, %d rounds in 2 s, spin %.2f -> %.2f -> %.2f rev/s (angle %.0f deg)", tFirst, c1 - c0, spinFull,
                   spinHalf, gun.spin(), a0),
               std::fabs(tFirst - 0.5f) < 0.05f && std::abs((c1 - c0) - 60) <= 2 && std::fabs(spinFull - 5.0f) < 0.01f &&
                   std::fabs(spinHalf - 2.5f) < 0.1f && gun.spin() < 0.01f);
    }

    // ---- T89: AA gun limits, slew, idle behaviour ---------------------------------------------------------------------
    {
        AaGun gun;
        Sim sim;
        ControlInput in;
        in.aimPitch = 1.0f;
        sim.run(gun, &in, 4.0f);
        const float hi = gun.pitchDeg();
        in.aimPitch = -1.0f;
        sim.run(gun, &in, 4.0f);
        const float lo = gun.pitchDeg();
        in.aimPitch = 0.0f; in.aimYaw = 1.0f;
        const float y0 = gun.yawDeg();
        sim.run(gun, &in, 1.0f);
        float dy = y0 - gun.yawDeg();
        if (dy < -180.0f) dy += 360.0f;
        if (dy > 180.0f) dy -= 360.0f;
        AaGun idleGun;
        sim.run(idleGun, nullptr, 10.0f);
        record("T89", "AA gun: elevation -5..+85 deg, 60 deg/s azimuth, idle sweep never fires",
               "pitch limits hold, 60+-3 deg/s, 0 rounds fired idle but it moved",
               fmt("pitch max %.2f min %.2f, azimuth %.1f deg/s, idle: moved %.1f deg, fired %d", hi, lo, dy,
                   std::fabs(idleGun.yawDeg() - 0.0f), idleGun.roundsFired()),
               hi <= 85.001f && hi > 84.9f && lo >= -5.001f && lo < -4.9f && std::fabs(dy - 60.0f) < 3.0f && idleGun.roundsFired() == 0);
    }
}
