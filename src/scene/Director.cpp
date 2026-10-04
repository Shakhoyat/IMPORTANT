#include "scene/Director.h"

#include "math/Frustum.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/SceneApi.h"

namespace ts {

namespace {

float ease(float x) {  // smooth start and stop of camera moves
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

Vec3 onOrbit(const Vec3& centre, float angleDeg, float radius, float height) {
    const float a = radians(angleDeg);
    return centre + Vec3{std::sin(a) * radius, height, std::cos(a) * radius};
}

struct Segment {
    const char* name;
    float duration;
};

// 12 shots, 120 s.
enum Shots { kShotFlyover, kShotRadar, kShotPad, kShotTakeoff, kShotMotorPool, kShotTank, kShotAaGun, kShotBunker, kShotStrike,
             kShotDestroyed, kShotThermalWide, kShotFinale };
constexpr Segment kSegments[] = {
    {"BASE FLYOVER", 12.0f},        {"RADAR", 6.0f},
    {"HELIPAD: START-UP", 6.0f},    {"HELICOPTER TAKE-OFF", 8.0f},
    {"MOTOR POOL", 6.0f},           {"MAIN BATTLE TANK", 8.0f},
    {"AA GUN", 8.0f},               {"UNDERGROUND BUNKER: RAMP, BLAST DOORS, TUNNEL, CIC", 24.0f},
    {"UAV STRIKE: THERMAL FPV", 12.0f}, {"TARGET DESTROYED", 10.0f},
    {"THERMAL OVERVIEW", 6.0f},     {"BASE FINALE", 14.0f},
};
constexpr int kSegmentCount = static_cast<int>(sizeof(kSegments) / sizeof(kSegments[0]));

float startOf(int index) {
    float t = 0.0f;
    for (int i = 0; i < index; ++i) t += kSegments[i].duration;
    return t;
}

Vec3 slotPos(const Scene& scene, Slot slot, const Vec3& fallback) {
    const std::vector<Controllable*> c = sa::controllables(scene);
    const int i = resolveSlot(c, slot);
    return i >= 0 ? c[i]->position() : fallback;
}

float terrainGuess(float x, float z) {
    return std::sqrt(x * x + z * z) <= kTerrainRadius ? terrainHeight(x, z) : terrainRimHeight();
}

}  // namespace

float directorLength() {
    float total = 0.0f;
    for (const Segment& s : kSegments) total += s.duration;
    return total;
}
int directorShotCount() { return kSegmentCount; }
float directorShotStart(int index) { return startOf(index < 0 ? 0 : (index >= kSegmentCount ? kSegmentCount - 1 : index)); }
const char* directorShotName(int index) { return kSegments[index < 0 ? 0 : (index >= kSegmentCount ? kSegmentCount - 1 : index)].name; }

WalkPose directorWalkPose(const WalkSpace* space, float u) {
    // Path along the centre line: ramp top -> down the ramp -> blast doors (z = 4) -> tunnel -> CIC.
    static const float kPath[][2] = {{0.0f, 17.0f}, {0.0f, 13.0f}, {0.0f, layout::kRampBottomZ}, {0.0f, layout::kBlastDoorZ - 0.5f},
                                     {0.0f, -1.0f}, {0.0f, -6.0f}};
    constexpr int kN = sizeof(kPath) / sizeof(kPath[0]);
    float lens[kN - 1], total = 0.0f;
    for (int i = 0; i + 1 < kN; ++i) {
        lens[i] = std::sqrt((kPath[i + 1][0] - kPath[i][0]) * (kPath[i + 1][0] - kPath[i][0]) +
                            (kPath[i + 1][1] - kPath[i][1]) * (kPath[i + 1][1] - kPath[i][1]));
        total += lens[i];
    }
    const float w = clampf((u - 0.05f) / 0.85f, 0.0f, 1.0f);  // pause at the start and at the end
    float d = ease(w) * 0.35f * total + w * 0.65f * total;     // eased start, steady walk
    d = clampf(d, 0.0f, total);
    const auto pointAt = [&](float dd, float& x, float& z) {
        int seg = 0;
        while (seg < kN - 2 && dd > lens[seg]) dd -= lens[seg++];
        const float k = lens[seg] > 0.0f ? clampf(dd / lens[seg], 0.0f, 1.0f) : 0.0f;
        x = lerpf(kPath[seg][0], kPath[seg + 1][0], k);
        z = lerpf(kPath[seg][1], kPath[seg + 1][1], k);
    };
    const auto floorAt = [&](float x, float z, float feet) {
        if (std::sqrt(x * x + z * z) > kTerrainRadius) return terrainRimHeight();
        if (space) return space->floorHeight(x, z, feet);
        // site-plan ramp profile
        return z >= layout::kRampTopZ ? terrainGuess(x, z)
               : z > layout::kRampBottomZ ? lerpf(layout::kBunkerFloorY, terrainGuess(x, layout::kRampTopZ),
                                                   (z - layout::kRampBottomZ) / (layout::kRampTopZ - layout::kRampBottomZ))
                                          : layout::kBunkerFloorY;
    };
    // Walk the path from the start so the floor under the feet is found step by step (ramp, then rooms under the roof).
    float x = kPath[0][0], z = kPath[0][1];
    float floor = terrainGuess(x, z);
    for (float dd = 0.0f; dd < d; dd += 0.1f) {
        pointAt(dd, x, z);
        floor = floorAt(x, z, floor);
    }
    pointAt(d, x, z);
    floor = floorAt(x, z, floor);
    float eyeY = floor + 1.7f;
    if (space) eyeY = std::min(eyeY, space->ceilingHeight(x, z, floor) - 0.15f);
    const float bob = std::sin(d * 7.0f) * 0.02f;
    WalkPose p;
    p.eye = {x, eyeY + bob, z};
    // Look straight down the path; glance down the ramp at the start, scan the room at the end.
    p.yawDeg = 180.0f + (u > 0.88f ? 28.0f * std::sin((u - 0.88f) / 0.12f * 3.1416f * 0.9f) : 0.0f);
    p.pitchDeg = lerpf(-9.0f, 0.0f, ease(u * 3.0f));
    return p;
}

DirectorScript directorScript(float t) {
    t = std::fmod(std::max(t, 0.0f), directorLength());
    DirectorScript s;
    const auto in = [&](float a, float b) { return t >= a && t < b; };
    if (in(12.5f, 12.7f)) {  // open the blast doors (toggle: one rising edge) while the camera is on the radar
        s.slot = static_cast<int>(Slot::Doors);
        s.in.fire = false;
        s.in.actionPressed = true;
    } else if (in(18.0f, 31.5f)) {  // helicopter: start-up, vertical take-off, then forward flight
        s.slot = static_cast<int>(Slot::Helicopter);
        s.in.actionPressed = in(18.3f, 18.6f);
        s.in.lift = in(22.0f, 27.5f) ? 1.0f : 0.0f;
        s.in.forward = in(27.0f, 31.5f) ? 0.6f : 0.0f;
        s.in.turn = in(29.0f, 31.5f) ? 0.3f : 0.0f;
    } else if (in(32.0f, 46.0f)) {  // tank: drive out of the motor pool and fire twice
        s.slot = static_cast<int>(Slot::Tank);
        s.in.forward = in(33.0f, 40.0f) ? 1.0f : 0.0f;
        s.in.turn = in(34.0f, 37.0f) ? 0.4f : 0.0f;
        s.in.aimYaw = in(40.0f, 41.0f) ? 0.2f : 0.0f;
        s.in.fire = in(41.5f, 41.65f) || in(44.0f, 44.15f);
    } else if (in(46.0f, 54.0f)) {  // AA gun: raise the barrels, long burst at the sky
        s.slot = static_cast<int>(Slot::AaGun);
        s.in.aimPitch = in(46.2f, 47.2f) ? 1.0f : 0.0f;
        s.in.aimYaw = in(48.0f, 52.0f) ? 0.35f : 0.0f;
        s.in.fire = in(47.4f, 53.6f);
    }
    s.strike = in(81.0f, 81.4f);
    return s;
}

DirectorShot directorShot(const Scene& scene, float t) {
    t = std::fmod(std::max(t, 0.0f), directorLength());
    int index = 0;
    while (index < kSegmentCount - 1 && t >= kSegments[index].duration) t -= kSegments[index++].duration;
    const float len = kSegments[index].duration;
    const float u = t / len;  // 0..1 inside the shot

    DirectorShot s;
    s.name = kSegments[index].name;
    s.script = index;
    s.fade = clampf(std::min(t, len - t) / 0.35f, 0.0f, 1.0f);
    using namespace layout;
    const Vec3 rover = sa::primaryRover(scene) + Vec3{0, 0.2f, 0};
    const Vec3 uav = transformPoint(scene.uav().root->world, {0, 0, 0});
    const Vec3 baseCentre{0.0f, 1.5f, 8.0f};

    switch (index) {
        case kShotFlyover: {  // crane in over the north gate, past the helipad, towards the radar
            s.eye = lerp(Vec3{-14.0f, 17.0f, 50.0f}, Vec3{44.0f, 8.5f, 12.0f}, ease(u));
            const Vec3 a{6.0f, 1.5f, 24.0f}, b = kHelipad + Vec3{0, 2.0f, 0}, c = kRadarSite + Vec3{0, 4.5f, 0};
            s.target = u < 0.5f ? lerp(a, b, ease(u * 2.0f)) : lerp(b, c, ease((u - 0.5f) * 2.0f));
            s.fovDeg = 46.0f;
            break;
        }
        case kShotRadar:  // slow orbit round the rotating radar antenna
            s.eye = onOrbit(kRadarSite, lerpf(200.0f, 255.0f, ease(u)), lerpf(12.0f, 9.0f, u), 3.2f);
            s.target = kRadarSite + Vec3{0, 3.8f, 0};
            s.fovDeg = 44.0f;
            break;
        case kShotPad:  // helicopter on the pad, rotors starting
            s.eye = kHelipad + Vec3{-11.0f + 2.0f * u, 1.4f, -7.0f};
            s.target = slotPos(scene, Slot::Helicopter, kHelipad + Vec3{0, 1.6f, 0}) + Vec3{0, 0.3f, 0};
            s.fovDeg = 40.0f;
            break;
        case kShotTakeoff:  // tilt up with the helicopter as it lifts off the pad
            s.eye = kHelipad + Vec3{-13.0f, 1.5f + 3.5f * ease(u), -4.0f};
            s.target = slotPos(scene, Slot::Helicopter, kHelipad + Vec3{0, 4.0f, 0});
            s.fovDeg = lerpf(38.0f, 46.0f, u);
            break;
        case kShotMotorPool:  // tank parked in its bay, low three-quarter view
            s.eye = kMotorPool + Vec3{lerpf(9.0f, 7.0f, ease(u)), 1.0f, lerpf(4.0f, 3.0f, ease(u))};
            s.target = slotPos(scene, Slot::Tank, kMotorPool + Vec3{0, 1.2f, 0}) + Vec3{0, 1.0f, 0};
            s.fovDeg = 42.0f;
            break;
        case kShotTank: {  // high three-quarter follow camera (the turret chase view clips the containers)
            const Vec3 tank = slotPos(scene, Slot::Tank, kMotorPool);
            const float a = radians(lerpf(20.0f, 70.0f, ease(u)));
            s.eye = tank + Vec3{std::sin(a) * 9.0f, 6.5f, std::cos(a) * 9.0f};
            s.target = tank + Vec3{0, 1.3f, 0};
            s.fovDeg = 46.0f;
            break;
        }
        case kShotAaGun: {  // low beside the pit, tilting up with the tracer stream (the sight view is all muzzle flash)
            const Vec3 gun = slotPos(scene, Slot::AaGun, kAaGunSite + Vec3{0, 1.5f, 0});
            s.eye = kAaGunSite + Vec3{lerpf(-7.5f, -6.0f, u), 1.2f, lerpf(5.0f, 7.0f, u)};
            s.target = gun + Vec3{0, 1.0f + 9.0f * ease(u), 0};
            s.fovDeg = lerpf(48.0f, 58.0f, u);
            break;
        }
        case kShotBunker: {  // first-person walk: down the ramp, through the blast doors, along the tunnel into the CIC
            const WalkPose p = directorWalkPose(sa::walkSpace(scene), u);
            const float yaw = radians(p.yawDeg), pitch = radians(p.pitchDeg);
            s.eye = p.eye;
            s.target = p.eye + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
            s.fovDeg = 62.0f;
            s.walk = true;
            break;
        }
        case kShotStrike:  // the UAV's thermal gimbal: lock, missile away, impact
            s.eye = uav + Vec3{0, -0.2f, 0};
            s.target = rover;
            s.fovDeg = lerpf(26.0f, 12.0f, ease(u * 1.5f));
            s.thermal = true;
            s.fpvHud = true;
            break;
        case kShotDestroyed: {  // low, close, on the burning wreck
            const float a = radians(lerpf(35.0f, 110.0f, ease(u)));
            s.eye = rover + Vec3{std::sin(a) * 5.0f, 0.7f, std::cos(a) * 5.0f};
            s.target = rover + Vec3{0, 0.9f, 0};
            s.fovDeg = 42.0f;
            break;
        }
        case kShotThermalWide:  // heat picture of the whole compound
            s.eye = onOrbit({0, 0, 0}, lerpf(60.0f, 110.0f, u), 30.0f, 17.0f);
            s.target = lerp(baseCentre, rover, 0.4f);
            s.thermal = true;
            s.fovDeg = 52.0f;
            break;
        default:  // finale: pull back and up from the compound, radar turning, helicopter above
            s.eye = onOrbit({0, 0, 0}, lerpf(150.0f, 215.0f, ease(u)), lerpf(22.0f, 62.0f, ease(u)), lerpf(5.0f, 30.0f, ease(u)));
            s.target = lerp(Vec3{0, 1.5f, 4.0f}, Vec3{6.0f, 3.0f, 0.0f}, ease(u));
            s.fovDeg = 44.0f;
            break;
    }
    if (!s.uavCamera && !s.walk) s.eye.y = std::max(s.eye.y, terrainGuess(s.eye.x, s.eye.z) + 0.15f);
    return s;
}

}  // namespace ts
