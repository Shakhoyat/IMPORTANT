#include "scene/Director.h"

#include "math/Frustum.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "scene/Garrison.h"
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

// v5 cut: 13 shots, 120 s.
enum Shots { kShotFlyover, kShotRadar, kShotPad, kShotTakeoff, kShotMotorPool, kShotTank, kShotAaGun, kShotBunker, kShotPatrol,
             kShotStrike, kShotFpvTracks, kShotKill, kShotFinale };
constexpr Segment kSegmentsV5[] = {
    {"BASE FLYOVER: WAVE INCOMING", 10.0f}, {"RADAR", 5.0f},
    {"HELIPAD: START-UP", 5.0f},    {"HELICOPTER TAKE-OFF", 7.0f},
    {"MOTOR POOL", 5.0f},           {"MAIN BATTLE TANK", 7.0f},
    {"AA GUN", 7.0f},               {"UNDERGROUND BUNKER: RAMP, BLAST DOORS, TUNNEL, CIC", 22.0f},
    {"TWO UAVS ON PATROL", 10.0f},  {"COMBINED STRIKE: TIME ON TARGET", 16.0f},
    {"THERMAL FPV: ALL TRACKS", 8.0f}, {"MULTI-HIT KILL", 10.0f},
    {"BASE FINALE", 8.0f},
};
constexpr int kSegmentCountV5 = static_cast<int>(sizeof(kSegmentsV5) / sizeof(kSegmentsV5[0]));

// v6 cut: 15 shots, 120 s (order = DirectorShotV6).
constexpr Segment kSegmentsV6[] = {
    {"NIGHT: WAVE INCOMING", 8.0f},          {"TWO UAVS ON PATROL", 7.0f},
    {"COMBINED STRIKE: TIME ON TARGET", 11.0f}, {"THERMAL FPV: ALL TRACKS", 7.0f},
    {"MULTI-HIT KILL", 9.0f},                {"DAWN: TIME-LAPSE, INTRUDERS WITHDRAW", 10.0f},
    {"DAY: BASE FLYOVER", 9.0f},             {"SOLDIERS ON PATROL", 7.0f},
    {"MECHANIC AT THE TANK", 6.0f},          {"LOGISTICS: CRATE RUN", 6.0f},
    {"HELICOPTER TAKE-OFF", 9.0f},           {"DAY THERMAL: PERSONNEL ARE NOT TARGETS", 7.0f},
    {"DUSK: SHIFT CHANGE", 9.0f},            {"OFF DUTY: INTO THE BUNKER", 8.0f},
    {"NIGHT: SEARCHLIGHTS ON", 7.0f},
};
constexpr int kSegmentCountV6 = static_cast<int>(sizeof(kSegmentsV6) / sizeof(kSegmentsV6[0]));
static_assert(kSegmentCountV6 == kV6ShotCount, "v6 shot table and DirectorShotV6 must match");

