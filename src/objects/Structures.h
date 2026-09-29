// Man-made props of the compound (perimeter fence and gate, watchtower, sandbag walls, camouflaged tent,
// supply caches, radio mast). Each function appends Builder parts (cube / sphere / cylinder / cone rows of
// the S-R-T table) below 'parent'. Solid parts are marked as occluders for the rover's hide logic.
#pragma once

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

}  // namespace ts
