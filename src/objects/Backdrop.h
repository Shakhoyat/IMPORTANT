// Decoration outside the 36 m terrain mesh (compound + apron): far ground plane, fog-faded hills, a pine
// treeline (38-45 m) and a far radio tower with red aviation lights (100 m). Nothing here is an occluder or a UAV obstacle.
#pragma once

#include <memory>
#include <vector>

#include "scene/SceneNode.h"

namespace ts {

constexpr float kBackdropInnerRadius = 37.0f;  // no backdrop part comes closer to the origin than this (terrain mesh r = 36)
constexpr float kFarGroundGap = 0.05f;         // far ground top sits this far below the terrain rim

std::unique_ptr<SceneNode> buildBackdrop();

// v7.1 WP-T: the treeline pines (ground position, height, yaw) the backdrop builds; the open-world walker collides
// with their crowns (crown base radius 0.28 * height).
struct TreelinePine {
    float x = 0.0f, z = 0.0f, height = 8.0f, yawDeg = 0.0f;
};
std::vector<TreelinePine> treelinePines();

}  // namespace ts
