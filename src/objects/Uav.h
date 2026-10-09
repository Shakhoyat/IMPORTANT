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
    // v9 M5: the propeller blade of each rotor and the motion-blur disc swept by it (Scene::applyRotorBlur fades
    // the blade out and the disc in with the rotor speed).
    SceneNode* blades[4] = {nullptr, nullptr, nullptr, nullptr};
    SceneNode* blurDiscs[4] = {nullptr, nullptr, nullptr, nullptr};
};
constexpr int kUavMissiles = 4;

constexpr float kLampFrontOffset = 0.045f;  // lens position along the pitch node's +Z
constexpr float kSpotOuterDeg = 14.0f;
constexpr float kSpotInnerDeg = 9.0f;
// v9 M5 rotor blur: full rotor speed (rev/s) and the band over which the blade fades out and the blur disc fades in.
constexpr float kRotorFullRps = 40.0f;
constexpr float kRotorBlurStartRps = 8.0f, kRotorBlurFullRps = 22.0f;
constexpr float kRotorDiscOpacity = 0.32f;   // blur disc opacity at full blur
// Blur amount 0..1 of a rotor turning at 'rps' rev/s (smoothstep over the band above).
float rotorBlur(float rps);

// Quadcopter with a two-axis searchlight/camera gimbal. Forward is local +Z. Phong shading.
UavRig buildUav();

}  // namespace ts
