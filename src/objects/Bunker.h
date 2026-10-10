// Underground command bunker (v4): entrance trench with ramp, blast doors, tunnel, command room (CIC), generator
// room, bunk room and an emergency exit shaft; on the surface only vent stacks, the beacon mast, an antenna and
// periscope mast, the escape hatch and a few sandbags. Everything is built from lab primitives, Flat shading.
#pragma once

#include <memory>

#include "scene/Controllable.h"
#include "scene/Door.h"
#include "scene/SceneNode.h"
#include "scene/WalkSpace.h"

namespace ts {

struct BunkerRig {
    std::unique_ptr<SceneNode> root;
    SceneNode* beaconBulb = nullptr;  // point light L1 on the beacon mast above the bunker
    SceneNode* fanCaps[2] = {nullptr, nullptr};  // spinning fan groups on the two surface vent stacks (spin about local Z)
    std::unique_ptr<Controllable> doors;  // blast doors (possessable); its root is already a child of 'root'
    std::unique_ptr<Controllable> generatorDoor;   // v9 M4: hinged door of the generator room (scene/Door.h)
    WalkSpace walk;                       // ramp, tunnel and room floors, wall boxes, indoor volumes
};

namespace bunker {
constexpr float kLeafWidth = 1.30f;       // one blast door leaf (hinge on the jamb to the centre line)
constexpr float kDoorOpeningHalf = 1.3f;  // clear opening 2.6 m wide, 2.6 m high
constexpr float kDoorTravelSeconds = 4.0f;
constexpr Vec3 kCicCentre{0.0f, -3.4f, -4.2f};  // free floor in the command room (feet position)
}  // namespace bunker

// v9 M4: hinge of the generator room door (south jamb of the doorway x 1.5 .. 1.8, z 0.6 .. 1.8 in the tunnel's east
// wall; opens 90 deg into the room). The entrance itself keeps its sliding blast doors: a 0.35 m armoured leaf in wall
// pockets has no room for a hinged leaf.
HingeParams generatorDoorHinge();

// Command bunker at the origin, drawn with Flat shading. Built from cubes, cylinders, spheres and cones.
BunkerRig buildBunker();

}  // namespace ts
