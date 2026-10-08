#pragma once

#include <memory>

#include "scene/SceneNode.h"

namespace ts {

struct UavRig {
    std::unique_ptr<SceneNode> root;       // flight position / attitude
    SceneNode* rotors[4] = {nullptr, nullptr, nullptr, nullptr};
    SceneNode* gimbalYaw = nullptr;        // level 1 of the searchlight hierarchy
    SceneNode* gimbalPitch = nullptr;      // level 2: lamp + thermal camera, looks along local +Z
    SceneNode* beam = nullptr;             // visible light cone, length set every frame
    SceneNode* lens = nullptr;             // v8: searchlight lens (emissive only while the searchlight runs)
    SceneNode* missiles[4] = {nullptr, nullptr, nullptr, nullptr};  // rail-mounted rounds (hidden after launch)
};
constexpr int kUavMissiles = 4;

constexpr float kLampFrontOffset = 0.045f;  // lens position along the pitch node's +Z
constexpr float kSpotOuterDeg = 14.0f;
constexpr float kSpotInnerDeg = 9.0f;

// Quadcopter with a two-axis searchlight/camera gimbal. Forward is local +Z. Phong shading.
UavRig buildUav();

}  // namespace ts
