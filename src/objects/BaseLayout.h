// Site plan of the military base: the coordinates every builder, controller and camera agrees on.
// +Z = north (gates, road), +X = east. Ground level is about y = 0 inside the compound, -0.3 on the apron.
//
//   inner compound   r < 19 m : patrol area of the UAV and the intruder rovers (fence at kFenceRadius),
//                               the underground command bunker and its entrance ramp
//   outer apron   19 < r < 34 : helipad, radar, anti-aircraft gun, motor pool (tank), barracks, depot,
//                               HESCO perimeter wall with guard towers at r = kPerimeterRadius
//   terrain mesh  r < kBaseRadius, backdrop (treeline, hills) outside it
#pragma once

#include "math/Math3D.h"

namespace ts::layout {

constexpr float kBaseRadius = 36.0f;        // terrain mesh radius (inner disc + flat apron)
constexpr float kPerimeterRadius = 34.0f;   // HESCO wall + concertina wire, guard towers
constexpr float kApronLevel = -0.30f;       // ground height outside the inner compound (== terrainRimHeight())

// Underground command bunker (all rooms share one floor level).
constexpr float kBunkerFloorY = -3.4f;
constexpr float kBunkerCeilingY = -0.8f;     // 2.6 m clear height
constexpr float kBunkerRoofTopY = -0.45f;    // top of the roof slab; terrain stays >= this + 0.15 above rooms
// Entrance ramp (open trench along the vehicle track), centre line x = 0.
constexpr float kRampHalfWidth = 1.5f;       // inner half width; retaining walls 0.35 m thick outside it
constexpr float kRampTopZ = 13.0f;           // ground level end
constexpr float kRampBottomZ = 6.0f;         // floor level from here to the blast door
constexpr float kBlastDoorZ = 4.0f;          // door plane (faces +Z)
// Every underground room lies inside this footprint (x0, z0, x1, z1).
constexpr float kBunkerMinX = -5.5f, kBunkerMaxX = 5.5f, kBunkerMinZ = -8.5f, kBunkerMaxZ = 4.0f;

// Outer apron sites (centre positions on the ground).
constexpr Vec3 kOuterGate{0.0f, kApronLevel, 34.0f};      // boom barrier just inside, guard booth east of it
constexpr Vec3 kHelipad{20.3f, kApronLevel, 17.0f};       // circular pad, radius kHelipadRadius
constexpr float kHelipadRadius = 5.0f;
constexpr Vec3 kRadarSite{29.0f, kApronLevel, -3.0f};
constexpr Vec3 kAaGunSite{21.0f, kApronLevel, -19.0f};
constexpr Vec3 kMotorPool{-22.0f, kApronLevel, -16.0f};   // tank parks here, facing the road (yaw 45 deg)
constexpr float kTankParkYawDeg = 45.0f;
constexpr Vec3 kBarracks{-27.0f, kApronLevel, 11.0f};     // Quonset huts around this point
constexpr Vec3 kFlagpole{-4.5f, kApronLevel, 27.0f};
constexpr Vec3 kFuelDepot{-6.0f, kApronLevel, -28.5f};
constexpr float kRoadHalfWidth = 2.2f;                     // inner gate (0, 19) -> outer gate, along +Z

// v8 barracks hall: an enterable, axis-aligned prefab barracks on the east apron (between the radar and the AA gun).
// Outer walls x [kHallX0, kHallX1], z [kHallZ0, kHallZ1]; the door is in the north wall, centred on a 1 m site-grid
// column (x = 23.5) so the soldiers' planner finds the way in. Inside: bunks (west), lockers (north-west), a mess
// table (north-east), a gym corner (south: mats, pull-up bar); outside north of it a small PT area.
namespace hall {
constexpr float kX0 = 18.6f, kX1 = 27.6f, kZ0 = -15.0f, kZ1 = -8.6f;
constexpr float kWall = 0.22f;                   // wall thickness
constexpr float kFloorY = kApronLevel + 0.12f;   // top of the floor slab
constexpr float kCeiling = 2.7f;                 // clear height above the floor
constexpr float kDoorX = 23.5f, kDoorW = 1.3f, kDoorH = 2.2f;
// inner faces
constexpr float kIX0 = kX0 + kWall, kIX1 = kX1 - kWall, kIZ0 = kZ0 + kWall, kIZ1 = kZ1 - kWall;
// bunk frames along the west wall (heads at the wall, length along +x), lower / upper mattress tops above the floor
constexpr float kBunkZ[3] = {-14.0f, -12.5f, -11.0f};
constexpr float kBunkX0 = kIX0 + 0.03f, kBunkLen = 2.0f, kBunkW = 0.95f;
constexpr float kBunkLow = 0.45f, kBunkHigh = 1.50f;
// mess table (long side along x) with a bench on each side
constexpr float kTableX = 25.7f, kTableZ = -11.0f, kTableLen = 2.2f, kTableDepth = 0.8f;
constexpr float kBenchNZ = -10.1f, kBenchSZ = -11.9f;
constexpr float kSeatX[2] = {25.1f, 26.3f};
// gym corner: pull-up bar (posts at kBarX +- 0.6), two exercise mats along x
constexpr float kBarX = 25.9f, kBarZ = -14.15f, kBarH = 2.25f;
constexpr float kMatX = 23.2f, kMatAZ = -14.1f, kMatBZ = -13.15f, kMatLen = 2.2f;
// lockers along the north wall west of the door
constexpr float kLockerX0 = 19.0f, kLockerX1 = 22.4f;
// outdoor PT area north of the hall: pull-up frame and two grass spots
constexpr float kPtBarX = 25.6f, kPtBarZ = -6.3f;
}  // namespace hall

// Intruder rovers spawn at the inner compound's edge (forest cover) and are destroyed by the UAV.
constexpr int kRoverCount = 3;      // v4: intruders built at start (v5: up to kMaxRovers, spawned in waves)
constexpr int kMaxRovers = 6;       // v5: intruder slots (most alive at once)
constexpr float kSpawnRadius = 16.8f;
// v5: what the intruders try to reach (assets of the camp inside the compound); a rover within
// kBreachRadius of its objective is a breach.
constexpr Vec3 kObjectives[5] = {{0.0f, 0.0f, 14.6f},    // bunker entrance ramp
                                 {-3.6f, 0.0f, 1.6f},    // bunker beacon / vent mast (the bunker below)
                                 {10.5f, 0.0f, 10.0f},   // watchtower
                                 {-13.0f, 0.0f, 6.0f},   // radio mast
                                 {-9.2f, 0.0f, -3.8f}};  // supply tent
constexpr float kBreachRadius = 2.0f;

}  // namespace ts::layout
