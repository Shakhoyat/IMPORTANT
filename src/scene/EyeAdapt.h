// v9 M4: eye adaptation (pure CPU reference + the per-tick local exposure bias; tests/test_v9_m4.cpp).
//
// The renderer meters the frame (centre-weighted mean of log2 luminance, ShadersPostFx.cpp kReduceFrag) and adapts that
// mean over time in kAdaptFrag; the composite pass then sets
//
//     EV   = clamp(key - Lbar, -R, R + B)          (stops; key = the time of day's design key, R = 1.5)
//     exp  = base * 2^EV
//
// Lbar (the adapted mean) follows the metered mean L with an ASYMMETRIC exponential in the log domain:
//
//     Lbar += (L - Lbar) * (1 - e^(-dt / tau)),    tau = tauUp (0.35 s) when L > Lbar (the view got brighter),
//                                                  tau = tauDown (1.1 s) when L < Lbar (the view got darker).
//
// Physically: light adaptation (pupil constriction + cone bleaching) takes a fraction of a second, so stepping out into
// the sun dazzles only briefly; dark adaptation is slower (the pupil reopens over seconds, rods take minutes), so
// stepping into a room first looks dim and brightens over 1-2 s. Game engines compress the same asymmetry (Unreal's
// defaults: speed up 3, speed down 1 EV/s); here the ratio is 1.1 / 0.35 = 3.1.
//
// B is the LOCAL EXPOSURE BIAS of the indoor light volume the camera is in (WalkSpace IndoorLight::exposureBiasEv,
// weighted by how deep the camera is inside the volume): indoors the eye may open up B stops further than outdoors, so
// a window-lit room is not stuck R stops under the daylight key. The bias itself moves per simulation tick with the
// same asymmetric time constants (opening up = dark adaptation = slow; closing down = fast), so a walk through the
// doorway adapts visibly over about 1-2 s and is a pure function of the tick sequence (deterministic under --seed).
// Outdoors B = 0 and EV is exactly the v8 formula: static outdoor (or bunker) shots do not change.
#pragma once

#include <vector>

#include "ai/Occlusion.h"
#include "math/Math3D.h"

namespace ts {

class WalkSpace;

struct EyeAdaptParams {
    float tauUp = 0.35f;     // s: the view got brighter (light adaptation, fast)
    float tauDown = 1.1f;    // s: the view got darker (dark adaptation, slow)
    float evRange = 1.5f;    // stops the auto exposure may move either way (outdoors)
    float feather = 0.8f;    // m: the bias weight rises from 0 at the volume's face to 1 this far inside
    float thermalRate = 2.0f;   // 1/s: thermal AGC (symmetric, the sensor's gain control; unchanged from v3)
};

constexpr float kHallExposureBiasEv = 1.25f;   // the barracks hall: window light only (IndoorLight sky 0.24)

// Blend factor of one adaptation step (the shader's uRateUp / uRateDown).
float adaptBlend(float dt, float tau);
// One step of the asymmetric adaptation of the mean log2 luminance (CPU mirror of kAdaptFrag).
float adaptLogLuminance(float adapted, float metered, float dt, const EyeAdaptParams& p = EyeAdaptParams{});
// Exposure in stops relative to the base exposure (CPU mirror of the composite pass).
float exposureEv(float keyLog2, float adaptedLog, float biasEv, const EyeAdaptParams& p = EyeAdaptParams{});
// Time for the asymmetric adaptation to cover 'fraction' of a step (s): -tau ln(1 - fraction).
float adaptTime(float tau, float fraction);

// Target bias at a camera position: max over the indoor volumes of weight(depth inside) * exposureBiasEv.
float localExposureBiasTarget(const WalkSpace* ws, const Vec3& eye, const EyeAdaptParams& p = EyeAdaptParams{});

// The per-tick local exposure bias (App: one step per simulation tick).
class LocalExposureBias {
public:
    void reset(float ev = 0.0f) { ev_ = ev; }
    void step(float target, float dt, const EyeAdaptParams& p = EyeAdaptParams{});
    float ev() const { return ev_; }

private:
    float ev_ = 0.0f;
};

}  // namespace ts
