// T120-T129: app layer without OpenGL: walk camera on the WalkSpace (ramp, closed / open blast door, ceiling, world
// radius), camera shake, director length / continuity / bunker walk path / scripted controls, possession key table.
#include <cmath>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/Camera.h"
#include "scene/Control.h"
#include "scene/Director.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

using namespace ts;
using ts::testkit::fmt;
using ts::testkit::record;

namespace {

// A stand-in for the bunker: entrance ramp (x -1.5..1.5, z 6..13), landing in front of the door, the door plane at
// z = 4, rooms behind it with a 2.6 m ceiling.
struct MiniBunker {
    WalkSpace ws;
    Box door;  // the closed blast door (a dynamic obstacle of the door controllable)
    MiniBunker() {
        using namespace layout;
        ws.addFloor({-kRampHalfWidth, kRampBottomZ, kRampHalfWidth, kRampTopZ, kBunkerFloorY, 0.0f, 2, 1e9f});
        ws.addFloor({-kRampHalfWidth, kBlastDoorZ - 1.0f, kRampHalfWidth, kRampBottomZ, kBunkerFloorY, kBunkerFloorY, 0, 1e9f});
        ws.addFloor({kBunkerMinX, kBunkerMinZ, kBunkerMaxX, kBlastDoorZ, kBunkerFloorY, kBunkerFloorY, 0, kBunkerCeilingY});
        const float wt = 0.35f;
        ws.addWall({{-kRampHalfWidth - wt, kBunkerFloorY, kRampBottomZ}, {-kRampHalfWidth, 0.5f, kRampTopZ}});
        ws.addWall({{kRampHalfWidth, kBunkerFloorY, kRampBottomZ}, {kRampHalfWidth + wt, 0.5f, kRampTopZ}});
        door = {{-kRampHalfWidth, kBunkerFloorY, kBlastDoorZ - 0.1f}, {kRampHalfWidth, kBunkerCeilingY, kBlastDoorZ + 0.1f}};
        ws.addIndoor({{kBunkerMinX, kBunkerFloorY, kBunkerMinZ}, {kBunkerMaxX, kBunkerCeilingY, kBlastDoorZ}});
    }
};

float rampY(float z) {
    using namespace layout;
    return kBunkerFloorY + (0.0f - kBunkerFloorY) * clampf((z - kRampBottomZ) / (kRampTopZ - kRampBottomZ), 0.0f, 1.0f);
}

struct FakeCtl : Controllable {
    std::string label;
    Vec3 pos;
    bool can = true;
    FakeCtl(std::string n, Vec3 p, bool possess = true) : label(std::move(n)), pos(p), can(possess) {}
    const char* name() const override { return label.c_str(); }
    const char* controls() const override { return ""; }
    SceneNode* root() override { return nullptr; }
    void update(const WorldContext&, const ControlInput*) override {}
    CameraHint camera(bool) const override { return {}; }
    std::string status() const override { return ""; }
    Vec3 position() const override { return pos; }
    bool possessable() const override { return can; }
};

}  // namespace

