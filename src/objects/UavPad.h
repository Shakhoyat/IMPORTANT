// v9 M5: the two UAV landing pads on the flat ground over the command bunker. Each pad is a flush concrete slab with a
// painted yellow ring, two low landing rails under the skids (the gimbal camera hangs between them, clear of the
// slab) and four green edge lights. Flush parts only: no UAV-avoidance obstacle, no occluder.
#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

constexpr int kUavPadCount = 2;
// Pad centres (x, z) on the bunker roof (flat, > 2 m from every standing part) and the parked heading (nose north).
constexpr float kUavPadX[kUavPadCount] = {-2.0f, 1.2f};
constexpr float kUavPadZ[kUavPadCount] = {-2.2f, -1.2f};
constexpr float kUavPadYawDeg = 0.0f;
constexpr float kUavPadSlab = 0.04f;        // slab thickness (top above the terrain)
constexpr float kUavPadRail = 0.02f;        // landing rail height above the slab
constexpr float kUavGearDrop = 0.132f;      // UAV root above the bottom of its skids

struct UavPadRig {
    std::unique_ptr<SceneNode> root;
    Vec3 touchdown[kUavPadCount];           // UAV root position when landed on pad i (skids on the rails)
    SceneNode* lights[kUavPadCount][4] = {};
};

UavPadRig buildUavPads();

}  // namespace ts
