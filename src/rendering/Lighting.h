// The three simultaneous light sources of the proposal.
#pragma once

#include "math/Math3D.h"

namespace ts {

struct DirectionalLight {  // L0: moonlight
    Vec3 direction{0, -1, 0};
    Vec3 color{0.55f, 0.62f, 0.80f};
    float intensity = 0.95f;
    bool enabled = true;
};

struct PointLight {  // L1: bunker beacon, attenuation 1 / (1 + kl d + kq d^2)
    Vec3 position{0, 3.7f, 0};
    Vec3 color{1.0f, 0.25f, 0.10f};
    float intensity = 1.6f;
    float linear = 0.35f, quadratic = 0.45f;
    bool enabled = true;
};

struct SpotLight {  // L2: UAV searchlight, smooth edge between inner and outer cone
    Vec3 position{0, 4, 0};
    Vec3 direction{0, -1, 0};
    Vec3 color{1.0f, 0.95f, 0.82f};
    float intensity = 5.0f;
    float innerDeg = 9.0f, outerDeg = 14.0f;
    float linear = 0.05f, quadratic = 0.03f;
    bool enabled = true;
};

struct LightRig {
    DirectionalLight moon;
    PointLight beacon;
    SpotLight spot;
    Vec3 ambient{0.06f, 0.065f, 0.085f};
};

}  // namespace ts
