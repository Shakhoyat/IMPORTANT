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

// Intruder rovers spawn at the inner compound's edge (forest cover) and are destroyed by the UAV.
constexpr int kRoverCount = 3;

}  // namespace ts::layout
