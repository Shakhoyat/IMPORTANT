#include "audio/Spatial.h"

#include <algorithm>
#include <cmath>

#include "audio/Dsp.h"

namespace ts::audio {

void panGains(float pan, float& left, float& right) {
    const float p = clampf(pan, -1.0f, 1.0f);
    const float a = (p + 1.0f) * 0.25f * kPi;  // 0 .. pi/2
    left = std::cos(a);
    right = std::sin(a);
}

float attenuation(float distance, float refDistance, float rolloff) {
    const float d = std::max(distance, refDistance);
    return std::pow(refDistance / d, rolloff);
}

float airCutoffHz(float distance) { return std::max(1200.0f, 16000.0f * std::exp(-std::max(distance, 0.0f) / 90.0f)); }

float dopplerRatio(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& srcPos, const Vec3& srcVel) {
    const Vec3 d = srcPos - listenerPos;
    const float len = length(d);
    if (len < 1e-3f) return 1.0f;
    const Vec3 dir = d / len;                          // listener -> source
    const float c = 343.0f;
    const float vs = dot(srcVel, dir);                 // source speed away from the listener
    const float vl = dot(listenerVel, dir);            // listener speed towards the source
    const float physical = (c + vl) / std::max(c + vs, 40.0f);
    return clampf(1.0f + 0.5f * (physical - 1.0f), 0.9f, 1.1f);
}

Spatial computeSpatial(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& right, const Vec3& srcPos,
                       const Vec3& srcVel, float refDistance, float gain) {
    Spatial s;
    const Vec3 d = srcPos - listenerPos;
    s.distance = length(d);
    // Close to the source the direction is ill-defined: fade the pan towards the centre inside 1 m.
    float pan = s.distance > 1e-3f ? dot(d / s.distance, right) : 0.0f;
    pan *= clampf(s.distance, 0.0f, 1.0f);
    float l, r;
    panGains(pan, l, r);
    const float att = gain * attenuation(s.distance, refDistance);
    s.gainL = l * att;
    s.gainR = r * att;
    s.lpAlpha = onePoleAlpha(airCutoffHz(s.distance));
    s.doppler = dopplerRatio(listenerPos, listenerVel, srcPos, srcVel);
    return s;
}

}  // namespace ts::audio