TS_TEST_GROUP(AppCamera) {
    using namespace layout;
    MiniBunker b;
    const float dt = 1.0f / 60.0f;

    // T120: walking down the ramp, the feet follow the sloping floor and the eye stays 1.7 m above it.
    Camera cam;
    cam.mode = CameraMode::Walk;
    cam.walkSpace = &b.ws;
    cam.placeWalk(0.0f, 12.5f);
    cam.walkYawDeg = 180.0f;
    float maxFeetDev = 0.0f, maxEyeDev = 0.0f;
    int steps = 0;
    while (cam.walkZ > kRampBottomZ + 0.4f && steps < 60 * 12) {
        cam.moveWalk(1.0f, 0.0f, dt, false);
        maxFeetDev = std::max(maxFeetDev, std::fabs(cam.walkFeetY - rampY(cam.walkZ)));
        maxEyeDev = std::max(maxEyeDev, std::fabs(cam.walkEyeY - (rampY(cam.walkZ) + Camera::kWalkEyeHeight)));
        ++steps;
    }
    record("T120", "Walk camera follows the entrance ramp floor going down (feet on the slope, eye 1.7 m above)",
           "feet within 0.05 m of the ramp profile, eye within 0.12 m of feet + 1.7, ends near the ramp bottom",
           fmt("max feet deviation %.3f m, max eye deviation %.3f m, z = %.2f m, feet y = %.2f m after %d steps", maxFeetDev,
               maxEyeDev, cam.walkZ, cam.walkFeetY, steps),
           maxFeetDev < 0.05f && maxEyeDev < 0.12f && cam.walkZ < kRampBottomZ + 0.5f);

    // T121: a closed door (dynamic obstacle) stops the walker; a wall bounds the ramp sides.
    cam.walkExtra = {b.door};
    for (int i = 0; i < 60 * 10; ++i) cam.moveWalk(1.0f, 0.0f, dt, true);
    const float stopZ = cam.walkZ;
    cam.placeWalk(0.5f, 8.0f, rampY(8.0f));  // strafe into the retaining walls (right of a -Z heading is +X)
    cam.walkYawDeg = 180.0f;
    float maxAbsX = 0.0f;
    for (int i = 0; i < 60 * 3; ++i) { cam.moveWalk(0.0f, 1.0f, dt, false); maxAbsX = std::max(maxAbsX, std::fabs(cam.walkX)); }
    for (int i = 0; i < 60 * 6; ++i) { cam.moveWalk(0.0f, -1.0f, dt, false); maxAbsX = std::max(maxAbsX, std::fabs(cam.walkX)); }
    record("T121", "Walk camera cannot pass a closed blast door box and stays inside the ramp walls",
           fmt("stops at z >= %.2f m (door plane %.1f + 0.1 + radius 0.3), |x| <= %.2f m", kBlastDoorZ + 0.1f + Camera::kWalkBodyRadius - 0.01f,
               kBlastDoorZ, kRampHalfWidth),
           fmt("stopped at z = %.3f m, max |x| %.3f m", stopZ, maxAbsX),
           stopZ >= kBlastDoorZ + 0.1f + Camera::kWalkBodyRadius - 0.02f && stopZ < kBlastDoorZ + 1.0f &&
               maxAbsX <= kRampHalfWidth + 0.01f);

    // T122: doors open (no extra solid): the walker reaches the CIC at the back of the rooms, eye under the ceiling.
    Camera c2;
    c2.mode = CameraMode::Walk;
    c2.walkSpace = &b.ws;
    c2.placeWalk(0.0f, 12.5f);
    c2.walkYawDeg = 180.0f;
    float minEyeGap = 1e9f;
    for (int i = 0; i < 60 * 20 && c2.walkZ > -6.0f; ++i) {
        c2.moveWalk(1.0f, 0.0f, dt, false);
        if (c2.walkZ < kBlastDoorZ) minEyeGap = std::min(minEyeGap, kBunkerCeilingY - c2.walkEyeY);
    }
    record("T122", "With the doors open the walker goes down the ramp, through the door and reaches the CIC",
           "z < -5.9 m, feet on the room floor (-3.4 m), eye below the 2.6 m ceiling",
           fmt("z = %.2f m, feet %.2f m, eye %.2f m, smallest ceiling gap %.2f m", c2.walkZ, c2.walkFeetY, c2.walkEyeY, minEyeGap),
           c2.walkZ < -5.9f && std::fabs(c2.walkFeetY - kBunkerFloorY) < 0.02f && minEyeGap > 0.1f);

    // T123: world radius clamp (52 m) and ceiling clamp of the eye.
    Camera c3;
    c3.mode = CameraMode::Walk;
    c3.placeWalk(40.0f, 0.0f);
    c3.walkYawDeg = 90.0f;
    for (int i = 0; i < 60 * 30; ++i) c3.moveWalk(1.0f, 0.0f, dt, true);
    const float r3 = std::sqrt(c3.walkX * c3.walkX + c3.walkZ * c3.walkZ);
    WalkSpace low;
    low.addFloor({-2, -2, 2, 2, 5.0f, 5.0f, 0, 5.0f + 1.5f});
    Camera c4;
    c4.mode = CameraMode::Walk;
    c4.walkSpace = &low;
    c4.placeWalk(0.0f, 0.0f, 5.0f);
    for (int i = 0; i < 30; ++i) c4.moveWalk(0.0f, 0.0f, dt, false);
    record("T123", "Walk camera is clamped to 52 m from the origin and the eye to 0.15 m under a low ceiling",
           "radius in [51, 52.01] m; eye <= ceiling - 0.15 = 6.35 m",
           fmt("radius %.2f m, eye %.3f m", r3, c4.walkEyeY), r3 <= 52.01f && r3 >= 51.0f && c4.walkEyeY <= 6.351f);

    // T124: camera shake is deterministic, zero at rest and at most ~1.2 degrees.
    CameraView v;
    v.view = lookAt({0, 2, 5}, {0, 1, 0}, {0, 1, 0});
    const CameraView v0 = v;
    CameraView s0 = v, s1 = v, s2 = v;
    applyShake(s0, 0.0f, 3.3f);
    applyShake(s1, 1.0f, 3.3f);
    applyShake(s2, 1.0f, 3.3f);
    float d0 = 0.0f, d1 = 0.0f, dd = 0.0f;
    for (int i = 0; i < 16; ++i) {
        if (i >= 12) continue;  // rotation part only
        d0 = std::max(d0, std::fabs(s0.view.m[i] - v0.view.m[i]));
        d1 = std::max(d1, std::fabs(s1.view.m[i] - v0.view.m[i]));
        dd = std::max(dd, std::fabs(s1.view.m[i] - s2.view.m[i]));
    }
    record("T124", "Camera shake: none at amount 0, small and repeatable at amount 1",
           "difference 0 at rest; 0 < max matrix change < 0.05 and identical on repeat",
           fmt("rest %.5f, shaken %.4f, repeat %.6f", d0, d1, dd), d0 == 0.0f && d1 > 1e-4f && d1 < 0.05f && dd == 0.0f);
}

