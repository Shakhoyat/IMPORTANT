#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

struct BunkerRig {
    std::unique_ptr<SceneNode> root;
    SceneNode* beaconBulb = nullptr;  // position of the point light
    SceneNode* fanCaps[2] = {nullptr, nullptr};
};

// Command bunker at the origin, drawn with Flat shading. Built from cubes, cylinders and spheres.
BunkerRig buildBunker();

}  // namespace ts
