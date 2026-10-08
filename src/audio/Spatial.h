// Spatialisation maths (pure functions): equal-power pan, inverse-distance attenuation with a near clamp,
// air-absorption low-pass and a light Doppler ratio. v7: HRTF-lite (interaural time and level differences, head
// shadow and rear pinna low-pass) and a scalable Doppler ratio for the game mixer.
#pragma once

#include "math/Math3D.h"

namespace ts::audio {

struct Spatial {
    float gainL = 1.0f, gainR = 1.0f;  // distance attenuation and pan combined
    float lpAlpha = 1.0f;              // one-pole low-pass coefficient (air absorption)
    float doppler = 1.0f;              // pitch ratio
    float distance = 0.0f;
};

// Equal-power pan law: pan in [-1 (left), +1 (right)] -> gains with gL^2 + gR^2 = 1.
void panGains(float pan, float& left, float& right);
// Inverse distance: ref / max(d, ref)^rolloff; 1 inside the near clamp, monotonically non-increasing.
float attenuation(float distance, float refDistance = 3.0f, float rolloff = 1.0f);
// Air absorption cutoff in Hz, decreasing with distance (about 16 kHz close, 1.2 kHz minimum).
float airCutoffHz(float distance);
// Light Doppler pitch ratio (0.5 x physical, clamped to 0.9..1.1) for a source moving relative to the listener.
float dopplerRatio(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& srcPos, const Vec3& srcVel);

// Everything at once for one source. 'right' is the listener's right vector (unit).
Spatial computeSpatial(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& right, const Vec3& srcPos,
                       const Vec3& srcVel, float refDistance, float gain);

// ---- v7 HRTF-lite ----
constexpr float kHeadRadius = 0.0875f;   // m (Woodworth spherical head)
// Woodworth ITD (s) for a lateral angle: (a / c)(theta + sin theta), theta = asin(lateral), lateral = dir . right in
// [-1, 1] (positive: the right ear leads). Max ~0.66 ms at 90 degrees.
float itdSeconds(float lateral);
// Doppler ratio with a scale on the physical shift (1 = physical) and a clamp. The 4-argument version above is
// dopplerRatio(..., 0.5, 0.9, 1.1).
float dopplerRatio(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& srcPos, const Vec3& srcVel, float scale,
                   float lo, float hi);

struct SpatialHrtf {
    float gainL = 0.7071f, gainR = 0.7071f;   // broadband ILD x distance attenuation x gain
    float delayL = 0.0f, delayR = 0.0f;       // per-ear delay in samples (the far ear lags)
    float shadowHzL = 20000.0f, shadowHzR = 20000.0f;  // head-shadow low-pass per ear
    float rearHz = 20000.0f;                  // pinna low-pass for sources behind the head
    float airHz = 16000.0f;                   // air absorption (airCutoffHz)
    float doppler = 1.0f, distance = 0.0f;
    float lateral = 0.0f, front = 1.0f;       // dir . right, dir . forward (for inspection / tests)
};
// Listener basis: right / forward unit vectors. Doppler: physical x dopplerScale, clamped to [0.8, 1.25].
SpatialHrtf computeHrtf(const Vec3& listenerPos, const Vec3& listenerVel, const Vec3& right, const Vec3& forward,
                        const Vec3& srcPos, const Vec3& srcVel, float refDistance, float gain, float dopplerScale = 1.0f);

}  // namespace ts::audio