TS_TEST_GROUP(AppDirector) {
    using namespace layout;
    Scene scene;
    scene.updateWorld();
    const float len = directorLength();
    const int n = directorShotCount();
    bool contiguous = true, finite = true, aboveGround = true;
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float start = directorShotStart(i);
        const float next = i + 1 < n ? directorShotStart(i + 1) : len;
        sum += next - start;
        const DirectorShot at = directorShot(scene, start + 0.001f), mid = directorShot(scene, 0.5f * (start + next)),
                           end = directorShot(scene, next - 0.001f);
        contiguous = contiguous && at.script == i && mid.script == i && end.script == i && at.fade < 0.05f && mid.fade > 0.99f &&
                     end.fade < 0.05f && next > start;
        for (const DirectorShot& s : {at, mid, end}) {
            finite = finite && std::isfinite(s.eye.x + s.eye.y + s.eye.z + s.target.x + s.target.y + s.target.z) && s.fovDeg > 5.0f;
            if (!s.uavCamera && !s.walk) aboveGround = aboveGround && s.eye.y >= terrainHeight(s.eye.x, s.eye.z) - 0.5f;
        }
    }
    record("T125", "Director: ~120 s, 12 shots, contiguous (every moment belongs to exactly one shot, fade dips at each cut)",
           "length 120 +- 0.5 s, shot durations sum to the length, finite cameras above the ground",
           fmt("length %.2f s, %d shots, sum %.2f s, contiguous %d, finite %d, above ground %d", len, n, sum, contiguous, finite, aboveGround),
           std::fabs(len - 120.0f) < 0.5f && n == 12 && std::fabs(sum - len) < 1e-3f && contiguous && finite && aboveGround);

    // T126: the bunker shot walks down the ramp, through the doors and into the CIC, eye always 1.7 m over the floor.
    MiniBunker b;
    float prevY = 1e9f, prevZ = 1e9f;
    bool monotone = true;
    float minY = 1e9f, endZ = 0.0f, maxStep = 0.0f;
    for (int i = 0; i <= 480; ++i) {
        const WalkPose p = directorWalkPose(&b.ws, static_cast<float>(i) / 480.0f);
        monotone = monotone && p.eye.z <= prevZ + 1e-4f;
        if (i > 0) maxStep = std::max(maxStep, std::fabs(p.eye.y - prevY));
        prevY = p.eye.y;
        prevZ = p.eye.z;
        minY = std::min(minY, p.eye.y);
        endZ = p.eye.z;
    }
    const WalkPose last = directorWalkPose(&b.ws, 1.0f);
    record("T126", "Director bunker shot: walk path descends the ramp, passes the door plane and ends in the rooms",
           "z decreases monotonically to < -5 m, eye ends at -3.4 + 1.7 = -1.7 m (+- 0.1), no vertical jump > 0.15 m per step",
           fmt("z end %.2f m, eye end %.2f m, lowest eye %.2f m, max vertical step %.3f m, monotone %d", endZ, last.eye.y, minY, maxStep, monotone),
           monotone && endZ < -5.0f && std::fabs(last.eye.y - (kBunkerFloorY + 1.7f)) < 0.1f && maxStep < 0.15f);

    // T129: scripted controls follow the shot list.
    const auto slotAt = [](float t) { return directorScript(t).slot; };
    const DirectorScript heli = directorScript(25.0f), tank = directorScript(36.0f), aa = directorScript(50.0f),
                         doors = directorScript(12.6f), none = directorScript(70.0f), strike = directorScript(81.2f);
    record("T129", "Director scripts: doors opened early, helicopter lifts, tank drives, AA fires, UAV strike at 81 s, nobody during the walk",
           "slot 4 @12.6 (action), slot 1 @25 (lift 1), slot 0 @36 (forward 1), slot 2 @50 (fire), slot -1 @70, strike @81.2",
           fmt("slots %d %d %d %d %d, lift %.1f, forward %.1f, fire %d, strike %d, doors action %d", slotAt(12.6f), slotAt(25.0f), slotAt(36.0f),
               slotAt(50.0f), slotAt(70.0f), heli.in.lift, tank.in.forward, aa.in.fire, strike.strike, doors.in.actionPressed),
           doors.slot == static_cast<int>(Slot::Doors) && doors.in.actionPressed && heli.slot == static_cast<int>(Slot::Helicopter) &&
               heli.in.lift > 0.5f && tank.slot == static_cast<int>(Slot::Tank) && tank.in.forward > 0.5f &&
               aa.slot == static_cast<int>(Slot::AaGun) && aa.in.fire && none.slot == -1 && strike.strike);
}

