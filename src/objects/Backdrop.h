// Decoration outside the 36 m terrain mesh (compound + apron): far ground plane, fog-faded hills, a pine
// treeline (38-45 m) and a far radio tower with red aviation lights (100 m). Nothing here is an occluder or a UAV obstacle.
#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

constexpr float kBackdropInnerRadius = 37.0f;  // no backdrop part comes closer to the origin than this (terrain mesh r = 36)
constexpr float kFarGroundGap = 0.05f;         // far ground top sits this far below the terrain rim

std::unique_ptr<SceneNode> buildBackdrop();

}  // namespace ts
