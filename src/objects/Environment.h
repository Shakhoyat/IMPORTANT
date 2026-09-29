#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

// Direction the moonlight travels (from the moon towards the ground).
Vec3 moonLightDirection();

// Everything inside the 20 m disc that is not the bunker or an agent: perimeter fence and gate, watchtower,
// sandbag walls, camouflaged tent, supply caches, radio mast, forest patches, bushes, rocks, and the moon.
// Gouraud shading (the moon is emissive).
std::unique_ptr<SceneNode> buildEnvironment();

}  // namespace ts
