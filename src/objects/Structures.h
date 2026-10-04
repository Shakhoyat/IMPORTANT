// Man-made props of the compound (perimeter fence and gate, watchtower, sandbag walls, camouflaged tent,
// supply caches, radio mast). Each function appends Builder parts (cube / sphere / cylinder / cone rows of
// the S-R-T table) below 'parent'. Solid parts are marked as occluders for the rover's hide logic.
#pragma once

#include <cstdint>
#include <vector>

#include "objects/Builder.h"

namespace ts {

// Circular area that vegetation placement keeps clear of (x, z centre and radius in metres).
struct Footprint {
    float x, z, radius;
};

// Posts every 18 degrees on r = kFenceRadius, three wire strands per bay, double gate where the track
// leaves the compound (+Z). Wires are not occluders or UAV obstacles.
void addPerimeterFence(Builder& b, SceneNode* parent);

// Steel watchtower: tapered legs, braces, platform with parapet, conical roof, ladder and a floodlight
// aimed along 'aimDir' (world direction, the floodlight head sits on the platform corner facing it).
void addWatchtower(Builder& b, SceneNode* parent, float x, float z, const Vec3& aimDir);

// Curved sandbag wall: 'rows' courses on an arc of 'radius' around (cx, cz) from startDeg to endDeg
// (angle measured from +X towards +Z). Each course has one bag fewer and sits half a bag offset.
void addSandbagArc(Builder& b, SceneNode* parent, float cx, float cz, float radius, float startDeg, float endDeg,
                   int bagsBottom, int rows);

// A-frame tent under a camouflage net held up by four poles.
void addCamoTent(Builder& b, SceneNode* parent, float x, float z, float yawDeg);

// Supply caches. variant 0: pallet with stacked crates and two drums; variant 1: fuel-drum cluster;
// variant 2: the original loose crate stack.
void addSupplyCache(Builder& b, SceneNode* parent, float x, float z, float yawDeg, int variant);

// Guyed lattice radio mast with an equipment shelter and blinking red aviation lights
// (leaves named "Aviation light", animated by the Scene).
void addRadioMast(Builder& b, SceneNode* parent, float x, float z);

// Practical lights of the compound: sodium lamp posts along the track, a fire barrel at the gate guard post
// and a lantern in the tent. Each is an emissive part whose material is also a light source.
void addPracticalLights(Builder& b, SceneNode* parent);

// Military dressing of the compound: HESCO blocks and jersey barriers at the inner gate, a sandbag fighting
// position with crates, a tracked-vehicle sign. Keeps clear of the bunker ramp strip (|x| < 2.5, z 3.5..13.5).
void addMilitaryDressing(Builder& b, SceneNode* parent);

// ---- v5 camp helpers (camouflage, revetments, light discipline) ------------------------------------------
// Camouflage nets hang at least this high above the ground (walkable areas stay free; a tank antenna is 3 m).
constexpr float kNetMinHeight = 3.2f;

// Sagging camouflage net on poles: a grid of thin cubes (Surface::Camo / Woven, about one cell in eight left
// open) whose height follows h(u, v) = poleHeight - sag sin(pi u) sin(pi v), each cube tilted to the slope.
// 'ground' = ground point below the net centre; the net is width (local X) x depth (local Z), rotated by yawDeg.
// Panels are named "Camo net panel"; their lowest point stays >= kNetMinHeight above 'ground' (poleHeight - sag
// >= 3.5 is enforced by clamping). Parts are not occluders.
SceneNode* addCamoNet(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float width, float depth,
                      float poleHeight, float sag, uint32_t seed, float holeHalfX = 0.0f, float holeZ0 = 0.0f,
                      float holeZ1 = 0.0f);
// holeHalfX > 0: panels whose centre lies in |x| < holeHalfX, holeZ0 < z < holeZ1 (net-local metres) are left
// out, e.g. the aperture above a launcher that raises its canisters through the net.

// U-shaped earth revetment (open at the rear, local -Z; the vehicle faces +Z): inner half width 'halfW', inner
// half depth 'halfD', crest height 'height'. Each wall = core cube + two sloped cubes (rotated about the wall
// axis) + a sandbag crest, so the profile is a trapezoid built from rotated cubes only.
SceneNode* addRevetment(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float halfW, float halfD,
                        float height);

// Cut branches and a small bush leaning against a vehicle flank (local x = side * dist from 'ground').
void addCutBranches(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float dist, float length,
                    uint32_t seed);

}  // namespace ts
