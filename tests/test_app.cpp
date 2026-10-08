// T120-T129: app layer without OpenGL: walk camera on the WalkSpace (ramp, closed / open blast door, ceiling, world
// radius), camera shake, director length / continuity / bunker walk path / scripted controls, possession key table.
// T237-T239 (v6): --time / --seed helpers and the HUD clock, the v6 director's forced clock, phase banners / PERS boxes /
// key table.
#include <cmath>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/Camera.h"
#include "scene/Control.h"
#include "scene/DayCycle.h"
#include "scene/Director.h"
#include "scene/HudMath.h"
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
    // The v6 cut (video, K) and the v5 cut (kept for the report captures) are both checked.
    const auto checkCut = [&](DirectorCut cut, float& len, int& n, float& sum, bool& contiguous, bool& finite, bool& aboveGround) {
        len = directorLength(cut);
        n = directorShotCount(cut);
        contiguous = finite = aboveGround = true;
        sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float start = directorShotStart(i, cut);
            const float next = i + 1 < n ? directorShotStart(i + 1, cut) : len;
            sum += next - start;
            const DirectorShot at = directorShot(scene, start + 0.001f, cut), mid = directorShot(scene, 0.5f * (start + next), cut),
                               end = directorShot(scene, next - 0.001f, cut);
            contiguous = contiguous && at.script == i && mid.script == i && end.script == i && at.fade < 0.05f && mid.fade > 0.99f &&
                         end.fade < 0.05f && next > start;
            for (const DirectorShot& s : {at, mid, end}) {
                finite = finite && std::isfinite(s.eye.x + s.eye.y + s.eye.z + s.target.x + s.target.y + s.target.z) && s.fovDeg > 5.0f;
                if (!s.uavCamera && !s.walk) aboveGround = aboveGround && s.eye.y >= terrainHeight(s.eye.x, s.eye.z) - 0.5f;
            }
        }
    };
    float len, sum, len5, sum5;
    int n, n5;
    bool contiguous, finite, aboveGround, contiguous5, finite5, aboveGround5;
    checkCut(DirectorCut::V6, len, n, sum, contiguous, finite, aboveGround);
    checkCut(DirectorCut::V5, len5, n5, sum5, contiguous5, finite5, aboveGround5);
    record("T125", "Director: v6 cut 120 s / 15 shots and v5 cut 120 s / 13 shots, contiguous (every moment belongs to exactly one shot, fade dips at each cut)",
           "both lengths 120 +- 0.5 s, shot durations sum to the length, finite cameras above the ground",
           fmt("v6: length %.2f s, %d shots, sum %.2f s, contiguous %d, finite %d, above ground %d; v5: %.2f s, %d shots, ok %d", len, n, sum,
               contiguous, finite, aboveGround, len5, n5, contiguous5 && finite5 && aboveGround5),
           std::fabs(len - 120.0f) < 0.5f && n == 15 && std::fabs(sum - len) < 1e-3f && contiguous && finite && aboveGround &&
               std::fabs(len5 - 120.0f) < 0.5f && n5 == 13 && std::fabs(sum5 - len5) < 1e-3f && contiguous5 && finite5 && aboveGround5);

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

    // T129: scripted controls follow the shot list. v5 cut: doors, helicopter, tank, AA gun. v6 cut: helicopter in its
    // shot, doors opened at dusk, nobody possessed in the night battle (the squad fights by itself: no scripted strike
    // exists any more, DirectorScript has no such field).
    const auto slot5 = [](float t) { return directorScript(t, DirectorCut::V5).slot; };
    const DirectorScript heli = directorScript(22.0f, DirectorCut::V5), tank = directorScript(32.0f, DirectorCut::V5),
                         aa = directorScript(43.0f, DirectorCut::V5), doors = directorScript(10.6f, DirectorCut::V5),
                         none = directorScript(70.0f, DirectorCut::V5);
    const bool v5ok = doors.slot == static_cast<int>(Slot::Doors) && doors.in.actionPressed && heli.slot == static_cast<int>(Slot::Helicopter) &&
                      heli.in.lift > 0.5f && tank.slot == static_cast<int>(Slot::Tank) && tank.in.forward > 0.5f &&
                      aa.slot == static_cast<int>(Slot::AaGun) && aa.in.fire && none.slot == -1 && slot5(70.0f) == -1;
    const DirectorScript heli6 = directorScript(directorShotStart(kV6Heli) + 5.0f), doors6 = directorScript(directorShotStart(kV6Dusk) + 0.2f);
    bool nightFree = true;
    for (float t = 0.0f; t < directorShotStart(kV6Dawn); t += 0.1f) nightFree = nightFree && directorScript(t).slot == -1;
    record("T129", "Director scripts: v5 cut unchanged (doors, helicopter, tank, AA); v6 cut: helicopter lifts in its shot, doors open at dusk, nobody possessed during the night battle",
           "v5: slot 4 @10.6 (action), 1 @22 (lift), 0 @32 (forward), 2 @43 (fire), -1 @70; v6: helicopter lift 1, doors action, slot -1 for t < dawn",
           fmt("v5 ok %d (slots %d %d %d %d %d), v6 heli slot %d lift %.1f, doors slot %d action %d, night free %d", v5ok, slot5(10.6f), slot5(22.0f),
               slot5(32.0f), slot5(43.0f), slot5(70.0f), heli6.slot, heli6.in.lift, doors6.slot, doors6.in.actionPressed, nightFree),
           v5ok && heli6.slot == static_cast<int>(Slot::Helicopter) && heli6.in.lift > 0.5f && doors6.slot == static_cast<int>(Slot::Doors) &&
               doors6.in.actionPressed && nightFree);

    // T177: the v5 game shots carry the game HUD, the bunker walk does not; the live-action cameras stay finite and above ground.
    Scene live;
    live.setMissionEnabled(true);
    for (int i = 0; i < 360; ++i) live.update(1.0f / 60.0f, Detection{});
    live.updateWorld();
    bool hudOk = true, finiteLive = true;
    for (int i = 0; i < directorShotCount(); ++i) {
        const DirectorShot s = directorShot(live, directorShotStart(i) + 0.5f);
        const bool game = i <= kV6Dawn || i == kV6DayThermal || i == kV6Dusk || i == kV6NightLights;
        hudOk = hudOk && (s.gameHud == game) && (!s.walk || !s.gameHud);
        finiteLive = finiteLive && std::isfinite(s.eye.x + s.eye.y + s.eye.z + s.target.x + s.target.z);
    }
    record("T177", "Director v6: the night battle, dawn, day thermal, dusk and night shots draw the game HUD (banners); the day camp / soldier / bunker shots do not",
           "gameHud on shots 0-5, 11, 12, 14 only; every camera finite on a running game",
           fmt("hud flags ok %d, finite %d, shots %d", hudOk, finiteLive, directorShotCount()), hudOk && finiteLive);
}

