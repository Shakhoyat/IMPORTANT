// Decoration outside the 20 m playable disc: far ground plane, fog-faded hills, a pine treeline and a far
// radio tower with red aviation lights. Nothing here is an occluder or a UAV obstacle.
#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

constexpr float kBackdropInnerRadius = 23.0f;  // no backdrop part comes closer to the origin than this
constexpr float kFarGroundGap = 0.05f;         // far ground top sits this far below the terrain rim

std::unique_ptr<SceneNode> buildBackdrop();

}  // namespace ts