TS_TEST_GROUP(AppControl) {
    // T127: key table 5 6 7 8 9 0 -> Tank, Helicopter, AA gun, Radar, Blast doors, UAV; resolved by name, any order.
    const char keys[] = {'5', '6', '7', '8', '9', '0'};
    const Slot expect[] = {Slot::Tank, Slot::Helicopter, Slot::AaGun, Slot::Radar, Slot::Doors, Slot::Uav};
    bool keyOk = true;
    for (int i = 0; i < 6; ++i) {
        Slot s;
        keyOk = keyOk && slotForKey(keys[i], s) && s == expect[i];
    }
    Slot dummy;
    keyOk = keyOk && !slotForKey('1', dummy) && !slotForKey('4', dummy) && !slotForKey('a', dummy);

    FakeCtl flag("FLAGPOLE FLAG", {-4.5f, 0, 27.0f}, false), uav("UAV-1 MANUAL DRONE", {0, 12, 0}), doors("BLAST DOORS", {0, -2, 4}),
        radar("RADAR AN/TPS", {29, 0, -3}), aa("AA GUN 2x35", {21, 0, -19}), heli("UH HELICOPTER", {20, 0, 17}),
        tank("MBT-1 TANK", {-22, 0, -16}), barrier("GATE BARRIER", {0, 0, 33});
    std::vector<Controllable*> list = {&flag, &tank, &heli, &aa, &radar, &doors, &barrier, &uav};  // deliberately shuffled
    bool resolveOk = true;
    for (int i = 0; i < 6; ++i) {
        const int idx = resolveSlot(list, expect[i]);
        resolveOk = resolveOk && idx >= 0 && slotOfName(list[idx]->name()) == static_cast<int>(expect[i]);
    }
    resolveOk = resolveOk && resolveSlot(list, Slot::Tank) == 1 && resolveSlot(list, Slot::Uav) == 7;
    // Documented order as the fallback when nothing is named.
    FakeCtl a("A", {}), bb("B", {}), c("C", {});
    std::vector<Controllable*> anon = {&a, &bb, &c};
    const bool fallbackOk = resolveSlot(anon, Slot::Helicopter) == 1 && resolveSlot(anon, Slot::Radar) == -1;
    record("T127", "Possession keys 5 6 7 8 9 0 map to tank, helicopter, AA gun, radar, blast doors, UAV (found by name)",
           "table exact, other keys rejected, names resolved in a shuffled list, order fallback for unnamed objects",
           fmt("keys %d, names %d, fallback %d", keyOk, resolveOk, fallbackOk), keyOk && resolveOk && fallbackOk);

    // T128: [ ] cycle skips the non-possessable flag and wraps; walk interaction finds the nearest door within 3.5 m.
    int cur = -1;
    std::vector<int> visited;
    for (int i = 0; i < 8; ++i) {
        cur = cyclePossessable(list, cur, +1);
        visited.push_back(cur);
    }
    const bool cycleOk = visited[0] == 1 && visited[6] == 7 && visited[7] == 1 &&
                         cyclePossessable(list, 1, -1) == 7 && cyclePossessable({}, -1, 1) == -1;
    const int nearDoor = nearestToggle(list, {0.5f, -2.0f, 6.0f});       // 2.1 m from the blast doors
    const int nearGate = nearestToggle(list, {1.0f, 0.0f, 31.0f});       // 2.2 m from the barrier
    const int farAway = nearestToggle(list, {10.0f, 0.0f, 10.0f});
    record("T128", "[ ] cycle all possessable controllables (flag skipped, wraps); walk interaction picks the door / barrier within 3.5 m",
           "cycle 1..7 then wraps to 1; blast doors at 2.1 m (index 5), barrier at 2.2 m (index 6), nothing at 10 m",
           fmt("first %d last %d wrap %d, door %d, barrier %d, far %d", visited[0], visited[6], visited[7], nearDoor, nearGate, farAway),
           cycleOk && nearDoor == 5 && nearGate == 6 && farAway == -1);
}
