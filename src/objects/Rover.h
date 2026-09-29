#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

struct RoverRig {
    std::unique_ptr<SceneNode> root;  // translation / yaw / pitch set every frame by the hide controller
    SceneNode* wheelsLeft[3] = {nullptr, nullptr, nullptr};
    SceneNode* wheelsRight[3] = {nullptr, nullptr, nullptr};
    SceneNode* scannerPivot = nullptr;
    SceneNode* irSensors[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
};

constexpr float kRoverWheelRadius = 0.08f;
constexpr float kRoverTrackWidth = 0.50f;  // distance between left and right wheel centres

// Small six-wheeled ground robot (UGV). Forward is local +Z. Phong shading.
RoverRig buildRover();

}  // namespace ts
