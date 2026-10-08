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
    return dopplerRatio(listenerPos, listenerVel, srcPos, srcVel, 0.5f, 0.9f, 1.1f);
}

float dopplerRatio(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& srcPos, const Vec3& srcVel, float scale,
                   float lo, float hi) {
    const Vec3 d = srcPos - listenerPos;
    const float len = length(d);
    if (len < 1e-3f) return 1.0f;
    const Vec3 dir = d / len;                          // listener -> source
    const float c = 343.0f;
    const float vs = dot(srcVel, dir);                 // source speed away from the listener
    const float vl = dot(listenerVel, dir);            // listener speed towards the source
    const float physical = (c + vl) / std::max(c + vs, 40.0f);
    const float r = 1.0f + scale * (physical - 1.0f);
    return std::isfinite(r) ? clampf(r, lo, hi) : 1.0f;
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

float itdSeconds(float lateral) {
    const float th = std::asin(clampf(lateral, -1.0f, 1.0f));
    return kHeadRadius / 343.0f * (th + std::sin(th));
}

SpatialHrtf computeHrtf(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& right, const Vec3& forward,
                        const Vec3& srcPos, const Vec3& srcVel, float refDistance, float gain, float dopplerScale) {
    SpatialHrtf s;
    const Vec3 d = srcPos - listenerPos;
    s.distance = length(d);
    const Vec3 dir = s.distance > 1e-3f ? d / s.distance : Vec3{0.0f, 0.0f, 0.0f};
    // Inside 1 m the direction is ill-defined: fade every interaural cue to the centre.
    const float near = clampf(s.distance, 0.0f, 1.0f);
    s.lateral = clampf(dot(dir, right), -1.0f, 1.0f) * near;
    s.front = s.distance > 1e-3f ? clampf(dot(dir, forward), -1.0f, 1.0f) : 1.0f;
    // Broadband ILD: a softened equal-power pan (about 11 dB at 90 degrees); the head shadow adds the rest at
    // high frequencies.
    float l, r;
    panGains(0.65f * s.lateral, l, r);
    const float rear = s.front < 0.0f ? 1.0f - 0.12f * (-s.front) * near : 1.0f;
    const float att = gain * attenuation(s.distance, refDistance) * rear;
    s.gainL = l * att;
    s.gainR = r * att;
    const float itd = itdSeconds(s.lateral) * static_cast<float>(kSampleRate);   // > 0: right ear leads
    s.delayL = itd > 0.0f ? itd : 0.0f;
    s.delayR = itd < 0.0f ? -itd : 0.0f;
    const float a = std::fabs(s.lateral);
    const float shadow = 20000.0f * std::pow(0.09f, a);   // 20 kHz on axis -> 1.8 kHz at 90 degrees
    s.shadowHzL = s.lateral > 0.0f ? shadow : 20000.0f;
    s.shadowHzR = s.lateral < 0.0f ? shadow : 20000.0f;
    s.rearHz = s.front < 0.0f ? 20000.0f * std::pow(0.3f, -s.front * near) : 20000.0f;   // 6 kHz straight behind
    s.airHz = airCutoffHz(s.distance);
    s.doppler = dopplerRatio(listenerPos, listenerVel, srcPos, srcVel, dopplerScale, 0.8f, 1.25f);
    return s;
}

}  // namespace ts::audio
