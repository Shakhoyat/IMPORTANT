// Spatialisation maths (pure functions): equal-power pan, inverse-distance attenuation with a near clamp,
// air-absorption low-pass and a light Doppler ratio.
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

}  // namespace ts::audio