TS_TEST_GROUP(GameHud) {
    const int W = 1600, H = 900;
    const Vec3 eye{0, 5, 20}, tgt{0, 0, 0};
    const Mat4 view = lookAt(eye, tgt, {0, 1, 0});
    const Mat4 proj = perspective(radians(50.0f), static_cast<float>(W) / H, 0.05f, 600.0f);
    const Mat4 vp = proj * view;
    // T170: marker projection.
    const MarkerProjection mid = projectMarker(vp, eye, tgt, W, H);
    const MarkerProjection behind = projectMarker(vp, eye, {0, 5, 60}, W, H);
    const MarkerProjection side = projectMarker(vp, eye, {40, 0, 0}, W, H);
    record("T170", "Marker projection: the look-at point lands on the screen centre, a point behind the camera is not in front, a far side point is off-screen",
           "centre +-1 px, dist = |eye - target|, behind.front false, side front and not on screen",
           fmt("centre (%.1f, %.1f), dist %.2f, behind front %d, side front %d onScreen %d", mid.x, mid.y, mid.dist, behind.front, side.front, side.onScreen),
           std::fabs(mid.x - W / 2) < 1.0f && std::fabs(mid.y - H / 2) < 1.0f && std::fabs(mid.dist - length(eye - tgt)) < 1e-3f && mid.onScreen &&
               !behind.front && !behind.onScreen && side.front && !side.onScreen);
    // T171: bracket size.
    const float near = markerHalfSize(5.0f, H), far = markerHalfSize(60.0f, H), far2 = markerHalfSize(60.0f, 2 * H);
    record("T171", "Marker bracket size shrinks with distance, is clamped to [10, 120] px at 900p and scales with the screen height",
           "near > far, 10 <= far, near <= 120, double height doubles the clamp",
           fmt("5 m %.1f px, 60 m %.1f px, 60 m at 1800p %.1f px", near, far, far2),
           near > far && far >= 10.0f - 1e-3f && near <= 120.0f + 1e-3f && std::fabs(far2 - 2.0f * far) < 1e-3f);
    // T172: edge arrow clamp.
    bool inside = true;
    float tipX = 0, tipY = 0;
    for (int k = 0; k < 72; ++k) {
        const float a = radians(5.0f * static_cast<float>(k));
        const EdgeArrow e = edgeArrow(W / 2 + 3000 * std::cos(a), H / 2 + 3000 * std::sin(a), true, W, H, 30.0f);
        inside = inside && e.x >= 30.0f - 1e-2f && e.x <= W - 30.0f + 1e-2f && e.y >= 30.0f - 1e-2f && e.y <= H - 30.0f + 1e-2f &&
                 std::fabs(e.dirX * e.dirX + e.dirY * e.dirY - 1.0f) < 1e-3f;
    }
    const EdgeArrow right = edgeArrow(W + 500.0f, H / 2.0f, true, W, H, 30.0f);
    tipX = right.x;
    tipY = right.y;
    const EdgeArrow back = edgeArrow(W / 2 + 200.0f, H / 2.0f, false, W, H, 30.0f);  // mirrored: really to the left
    record("T172", "Edge arrow: always inside the margin rectangle, points outwards; a point to the right sits on the right edge; behind-camera points flip",
           "tip within [30, W-30] x [30, H-30]; right case (1570, 450); behind-right points left (dirX < 0)",
           fmt("all inside %d, right tip (%.0f, %.0f), behind dir (%.2f, %.2f)", inside, tipX, tipY, back.dirX, back.dirY),
           inside && std::fabs(tipX - (W - 30.0f)) < 1.0f && std::fabs(tipY - H / 2.0f) < 1.0f && back.dirX < -0.9f);
    // T173: banner timing.
    const float d = bannerDuration(BannerKind::Wave);
    const float a0 = bannerAlpha(0.0f, d), a1 = bannerAlpha(d * 0.5f, d), a2 = bannerAlpha(d - 0.25f, d), a3 = bannerAlpha(d + 0.1f, d);
    bool monoOut = true;
    for (float t = d - 0.5f; t < d - 0.01f; t += 0.02f) monoOut = monoOut && bannerAlpha(t + 0.02f, d) <= bannerAlpha(t, d) + 1e-5f;
    record("T173", "Banner timing: wave banner lasts 2.5 s, fades in from 0, holds at 1, fades out monotonically to 0; score pop-ups rise 0 -> 1",
           "duration 2.5, alpha(0) 0, alpha(mid) 1, alpha(end - 0.25) 0.5, alpha(after) 0, rise(0) 0 and rise(end) 1",
           fmt("duration %.2f, alpha %.2f %.2f %.2f %.2f, rise %.2f -> %.2f", d, a0, a1, a2, a3, bannerRise(0, 2.2f), bannerRise(2.2f, 2.2f)),
           std::fabs(d - 2.5f) < 1e-4f && a0 < 1e-4f && std::fabs(a1 - 1.0f) < 1e-4f && std::fabs(a2 - 0.5f) < 1e-3f && a3 == 0.0f && monoOut &&
               bannerRise(0, 2.2f) < 1e-4f && std::fabs(bannerRise(2.2f, 2.2f) - 1.0f) < 1e-4f);
    // T174: events -> banners.
    HudEvents ev;
    GameEvent wave, spot, kill, hit, breach, lost;
    wave.type = GameEventType::WaveStart; wave.value = 3; wave.time = 10.0f;
    spot.type = GameEventType::EnemySpotted; spot.track = 4; spot.time = 10.5f;
    kill.type = GameEventType::EnemyDestroyed; kill.value = 150; kill.combined = true; kill.time = 11.0f;
    hit.type = GameEventType::EnemyHit; hit.value = 60; hit.time = 11.0f;
    breach.type = GameEventType::Breach; breach.value = 10; breach.time = 12.0f;
    lost.type = GameEventType::BaseLost; lost.time = 14.0f;
    ev.consume({wave, spot, spot, kill, hit}, 11.0f);
    const size_t n1 = ev.banners().size();
    const Banner* kb = ev.latest(BannerKind::Kill);
    const bool killOk = kb && kb->value == 150 && kb->combined && kb->hasPosition;
    ev.prune(11.4f);  // the hit marker (0.35 s) is gone
    const bool hitGone = ev.latest(BannerKind::Hit) == nullptr && ev.latest(BannerKind::Wave) != nullptr;
    ev.prune(12.6f);  // wave (2.5 s) over, spotted (1.8 s) over, kill (2.2 s) still on
    const bool waveGone = ev.latest(BannerKind::Wave) == nullptr && ev.latest(BannerKind::Spotted) == nullptr && ev.latest(BannerKind::Kill) != nullptr;
    ev.consume({breach}, 12.0f);
    const float flash0 = ev.breachFlash(12.0f), flash1 = ev.breachFlash(12.5f), flash2 = ev.breachFlash(13.2f);
    ev.consume({lost}, 14.0f);
    const bool lostText = std::string(ev.baseLostText(14.5f) ? ev.baseLostText(14.5f) : "") == "BASE LOST" &&
                          std::string(ev.baseLostText(17.0f) ? ev.baseLostText(17.0f) : "") == "RESTARTING" && ev.baseLostText(20.0f) == nullptr;
    const float dim = ev.baseLostDim(16.5f);
    ev.prune(1.0f);  // the clock jumped back: a new scene
    record("T174", "Event queue: wave / spotted (replaced, not stacked) / kill (+150 combined) / hit become banners that expire on time; breach flash decays; BASE LOST then RESTARTING; a clock reset clears all",
           "4 banners after 5 events (duplicate spotted merged), hit gone after 0.4 s, wave + spotted gone at 2.6 s, flash 1 -> ~0.4 -> 0, text phases, dim 0..0.65, cleared on reset",
           fmt("%zu banners, kill ok %d, hit gone %d, wave gone %d, flash %.2f %.2f %.2f, text %d, dim %.2f, after reset %zu", n1, killOk, hitGone, waveGone,
               flash0, flash1, flash2, lostText, dim, ev.banners().size()),
           n1 == 4 && killOk && hitGone && waveGone && flash0 > 0.99f && flash1 > 0.3f && flash1 < 0.5f && flash2 == 0.0f && lostText && dim > 0.3f &&
               dim <= 0.65f && ev.banners().empty());
    // T175: mini-map mapping.
    const MiniMapGeom g = miniMapGeometry(1600, 900);
    float sx, sy;
    const bool inC = miniMapToScreen(g, 0, 0, sx, sy);
    const float cx0 = sx, cy0 = sy;
    miniMapToScreen(g, g.worldRadius, 0, sx, sy);
    const float rx = sx - cx0, ry = sy - cy0;
    miniMapToScreen(g, 0, 10.0f, sx, sy);
    const float dz = sy - cy0;
    const bool outside = miniMapToScreen(g, 100.0f, 100.0f, sx, sy);
    const float rim = std::sqrt((sx - cx0) * (sx - cx0) + (sy - cy0) * (sy - cy0));
    const MiniMapGeom g2 = miniMapGeometry(1600, 1800);
    record("T175", "Mini-map: 180 px across at 900p (360 at 1800p), bottom-right; world +x right, +z down; the rim is at the world radius; far points are pulled onto the rim",
           "diameter 180, centre inside the screen, (R, 0) -> +radius px, +10 m z -> down by radius/4, (100,100) clamped to the rim and reported outside",
           fmt("diameter %.0f / %.0f, centre (%.0f, %.0f), rim dx %.1f dy %.1f, z10 dy %.1f, clamped rim %.1f outside %d", 2 * g.radius, 2 * g2.radius, cx0, cy0, rx, ry,
               dz, rim, !outside),
           std::fabs(2 * g.radius - 180.0f) < 0.01f && std::fabs(2 * g2.radius - 360.0f) < 0.01f && inC && cx0 > 800 && cy0 > 450 && cx0 + g.radius <= 1600 &&
               cy0 + g.radius <= 900 && std::fabs(rx - g.radius) < 0.01f && std::fabs(ry) < 0.01f && std::fabs(dz - g.radius / g.worldRadius * 10.0f) < 0.01f &&
               !outside && std::fabs(rim - g.radius) < 0.01f);
    // T176: colours and headings.
    float r1, g1, b1, r2, g2c, b2, r3, g3, b3;
    integrityColor(100.0f, r1, g1, b1);
    integrityColor(50.0f, r2, g2c, b2);
    integrityColor(0.0f, r3, g3, b3);
    float hx, hy;
    miniMapHeading(0.0f, hx, hy);
    const bool head0 = std::fabs(hx) < 1e-5f && std::fabs(hy - 1.0f) < 1e-5f;
    miniMapHeading(1.5707963f, hx, hy);
    record("T176", "Integrity bar colour: green at 100 %, amber at 50 %, red at 0 %; map heading 0 = +z (down the screen), 90 deg = +x",
           "g > r at 100, r > 0.9 and g > 0.7 at 50, r > 0.9 and g < 0.3 at 0",
           fmt("100%%: (%.2f %.2f %.2f), 50%%: (%.2f %.2f %.2f), 0%%: (%.2f %.2f %.2f), heading ok %d", r1, g1, b1, r2, g2c, b2, r3, g3, b3, head0),
           g1 > r1 && r2 > 0.9f && g2c > 0.7f && r3 > 0.9f && g3 < 0.3f && head0 && std::fabs(hx - 1.0f) < 1e-5f);
    // T178: a running mission feeds the HUD queue; the director-start announcement (App::step) re-arms the running wave's banner.
    Scene sc;
    HudEvents hudEv;
    bool consistent = true;
    for (int i = 0; i < 1200; ++i) {
        sc.update(1.0f / 60.0f, Detection{});
        hudEv.consume(sc.events(), sc.time());
        for (const Banner& b : hudEv.banners()) consistent = consistent && b.start <= sc.time() + 1e-3f && sc.time() - b.start <= b.duration + 1e-3f;
    }
    GameEvent announce;
    announce.type = GameEventType::WaveStart;
    announce.value = sc.mission().state().wave;
    announce.time = sc.time();
    hudEv.consume({announce}, sc.time());
    const Banner* wb = hudEv.latest(BannerKind::Wave);
    record("T178", "A running mission keeps the HUD queue consistent (no expired or future banners over 20 s); the director-start announcement shows the running wave",
           "all banners within their lifetime, wave >= 1, announced wave banner carries the mission's wave number",
           fmt("consistent %d, mission wave %d, banner wave %d", consistent, sc.mission().state().wave, wb ? wb->value : -1),
           consistent && sc.mission().state().wave >= 1 && wb && wb->value == sc.mission().state().wave);
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

TS_TEST_GROUP(AppV6) {
    // T237: --time / --seed parsing and the HUD clock helpers.
    struct Case { const char* text; bool ok; float hour; bool autoCycle; };
    const Case cases[] = {{"day", true, 12.0f, false},   {"night", true, 23.0f, false}, {"dusk", true, 17.75f, false},
                          {"dawn", true, 6.0f, false},   {"cycle", true, -1.0f, true},  {"06:30", true, 6.5f, false},
                          {"23:59", true, 23.0f + 59.0f / 60.0f, false}, {"24:00", true, 0.0f, false}, {"7", true, 7.0f, false},
                          {"25:00", false, 0, false},    {"12:60", false, 0, false},    {"noonish", false, 0, false},
                          {"12:30x", false, 0, false},   {"", false, 0, false}};
    bool parseOk = true;
    for (const Case& c : cases) {
        float h = -7.0f;
        bool a = false;
        const bool ok = parseTimeOfDay(c.text, h, a);
        parseOk = parseOk && ok == c.ok && (!ok || (std::fabs(h - c.hour) < 1e-4f && a == c.autoCycle));
    }
    const bool clockOk = formatClock(0.0f) == "00:00" && formatClock(17.75f) == "17:45" && formatClock(23.999f) == "23:59" &&
                         formatClock(24.5f) == "00:30" && formatClock(-1.0f) == "23:00";
    const uint32_t s1 = mixRunSeed(12345u, 1000u), s2 = mixRunSeed(12345u, 1001u), s3 = mixRunSeed(12345u, 1000u);
    const bool seedOk = s1 != 0u && s1 != s2 && s1 == s3 && mixRunSeed(0u, 0u) != 0u;
    const bool glyphOk = skyGlyph(30.0f) == SkyGlyph::Sun && skyGlyph(2.0f) == SkyGlyph::LowSun && skyGlyph(-5.0f) == SkyGlyph::LowSun &&
                         skyGlyph(-20.0f) == SkyGlyph::Moon;
    record("T237", "CLI helpers: --time accepts day / night / dusk / dawn / cycle / HH:MM and rejects bad values; HUD clock text; run seed never 0, differs per clock tick; sun / low sun / moon glyph",
           "14 parse cases as specified, 00:00 17:45 23:59 00:30 23:00, seeds non-zero / repeatable / distinct, glyph thresholds +-6 deg",
           fmt("parse %d, clock %d, seeds %u %u (repeat %d), glyph %d", parseOk, clockOk, s1, s2, s1 == s3, glyphOk),
           parseOk && clockOk && seedOk && glyphOk);

    // T238: the v6 director owns the clock: every moment forces an hour; the battle is at night (waves allowed), the
    // dawn shot crosses the -6 deg line upwards, the camp shots are in daylight, the dusk shot crosses sunset, the last
    // shot is night again; inside a shot the clock never jumps.
    const DayCycleConfig dc;
    const auto elev = [&](float hour) { return DayCycle::sunElevationDeg(hour, dc); };
    bool allForced = true, smooth = true;
    float prevHour = -1.0f;
    int prevShot = -1;
    Scene scene;
    scene.updateWorld();
    for (float t = 0.0f; t < directorLength(); t += 0.05f) {
        const DirectorScript ds = directorScript(t);
        const int shot = directorShot(scene, t).script;
        allForced = allForced && ds.hour >= 0.0f && ds.hour < 24.0f;
        if (shot == prevShot && prevHour >= 0.0f) smooth = smooth && std::fabs(ds.hour - prevHour) < 0.05f;
        prevHour = ds.hour;
        prevShot = shot;
    }
    const auto hourAt = [](int shot, float u) {
        return directorScript(directorShotStart(shot) + u * (directorShotStart(shot + 1) - directorShotStart(shot))).hour;
    };
    bool battleNight = true;
    for (int i = kV6NightWave; i <= kV6Kill; ++i) battleNight = battleNight && elev(hourAt(i, 0.5f)) < -6.0f;
    const float dawn0 = elev(hourAt(kV6Dawn, 0.02f)), dawn1 = elev(hourAt(kV6Dawn, 0.98f));
    bool campDay = true;
    for (int i = kV6DayFlyover; i <= kV6DayThermal; ++i) campDay = campDay && elev(hourAt(i, 0.5f)) > 10.0f;
    const float dusk0 = elev(hourAt(kV6Dusk, 0.02f)), dusk1 = elev(hourAt(kV6Dusk, 0.98f));
    const float lastEl = elev(directorScript(directorLength() - 0.5f).hour);
    record("T238", "v6 director clock: forced at every moment, continuous inside shots; night battle, dawn time-lapse through -6 deg, day camp shots, dusk through sunset, night finale",
           "hour in [0, 24) always, < 0.05 h per 0.05 s inside a shot; shots 0-4 below -6 deg; dawn starts < -6 and ends > +4; shots 6-11 above 10 deg; dusk from > 0 to < 0; last below -6",
           fmt("forced %d, smooth %d, battle night %d, dawn %.1f -> %.1f deg, camp day %d, dusk %.1f -> %.1f deg, finale %.1f deg", allForced, smooth,
               battleNight, dawn0, dawn1, campDay, dusk0, dusk1, lastEl),
           allForced && smooth && battleNight && dawn0 < -6.0f && dawn1 > 4.0f && campDay && dusk0 > 0.0f && dusk1 < 0.0f && lastEl < -6.0f);

    // T239: phase events -> one phase banner at a time (a scrubbed clock replaces it), withdrawals merge into one counted
    // banner; the CV pixel box maps onto the first-person view (identity at the same field of view); the F1 table lists
    // the v6 keys.
    HudEvents ev;
    GameEvent dusk, night, dawn, wd1, wd2;
    dusk.type = GameEventType::Dusk; dusk.time = 5.0f;
    night.type = GameEventType::NightFalls; night.time = 5.5f;
    dawn.type = GameEventType::Dawn; dawn.time = 6.0f;
    wd1.type = GameEventType::Withdraw; wd1.time = 6.1f;
    wd2.type = GameEventType::Withdraw; wd2.time = 6.2f;
    ev.consume({dusk}, 5.0f);
    const bool duskOn = ev.latest(BannerKind::Dusk) != nullptr && std::string(phaseBannerText(BannerKind::Dusk)) == "DUSK";
    ev.consume({night}, 5.5f);
    const bool replaced = ev.latest(BannerKind::Dusk) == nullptr && ev.latest(BannerKind::NightFalls) != nullptr;
    GameEvent shift;
    shift.type = GameEventType::ShiftChange; shift.time = 5.5f; shift.value = 3;
    ev.consume({shift}, 5.5f);
    const bool shiftSeparate = ev.latest(BannerKind::Shift) != nullptr && ev.latest(BannerKind::NightFalls) != nullptr;
        ev.consume({dawn, wd1}, 6.1f);
    ev.consume({wd2}, 6.2f);
    int withdrawBanners = 0;
    for (const Banner& b : ev.banners()) withdrawBanners += b.kind == BannerKind::Withdraw;
    const Banner* wb = ev.latest(BannerKind::Withdraw);
    const bool merged = withdrawBanners == 1 && wb && wb->value == 2 && ev.latest(BannerKind::Dawn) != nullptr;
    const bool texts = phaseBannerText(BannerKind::Wave) == nullptr && phaseBannerDetail(BannerKind::NightFalls) != nullptr &&
                       bannerDuration(BannerKind::Dawn) > 3.0f;
    const ScreenBox full = cvBoxToScreen(0, 0, 319, 239, 320, 240, 40.0f, 40.0f, 1600, 1200);
    const ScreenBox zoom = cvBoxToScreen(150, 110, 169, 129, 320, 240, 40.0f, 20.0f, 1600, 1200);
    const ScreenBox wide = cvBoxToScreen(0, 0, 319, 239, 320, 240, 40.0f, 40.0f, 1920, 1080);
    const bool mapOk = std::fabs(full.x0) < 0.01f && std::fabs(full.x1 - 1600.0f) < 0.01f && std::fabs(full.y0) < 0.01f &&
                       std::fabs(full.y1 - 1200.0f) < 0.01f && zoom.x1 - zoom.x0 > 2.0f * (20.0f / 320.0f * 1600.0f) &&
                       std::fabs(wide.y1 - wide.y0 - 1080.0f) < 0.01f && std::fabs((wide.x0 + wide.x1) * 0.5f - 960.0f) < 0.01f &&
                       wide.x1 - wide.x0 < 1920.0f;
    bool f2 = false, f3 = false, scrub = false, phaseKey = false;
    for (const HelpLine& l : helpLines()) {
        const std::string t = l.text;
        f2 = f2 || t.find("F2") != std::string::npos;
        f3 = f3 || t.find("F3") != std::string::npos;
        scrub = scrub || t.find(", .") != std::string::npos;
        phaseKey = phaseKey || t.find("/ next phase") != std::string::npos;
    }
    bool allV6 = !keyBindingsV6().empty();
    for (const KeyBinding& k : keyBindingsV6()) allV6 = allV6 && std::string(k.version) == "v6";
    record("T239", "v6 HUD: dusk banner replaced by NIGHT FALLS (the shift-change line stays beside it), two withdrawals merge into one counted banner; CV boxes map onto the FPV screen (identity at the same FOV, x2 when zoomed in, centred pillarbox at 16:9); F1 lists F2 / , . / / / F3",
           "replace + merge (count 2), texts, full box = full 4:3 screen, zoom doubles the box, 16:9 full height centred; help has the four v6 keys, key table all v6",
           fmt("dusk %d, replaced %d, shift kept %d, merged %d (%d banners, count %d), texts %d, map %d (16:9 x %.0f..%.0f), help %d %d %d %d, table %d", duskOn, replaced,
               shiftSeparate, merged, withdrawBanners, wb ? wb->value : -1, texts, mapOk, wide.x0, wide.x1, f2, scrub, phaseKey, f3, allV6),
           duskOn && replaced && shiftSeparate && merged && texts && mapOk && f2 && f3 && scrub && phaseKey && allV6);

    // T310 (WP-CAP): the v7 director cut: about 150 s in 23 segments, the v5 / v6 cuts unchanged (120 s each); the actions
    // in story order (picker -> UAV -> round -> rover -> soldier, the five render modes 0..4 in order, the ray debugger, the
    // Explorer stages raster 3 / interpolation 4 / reflection 9 / final 15); the clock: night action, dawn through -6 deg,
    // day heroes and ray tracing in sunlight, dusk through sunset, night finale; every camera finite with its target apart.
    {
        const DirectorCut v7 = DirectorCut::V7;
        const float len = directorLength(v7);
        const auto act = [&](int i) { return directorShot(scene, directorShotStart(i, v7) + 0.1f, v7); };
        bool order = act(kV7Picker).act == V7Act::Picker && act(kV7PovUav).act == V7Act::PovUav &&
                     act(kV7PovMissile).act == V7Act::PovMissile && act(kV7PovRover).act == V7Act::PovRover &&
                     act(kV7SoldierFire).act == V7Act::SoldierFire && act(kV7RtTree).act == V7Act::RtTree;
        for (int m = 0; m < 5; ++m) order = order && act(kV7RtRaster + m).act == V7Act::RtMode && act(kV7RtRaster + m).param == m;
        const int stages[4] = {3, 4, 9, 15};
        for (int k = 0; k < 4; ++k) order = order && act(kV7ExRaster + k).act == V7Act::Explorer && act(kV7ExRaster + k).param == stages[k];
        const auto hour7 = [&](int i, float u) {
            return directorScript(directorShotStart(i, v7) + u * (directorShotStart(i + 1, v7) - directorShotStart(i, v7)), v7).hour;
        };
        bool night = true;
        for (int i = kV7NightWave; i <= kV7SoldierFire; ++i) night = night && elev(hour7(i, 0.5f)) < -6.0f;
        bool day = true;
        for (int i = kV7HeroTank; i <= kV7ExFinal; ++i) day = day && elev(hour7(i, 0.5f)) > 10.0f;
        const float d0 = elev(hour7(kV7Dawn, 0.02f)), d1 = elev(hour7(kV7Dawn, 0.98f));
        const float s0 = elev(hour7(kV7Dusk, 0.02f)), s1 = elev(hour7(kV7Dusk, 0.98f));
        const float fin = elev(directorScript(len - 0.5f, v7).hour);
        bool cams = true;
        for (float t = 0.0f; t < len; t += 0.25f) {
            const DirectorShot sh = directorShot(scene, t, v7);
            cams = cams && std::isfinite(sh.eye.x + sh.eye.y + sh.eye.z + sh.target.x + sh.target.y + sh.target.z) &&
                   length(sh.target - sh.eye) > 0.5f && sh.local >= 0.0f && sh.local <= sh.length + 1e-3f && sh.name[0];
        }
        record("T310", "v7 director cut: ~150 s, 23 segments, actions in story order, render modes 0..4, Explorer stages 3/4/9/15, night / dawn / day / dusk / night clock, finite cameras; v5 / v6 cuts unchanged",
               "length 140..160 s, 23 segments, v5 = v6 = 120 s; order ok; night < -6 deg, day > 10 deg, dawn < -6 -> > 4, dusk > 0 -> < 0, finale < -6; cameras ok",
               fmt("length %.1f s, %d segments, v5 %.0f v6 %.0f, order %d, night %d, day %d, dawn %.1f -> %.1f, dusk %.1f -> %.1f, finale %.1f, cameras %d", len,
                   directorShotCount(v7), directorLength(DirectorCut::V5), directorLength(DirectorCut::V6), order, night, day, d0, d1, s0, s1, fin, cams),
               len >= 140.0f && len <= 160.0f && directorShotCount(v7) == kV7ShotCount && kV7ShotCount == 23 &&
                   std::fabs(directorLength(DirectorCut::V5) - 120.0f) < 0.01f && std::fabs(directorLength(DirectorCut::V6) - 120.0f) < 0.01f &&
                   order && night && day && d0 < -6.0f && d1 > 4.0f && s0 > 0.0f && s1 < 0.0f && fin < -6.0f && cams);
    }
}