// v7 cut: 23 segments, 150 s (order = DirectorShotV7). Titles: segment name (line 1) + what it shows (line 2).
struct SegmentV7 {
    const char* name;
    float duration;
    V7Act act;
    int param;
    const char* subtitle;
};
constexpr SegmentV7 kSegmentsV7[] = {
    {"NIGHT: WAVE INCOMING", 8.0f, V7Act::Camera, 0, "intruder rovers close in on the camp; two UAVs patrol"},
    {"COMBINED STRIKE", 9.0f, V7Act::Camera, 0, "both UAVs commit to one vision track: rounds on target together"},
    {"POV PICKER", 4.0f, V7Act::Picker, 0, "every moving object, sorted by distance: jump into any of them"},
    {"JUMP: UAV-2 GIMBAL", 5.0f, V7Act::PovUav, 0, "the wing UAV's thermal camera, spring-blended camera jump"},
    {"JUMP: RIDE THE ROUND", 7.0f, V7Act::PovMissile, 0, "nose camera of a UAV round, held at the impact"},
    {"JUMP: INTRUDER ROVER", 6.0f, V7Act::PovRover, 0, "the hunted rover's mast camera while the UAVs designate it"},
    {"FIRST PERSON: SENTRY", 7.0f, V7Act::SoldierFire, 0, "a soldier's eyes: rifle, recoil, muzzle flash, tracers"},
    {"DAWN: TIME-LAPSE", 11.0f, V7Act::Camera, 0, "the sun rises, surviving rovers withdraw, the day shift turns out"},
    {"DAY: MAIN BATTLE TANK", 7.0f, V7Act::Camera, 0, "hero orbit: tracks, turret, sights (primitives only)"},
    {"DAY: SCOUT HELICOPTER", 7.0f, V7Act::Camera, 0, "hero orbit: canopy, rotor head, skids"},
    {"DAY: AA GATLING GUN", 7.0f, V7Act::Camera, 0, "hero orbit: six-barrel gatling, radar dome, mount"},
    {"RAY TRACING 1/5: RASTER", 4.5f, V7Act::RtMode, 0, "the real-time rasteriser: glass and mirrors approximated"},
    {"RAY TRACING 2/5: HYBRID", 4.5f, V7Act::RtMode, 1, "raster G-buffer + ray-traced shadows and reflections"},
    {"RAY TRACING 3/5: WHITTED", 4.5f, V7Act::RtMode, 2, "recursive rays: reflection, refraction (Snell, Fresnel)"},
    {"RAY TRACING 4/5: PATH TRACING", 4.5f, V7Act::RtMode, 3, "progressive Monte Carlo: soft shadows, colour bleeding"},
    {"RAY TRACING 5/5: LIGHT TRACING", 4.5f, V7Act::RtMode, 4, "reverse: paths start at the lights (caustics)"},
    {"RAY DEBUGGER", 6.5f, V7Act::RtTree, 2, "one pixel's ray tree: primary, reflection, refraction, shadow rays"},
    {"FRAME EXPLORER: RASTERISATION", 6.0f, V7Act::Explorer, 3, "edge functions and coverage, scanline by scanline"},
    {"FRAME EXPLORER: COLOUR INTERPOLATION", 5.5f, V7Act::Explorer, 4, "barycentric, affine vs perspective-correct"},
    {"FRAME EXPLORER: REFLECTION", 5.5f, V7Act::Explorer, 9, "mirror law and the screen-space ray march"},
    {"FRAME EXPLORER: FINAL FRAME", 5.0f, V7Act::Explorer, 15, "tone-mapped frame + the thermal sensor's view"},
    {"DUSK: SHIFT CHANGE", 8.0f, V7Act::Camera, 0, "the sun sets, lamps come on, the night shift takes over"},
    {"NIGHT: SEARCHLIGHTS ON", 13.0f, V7Act::Camera, 0, "the UAVs light their searchlights for the next wave"},
};
constexpr int kSegmentCountV7 = static_cast<int>(sizeof(kSegmentsV7) / sizeof(kSegmentsV7[0]));
static_assert(kSegmentCountV7 == kV7ShotCount, "v7 shot table and DirectorShotV7 must match");
// The generic helpers below see the v7 table through this name / duration view.
struct SegmentsV7View {
    Segment items[kSegmentCountV7];
    constexpr SegmentsV7View() : items() {
        for (int i = 0; i < kSegmentCountV7; ++i) items[i] = Segment{kSegmentsV7[i].name, kSegmentsV7[i].duration};
    }
};
constexpr SegmentsV7View kSegmentsV7View{};

const Segment* segments(DirectorCut cut) {
    return cut == DirectorCut::V5 ? kSegmentsV5 : cut == DirectorCut::V6 ? kSegmentsV6 : kSegmentsV7View.items;
}
int segmentCount(DirectorCut cut) {
    return cut == DirectorCut::V5 ? kSegmentCountV5 : cut == DirectorCut::V6 ? kSegmentCountV6 : kSegmentCountV7;
}

float startOf(int index, DirectorCut cut) {
    float t = 0.0f;
    for (int i = 0; i < index; ++i) t += segments(cut)[i].duration;
    return t;
}

int clampIndex(int index, DirectorCut cut) { return index < 0 ? 0 : (index >= segmentCount(cut) ? segmentCount(cut) - 1 : index); }

