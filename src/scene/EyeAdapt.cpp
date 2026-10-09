#include "scene/EyeAdapt.h"

#include <algorithm>
#include <cmath>

#include "scene/WalkSpace.h"

namespace ts {

float adaptBlend(float dt, float tau) {
    if (dt <= 0.0f) return 0.0f;
    if (tau <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-dt / tau);
}

float adaptLogLuminance(float adapted, float metered, float dt, const EyeAdaptParams& p) {
    const float k = adaptBlend(dt, metered > adapted ? p.tauUp : p.tauDown);
    return adapted + (metered - adapted) * k;
}

float exposureEv(float keyLog2, float adaptedLog, float biasEv, const EyeAdaptParams& p) {
    return clampf(keyLog2 - adaptedLog, -p.evRange, p.evRange + std::max(biasEv, 0.0f));
}

float adaptTime(float tau, float fraction) {
    fraction = clampf(fraction, 0.0f, 0.999999f);
    return -tau * std::log(1.0f - fraction);
}

float localExposureBiasTarget(const WalkSpace* ws, const Vec3& eye, const EyeAdaptParams& p) {
    if (!ws) return 0.0f;
    const std::vector<Box>& vol = ws->indoorVolumes();
    const std::vector<IndoorLight>& light = ws->indoorLights();
    float best = 0.0f;
    for (size_t i = 0; i < vol.size() && i < light.size(); ++i) {
        if (light[i].exposureBiasEv <= 0.0f) continue;
        const Box& b = vol[i];
        // depth inside the box: distance to the nearest face (negative outside)
        const float d = std::min({eye.x - b.min.x, b.max.x - eye.x, eye.y - b.min.y, b.max.y - eye.y, eye.z - b.min.z, b.max.z - eye.z});
        if (d <= 0.0f) continue;
        const float t = clampf(d / std::max(p.feather, 1e-3f), 0.0f, 1.0f), w = t * t * (3.0f - 2.0f * t);
        best = std::max(best, w * light[i].exposureBiasEv);
    }
    return best;
}

void LocalExposureBias::step(float target, float dt, const EyeAdaptParams& p) {
    // Opening up (a larger bias) is dark adaptation: slow; closing down is light adaptation: fast.
    ev_ += (target - ev_) * adaptBlend(dt, target > ev_ ? p.tauDown : p.tauUp);
}

}  // namespace ts
