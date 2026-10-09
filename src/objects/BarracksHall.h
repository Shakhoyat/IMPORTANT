// v8 enterable barracks hall (objects/BaseLayout.h, namespace layout::hall): a prefab barracks with walls, window and
// door openings, a ceiling under a gable roof, and a furnished interior -- bunk beds, lockers, a mess table with
// benches, a gym corner (pull-up bar, mats, dumbbell rack, punching bag), rifle rack, map board, ceiling lights --
// plus a small PT area outside. Built from the unit primitives like everything else. The walk data (floor with its
// ceiling, the doorway) and the indoor light volume go to the scene's WalkSpace; the soldiers' planner finds the way
// in through the door because the openings are real gaps between the wall parts.
#pragma once

#include <memory>
#include <vector>

#include "ai/Occlusion.h"
#include "objects/HingedDoor.h"
#include "scene/SceneNode.h"
#include "scene/WalkSpace.h"

namespace ts {

struct HallRig {
    std::unique_ptr<SceneNode> root;
    std::vector<WalkFloor> floors;   // interior (ceiling 2.7 m) and the doorway
    Box indoor;                      // indoor light volume (windows: some sky light and the sun come in)
    std::unique_ptr<HingedDoor> door;   // v9 M4: the hinged door in the north doorway (its leaf is under 'root')
};

// v9 M4: the hall door's hinge (east jamb, outer face; opens outwards through 180 deg onto the wall, where the v8 leaf
// stood) -- shared by the scene build and the tests.
HingeParams hallDoorHinge();

HallRig buildBarracksHall();

}  // namespace ts