// Shot index and shot-local time of sequence time t.
int locate(float& t, DirectorCut cut) {
    t = std::fmod(std::max(t, 0.0f), directorLength(cut));
    int index = 0;
    while (index < segmentCount(cut) - 1 && t >= segments(cut)[index].duration) t -= segments(cut)[index++].duration;
    return index;
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

float directorLength(DirectorCut cut) {
    float total = 0.0f;
    for (int i = 0; i < segmentCount(cut); ++i) total += segments(cut)[i].duration;
    return total;
}
int directorShotCount(DirectorCut cut) { return segmentCount(cut); }
float directorShotStart(int index, DirectorCut cut) { return startOf(clampIndex(index, cut), cut); }
const char* directorShotName(int index, DirectorCut cut) { return segments(cut)[clampIndex(index, cut)].name; }

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

namespace {

DirectorScript scriptV5(float t) {
    t = std::fmod(std::max(t, 0.0f), directorLength(DirectorCut::V5));
    DirectorScript s;
    const auto in = [&](float a, float b) { return t >= a && t < b; };
    if (in(10.5f, 10.7f)) {  // open the blast doors (toggle: one rising edge) while the camera is on the radar
        s.slot = static_cast<int>(Slot::Doors);
        s.in.fire = false;
        s.in.actionPressed = true;
    } else if (in(15.0f, 28.0f)) {  // helicopter: start-up, vertical take-off, then forward flight
        s.slot = static_cast<int>(Slot::Helicopter);
        s.in.actionPressed = in(15.3f, 15.6f);
        s.in.lift = in(19.5f, 25.0f) ? 1.0f : 0.0f;
        s.in.forward = in(24.5f, 28.0f) ? 0.6f : 0.0f;
        s.in.turn = in(26.0f, 28.0f) ? 0.3f : 0.0f;
    } else if (in(28.0f, 40.0f)) {  // tank: drive out of the motor pool and fire twice
        s.slot = static_cast<int>(Slot::Tank);
        s.in.forward = in(29.0f, 36.0f) ? 1.0f : 0.0f;
        s.in.turn = in(30.0f, 33.0f) ? 0.4f : 0.0f;
        s.in.aimYaw = in(35.0f, 36.0f) ? 0.2f : 0.0f;
        s.in.fire = in(36.5f, 36.65f) || in(38.5f, 38.65f);
    } else if (in(40.0f, 46.0f)) {  // AA gun: raise the barrels, long burst at the sky
        s.slot = static_cast<int>(Slot::AaGun);
        s.in.aimPitch = in(40.2f, 41.2f) ? 1.0f : 0.0f;
        s.in.aimYaw = in(41.8f, 45.0f) ? 0.35f : 0.0f;
        s.in.fire = in(41.4f, 45.8f);
    }
    return s;  // the UAV strikes are played by the squad itself (no scripted missile any more)
}

// Clock of the v6 cut at shot 'index', shot-local fraction u: night battle at 23:00, a dawn time-lapse, day hours that
// advance shot by shot, a dusk ramp through sunset and the night again.
float hourV6(int index, float u) {
    switch (index) {
        case kV6NightWave: case kV6Patrol: case kV6Strike: case kV6FpvTracks: case kV6Kill: return 23.0f;
        case kV6Dawn: return lerpf(5.2f, 7.4f, ease(u));
        case kV6DayFlyover: return 9.5f;
        case kV6SoldierPatrol: return 10.0f;
        case kV6Mechanic: return 10.5f;
        case kV6Logistics: return 11.0f;
        case kV6Heli: return 12.0f;
        case kV6DayThermal: return 13.0f;
        case kV6Dusk: return lerpf(17.2f, 18.05f, ease(u));
        case kV6Bunker: return lerpf(18.05f, 18.4f, u);
        default: return 19.5f;
    }
}

DirectorScript scriptV6(float t) {
    const int index = locate(t, DirectorCut::V6);  // t is now shot-local
    const float len = kSegmentsV6[index].duration;
    DirectorScript s;
    s.hour = hourV6(index, t / len);
    const auto in = [&](float a, float b) { return t >= a && t < b; };
    if (index == kV6Heli) {  // start-up, vertical take-off, then forward flight
        s.slot = static_cast<int>(Slot::Helicopter);
        s.in.actionPressed = in(0.2f, 0.5f);
        s.in.lift = in(3.8f, 8.0f) ? 1.0f : 0.0f;
        s.in.forward = in(7.0f, 9.0f) ? 0.6f : 0.0f;
        s.in.turn = in(8.0f, 9.0f) ? 0.3f : 0.0f;
    } else if (index == kV6Dusk && in(0.1f, 0.3f)) {  // open the blast doors for the off-duty shift (App keeps them open)
        s.slot = static_cast<int>(Slot::Doors);
        s.in.actionPressed = true;
    }
    return s;
}

}  // namespace

namespace {

// Clock of the v7 cut: the night action at 23:00, a dawn time-lapse, the day (heroes 10:30, ray tracing and the frozen
// Explorer frame at 16:30 for long sun shadows), the dusk ramp and the night again.
float hourV7(int index, float u) {
    if (index <= kV7SoldierFire) return 23.0f;
    if (index == kV7Dawn) return lerpf(5.2f, 7.4f, ease(u));
    if (index <= kV7HeroAa) return 10.5f;
    if (index <= kV7ExFinal) return 16.5f;
    if (index == kV7Dusk) return lerpf(17.2f, 18.05f, ease(u));
    return lerpf(19.5f, 20.0f, u);
}

DirectorScript scriptV7(float t) {
    const int index = locate(t, DirectorCut::V7);
    DirectorScript s;
    s.hour = hourV7(index, t / kSegmentsV7[index].duration);
    return s;  // no scripted controllables: the recorder drives the v7 actions (POV jumps, possession) itself
}

}  // namespace

DirectorScript directorScript(float t, DirectorCut cut) {
    return cut == DirectorCut::V5 ? scriptV5(t) : cut == DirectorCut::V6 ? scriptV6(t) : scriptV7(t);
}

void directorRtCamera(Vec3& eye, Vec3& target, float& fovDeg, bool side) {
    const Vec3 p{kV7PropsX, terrainHeight(kV7PropsX, kV7PropsZ), kV7PropsZ};
    if (side) {
        eye = p + Vec3{-7.0f, 3.0f, -1.5f};
        target = p + Vec3{-1.0f, 1.0f, 2.5f};
        fovDeg = 55.0f;
    } else {
        eye = p + Vec3{-4.6f, 2.3f, 6.2f};
        target = p + Vec3{0.6f, 0.5f, -0.8f};
        fovDeg = 50.0f;
    }
}

namespace {

// The point the game shots follow: the squad's target track, else the centroid of the confirmed tracks, else the rover.
Vec3 actionPoint(const Scene& scene) {
    const Squad& sq = scene.squad();
    if (sq.targetTrack() >= 0)
        if (const Track* t = sq.tracker().find(sq.targetTrack())) return t->position();
    Vec3 sum{0, 0, 0};
    int n = 0;
    for (const Track& t : sq.tracker().tracks())
        if (t.state != TrackState::Tentative) {
            sum = sum + t.position();
            ++n;
        }
    if (n > 0) return sum * (1.0f / static_cast<float>(n));
    return sa::primaryRover(scene);
}

// The rover worth filming for a kill: the most damaged one still alive, else the freshest wreck, else the action point.
Vec3 hotRover(const Scene& scene) {
    const Intruder* best = nullptr;
    for (const auto& in : scene.intruders())
        if (in->life == Intruder::Life::Alive && in->hits > 0 && (!best || in->health < best->health)) best = in.get();
    if (best) return best->state.position;
    const Intruder* wreck = nullptr;
    for (const auto& in : scene.intruders())
        if (in->life == Intruder::Life::Wreck && in->timer < 8.0f && (!wreck || in->timer < wreck->timer)) wreck = in.get();
    if (wreck) return wreck->wreckPosition;
    return actionPoint(scene);
}

}  // namespace

namespace {

DirectorShot shotV5(const Scene& scene, float t) {
    const int index = locate(t, DirectorCut::V5);
    const float len = kSegmentsV5[index].duration;
    const float u = t / len;  // 0..1 inside the shot

    DirectorShot s;
    s.name = kSegmentsV5[index].name;
    s.script = index;
    s.fade = clampf(std::min(t, len - t) / 0.35f, 0.0f, 1.0f);
    using namespace layout;
    const Vec3 uav = transformPoint(scene.uav().root->world, {0, 0, 0});

    switch (index) {
        case kShotFlyover: {  // crane in over the north gate, past the helipad, towards the radar
            s.eye = lerp(Vec3{-14.0f, 17.0f, 50.0f}, Vec3{44.0f, 8.5f, 12.0f}, ease(u));
            const Vec3 a{6.0f, 1.5f, 24.0f}, b = kHelipad + Vec3{0, 2.0f, 0}, c = kRadarSite + Vec3{0, 4.5f, 0};
            s.target = u < 0.5f ? lerp(a, b, ease(u * 2.0f)) : lerp(b, c, ease((u - 0.5f) * 2.0f));
            s.fovDeg = 46.0f;
            s.gameHud = true;  // opens with the "WAVE n INCOMING" banner
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
        case kShotPatrol:  // both UAVs patrolling opposite sides of the compound, wide orbit
            s.eye = onOrbit({0, 0, 4.0f}, lerpf(25.0f, 85.0f, ease(u)), 36.0f, 21.0f);
            s.target = {0.0f, 3.0f, 3.0f};
            s.fovDeg = 58.0f;
            s.gameHud = true;
            break;
        case kShotStrike: {  // wide angle on the squad: both UAVs, the target and the converging rounds in frame
            const Vec3 a = scene.droneState(0).position, b = scene.droneState(1).position;
            const Vec3 tgt = actionPoint(scene) + Vec3{0, 0.5f, 0};
            const Vec3 c = (a + b + tgt) * (1.0f / 3.0f);
            const float r = std::max({length(a - c), length(b - c), length(tgt - c), 5.0f});
            s.eye = c + Vec3{std::sin(radians(40.0f + 50.0f * u)) * (r * 1.7f + 9.0f), r * 0.75f + 5.0f,
                             std::cos(radians(40.0f + 50.0f * u)) * (r * 1.7f + 9.0f)};
            s.target = c + Vec3{0, -1.0f, 0};
            s.fovDeg = 58.0f;
            s.gameHud = true;
            break;
        }
        case kShotFpvTracks:  // the UAV's thermal gimbal with the boxes of every track
            s.eye = uav + Vec3{0, -0.2f, 0};
            s.target = actionPoint(scene);
            s.fovDeg = 34.0f;
            s.thermal = true;
            s.fpvHud = true;
            s.gameHud = true;
            break;
        case kShotKill: {  // close orbit on the most damaged rover: hits, the last blast, the burning wreck
            const Vec3 r = hotRover(scene) + Vec3{0, 0.5f, 0};
            const float a = radians(lerpf(30.0f, 120.0f, ease(u)));
            s.eye = r + Vec3{std::sin(a) * 6.5f, 2.2f, std::cos(a) * 6.5f};
            s.target = r;
            s.fovDeg = 44.0f;
            s.gameHud = true;
            break;
        }
        default:  // finale: pull back and up from the compound, radar turning, helicopter above
            s.eye = onOrbit({0, 0, 0}, lerpf(150.0f, 215.0f, ease(u)), lerpf(22.0f, 62.0f, ease(u)), lerpf(5.0f, 30.0f, ease(u)));
            s.target = lerp(Vec3{0, 1.5f, 4.0f}, Vec3{6.0f, 3.0f, 0.0f}, ease(u));
            s.fovDeg = 44.0f;
            s.gameHud = true;
            break;
    }
    if (!s.uavCamera && !s.walk) s.eye.y = std::max(s.eye.y, terrainGuess(s.eye.x, s.eye.z) + 0.15f);
    return s;
}

// The soldier the off-duty shot follows: the one closest to the bunker ramp (fallback: the ramp top).
Vec3 soldierNearRamp(const Scene& scene) {
    const Vec3 ramp{0.0f, 0.0f, layout::kRampTopZ};
    const Garrison* g = sa::garrison(scene);
    Vec3 best = ramp + Vec3{0.0f, 0.0f, 1.0f};
    float bestD = 1e9f;
    for (int i = 0; g && i < g->count(); ++i) {
        if (!g->soldier(i)) continue;
        const Vec3 p = g->soldier(i)->position();
        const float d = length(Vec3{p.x - ramp.x, 0.0f, p.z - ramp.z});
        if (d < bestD) {
            bestD = d;
            best = p;
        }
    }
    return best;
}

DirectorShot shotV6(const Scene& scene, float t) {
    const int index = locate(t, DirectorCut::V6);
    const float len = kSegmentsV6[index].duration;
    const float u = t / len;  // 0..1 inside the shot

    DirectorShot s;
    s.name = kSegmentsV6[index].name;
    s.script = index;
    s.fade = clampf(std::min(t, len - t) / 0.35f, 0.0f, 1.0f);
    using namespace layout;
    const Vec3 uav = transformPoint(scene.uav().root->world, {0, 0, 0});

    switch (index) {
        case kV6NightWave: {  // night crane over the north gate towards the camp centre: the wave banner
            s.eye = lerp(Vec3{-14.0f, 17.0f, 50.0f}, Vec3{30.0f, 11.0f, 26.0f}, ease(u));
            s.target = lerp(Vec3{6.0f, 1.5f, 24.0f}, Vec3{0.0f, 2.0f, 4.0f}, ease(u));
            s.fovDeg = 48.0f;
            s.gameHud = true;
            break;
        }
        case kV6Patrol:  // both UAVs and their searchlight cones over the compound
            s.eye = onOrbit({0, 0, 4.0f}, lerpf(25.0f, 75.0f, ease(u)), 36.0f, 21.0f);
            s.target = {0.0f, 3.0f, 3.0f};
            s.fovDeg = 58.0f;
            s.gameHud = true;
            break;
        case kV6Strike: {  // wide angle on the squad: both UAVs, the target and the converging rounds in frame
            const Vec3 a = scene.droneState(0).position, b = scene.droneState(1).position;
            const Vec3 tgt = actionPoint(scene) + Vec3{0, 0.5f, 0};
            const Vec3 c = (a + b + tgt) * (1.0f / 3.0f);
            const float r = std::max({length(a - c), length(b - c), length(tgt - c), 5.0f});
            s.eye = c + Vec3{std::sin(radians(40.0f + 50.0f * u)) * (r * 1.7f + 9.0f), r * 0.75f + 5.0f,
                             std::cos(radians(40.0f + 50.0f * u)) * (r * 1.7f + 9.0f)};
            s.target = c + Vec3{0, -1.0f, 0};
            s.fovDeg = 58.0f;
            s.gameHud = true;
            break;
        }
        case kV6FpvTracks:  // the UAV's thermal gimbal with the boxes of every track
            s.eye = uav + Vec3{0, -0.2f, 0};
            s.target = actionPoint(scene);
            s.fovDeg = 34.0f;
            s.thermal = true;
            s.fpvHud = true;
            s.gameHud = true;
            break;
        case kV6Kill: {  // close orbit on the most damaged rover: hits, the last blast, the burning wreck
            const Vec3 r = hotRover(scene) + Vec3{0, 0.5f, 0};
            const float a = radians(lerpf(30.0f, 120.0f, ease(u)));
            s.eye = r + Vec3{std::sin(a) * 6.5f, 2.2f, std::cos(a) * 6.5f};
            s.target = r;
            s.fovDeg = 44.0f;
            s.gameHud = true;
            break;
        }
        case kV6Dawn:  // time-lapse: high in the west, looking east over the camp into the sunrise
            s.eye = lerp(Vec3{-40.0f, 15.0f, 12.0f}, Vec3{-36.0f, 12.0f, 2.0f}, ease(u));
            s.target = lerp(Vec3{8.0f, 3.0f, 2.0f}, Vec3{12.0f, 4.0f, -2.0f}, ease(u));
            s.fovDeg = 56.0f;
            s.gameHud = true;  // DAWN / INTRUDERS WITHDRAW banners
            break;
        case kV6DayFlyover: {  // the v5 crane by day: north gate, helipad, radar
            s.eye = lerp(Vec3{-14.0f, 17.0f, 50.0f}, Vec3{44.0f, 8.5f, 12.0f}, ease(u));
            const Vec3 a{6.0f, 1.5f, 24.0f}, b = kHelipad + Vec3{0, 2.0f, 0}, c = kRadarSite + Vec3{0, 4.5f, 0};
            s.target = u < 0.5f ? lerp(a, b, ease(u * 2.0f)) : lerp(b, c, ease((u - 0.5f) * 2.0f));
            s.fovDeg = 46.0f;
            break;
        }
        case kV6SoldierPatrol: {  // slow orbit round the inner-loop patrol on its spline route
            const Vec3 p = sa::soldierPosition(scene, kSoldierPatrol, Vec3{8.0f, 0.0f, 20.0f});
            s.eye = onOrbit(p, lerpf(130.0f, 185.0f, ease(u)), 7.5f, 2.6f);
            s.target = p + Vec3{0, 1.1f, 0};
            s.fovDeg = 42.0f;
            break;
        }
        case kV6Mechanic: {  // low three-quarter view: the mechanic working on the parked tank
            const Vec3 p = sa::soldierPosition(scene, kSoldierMechanic, kMotorPool + Vec3{2.5f, 0.0f, 1.5f});
            const Vec3 mid = lerp(p, kMotorPool, 0.35f);
            s.eye = onOrbit(mid, lerpf(60.0f, 85.0f, ease(u)), 6.5f, 1.8f);
            s.target = mid + Vec3{0, 1.0f, 0};
            s.fovDeg = 44.0f;
            break;
        }
        case kV6Logistics: {  // tracking side-on with the crate carrier
            const Vec3 p = sa::soldierPosition(scene, kSoldierLogistics, kFuelDepot + Vec3{3.0f, 0.0f, 3.0f});
            s.eye = onOrbit(p, lerpf(250.0f, 280.0f, u), 6.0f, 2.2f);
            s.target = p + Vec3{0, 1.0f, 0};
            s.fovDeg = 46.0f;
            break;
        }
        case kV6Heli:  // start-up on the pad, then tilt up with the helicopter as it lifts off
            s.eye = kHelipad + Vec3{-13.0f, 1.5f + 3.5f * ease(clampf((u - 0.35f) / 0.65f, 0.0f, 1.0f)), -4.0f};
            s.target = slotPos(scene, Slot::Helicopter, kHelipad + Vec3{0, 2.0f, 0}) + Vec3{0, 0.3f, 0};
            s.fovDeg = lerpf(38.0f, 46.0f, u);
            break;
        case kV6DayThermal:  // through UAV 1's own gimbal camera: warm soldiers are boxed PERS, never targets
            s.eye = uav + Vec3{0, -0.2f, 0};
            s.target = uav + Vec3{0, -1.0f, 0};
            s.fovDeg = 40.0f;  // = the CV camera, so the blob boxes line up
            s.uavCamera = true;
            s.thermal = true;
            s.fpvHud = true;
            s.gameHud = true;
            break;
        case kV6Dusk:  // east of the camp looking west into the sunset while the shifts change
            s.eye = lerp(Vec3{36.0f, 10.0f, 14.0f}, Vec3{33.0f, 8.0f, 4.0f}, ease(u));
            s.target = lerp(Vec3{-6.0f, 2.0f, 6.0f}, Vec3{-10.0f, 3.0f, 0.0f}, ease(u));
            s.fovDeg = 54.0f;
            s.gameHud = true;  // DUSK / SHIFT CHANGE banners
            break;
        case kV6Bunker: {  // over the shoulder: a soldier going down the ramp towards the open blast doors
            const Vec3 p = soldierNearRamp(scene);
            s.eye = Vec3{lerpf(2.6f, 1.6f, ease(u)), 3.2f, kRampTopZ + 4.5f};
            s.target = p + Vec3{0, 0.9f, 0};
            s.fovDeg = 48.0f;
            break;
        }
        default:  // night again: searchlights on, pull back and up from the compound
            s.eye = onOrbit({0, 0, 0}, lerpf(150.0f, 205.0f, ease(u)), lerpf(24.0f, 56.0f, ease(u)), lerpf(6.0f, 26.0f, ease(u)));
            s.target = lerp(Vec3{0, 1.5f, 4.0f}, Vec3{4.0f, 3.0f, 0.0f}, ease(u));
            s.fovDeg = 46.0f;
            s.gameHud = true;  // NIGHT FALLS banner
            break;
    }
    if (!s.uavCamera && !s.walk) s.eye.y = std::max(s.eye.y, terrainGuess(s.eye.x, s.eye.z) + 0.15f);
    return s;
}

}  // namespace

namespace {

DirectorShot shotV7(const Scene& scene, float t) {
    const int index = locate(t, DirectorCut::V7);
    const SegmentV7& seg = kSegmentsV7[index];
    const float u = t / seg.duration;
    DirectorShot s;
    s.name = seg.name;
    s.subtitle = seg.subtitle;
    s.script = index;
    s.act = seg.act;
    s.param = seg.param;
    s.local = t;
    s.length = seg.duration;
    s.fade = clampf(std::min(t, seg.duration - t) / 0.35f, 0.0f, 1.0f);
    using namespace layout;
    // A hero orbit round a vehicle where it stands (its live position): low, slow, three-quarter.
    const auto hero = [&](Slot slot, const Vec3& fallback, float a0, float a1, float radius, float height, float aimY) {
        const Vec3 p = slotPos(scene, slot, fallback);
        s.eye = onOrbit(p, lerpf(a0, a1, ease(u)), radius, height);
        s.eye.y = std::max(s.eye.y, terrainGuess(s.eye.x, s.eye.z) + 0.6f);
        s.target = p + Vec3{0, aimY, 0};
        s.fovDeg = 46.0f;
    };
    switch (index) {
        case kV7NightWave: case kV7Strike: case kV7Dawn: case kV7Dusk:   // the v6 night, dawn and dusk shots
        {
            const int v6 = index == kV7NightWave ? kV6NightWave : index == kV7Strike ? kV6Strike : index == kV7Dawn ? kV6Dawn : kV6Dusk;
            DirectorShot b = shotV6(scene, directorShotStart(v6, DirectorCut::V6) + u * kSegmentsV6[v6].duration);
            s.eye = b.eye;
            s.target = b.target;
            s.fovDeg = b.fovDeg;
            s.gameHud = b.gameHud;
            break;
        }
        case kV7Picker:  // high, slow orbit over the camp behind the picker list
            s.eye = onOrbit({0.0f, 0.0f, 3.0f}, lerpf(20.0f, 45.0f, ease(u)), 38.0f, 24.0f);
            s.target = {0.0f, 1.0f, 3.0f};
            s.fovDeg = 52.0f;
            break;
        case kV7PovUav: case kV7PovMissile: case kV7PovRover: case kV7SoldierFire: {   // fallback: the squad from a wing
            DirectorShot b = shotV6(scene, directorShotStart(kV6Strike, DirectorCut::V6) + u * kSegmentsV6[kV6Strike].duration);
            s.eye = b.eye;
            s.target = b.target;
            s.fovDeg = b.fovDeg;
            s.gameHud = true;
            break;
        }
        // low (under the camp nets, ~3.5 m) and close: the detailed models fill the frame
        case kV7HeroTank: hero(Slot::Tank, kMotorPool, 215.0f, 265.0f, 7.5f, 2.5f, 1.1f); break;   // over the low wall
        case kV7HeroHeli: hero(Slot::Helicopter, kHelipad, 125.0f, 175.0f, 7.5f, 2.0f, 1.3f); break;
        case kV7HeroAa: hero(Slot::AaGun, kAaGunSite, 300.0f, 350.0f, 7.5f, 2.2f, 1.6f); break;
        case kV7RtTree: {  // the pixel is picked from the showcase camera, then the camera swings round to the side
            Vec3 e0, t0, e1, t1;
            float f0 = 50.0f, f1 = 55.0f;
            directorRtCamera(e0, t0, f0, false);
            directorRtCamera(e1, t1, f1, true);
            const float k = ease(clampf((t - 1.2f) / 3.5f, 0.0f, 1.0f));
            s.eye = lerp(e0, e1, k);
            s.target = lerp(t0, t1, k);
            s.fovDeg = lerpf(f0, f1, k);
            break;
        }
        case kV7RtRaster: case kV7RtHybrid: case kV7RtWhitted: case kV7RtPath: case kV7RtLight:
        case kV7ExRaster: case kV7ExInterp: case kV7ExReflect: case kV7ExFinal:   // still camera (path tracing converges)
            directorRtCamera(s.eye, s.target, s.fovDeg, false);
            break;
        default:  // finale: the v6 searchlights shot
        {
            DirectorShot b = shotV6(scene, directorShotStart(kV6NightLights, DirectorCut::V6) + u * kSegmentsV6[kV6NightLights].duration);
            s.eye = b.eye;
            s.target = b.target;
            s.fovDeg = b.fovDeg;
            s.gameHud = b.gameHud;
            break;
        }
    }
    return s;
}

}  // namespace

DirectorShot directorShot(const Scene& scene, float t, DirectorCut cut) {
    return cut == DirectorCut::V5 ? shotV5(scene, t) : cut == DirectorCut::V6 ? shotV6(scene, t) : shotV7(scene, t);
}

}  // namespace ts
