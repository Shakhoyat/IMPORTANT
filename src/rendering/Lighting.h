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

// Sky, fog and exposure state of the time of day (filled by DayCycle::applyTo). The defaults are the v5 night
// literals exactly, so a scene without a day cycle renders as before.
struct Atmosphere {
    float day = 0.0f, night = 1.0f;     // blend weights from the sun elevation
    float solar = 0.0f;                 // thermal solar-loading weight 0..1
    float practicalScale = 1.0f;        // lamps / practicals / beacon (0 by day)
    bool beamsOn = true;                // UAV searchlights
    Vec3 sunDir{0, -1, 0};              // direction sunlight travels (unquantised, for the sky)
    Vec3 sunColor{0, 0, 0};             // sky-shader disc / halo radiance
    Vec3 skyZenith{0.030f, 0.040f, 0.075f}, groundBounce{0.010f, 0.010f, 0.011f};
    Vec3 skyHorizon{0.030f, 0.040f, 0.075f};   // v8: day sky near the horizon (side ambient, reflections); unused at night
    Vec3 fogColor{0.014f, 0.018f, 0.030f}, fogAmbient{0.006f, 0.008f, 0.014f};
    float fogDensity = 0.018f, volFogDensity = 0.022f;
    float exposureKeyLog2 = -5.2f;      // auto-exposure key (mean log2 luminance)
    float purkinje = 1.0f;              // scotopic (night vision) colour shift weight
    // v6 additions (defaults again = the v5 night)
    bool keyIsSun = false;              // the key light (moon slot) is the sun: cloud shadows, solar loading
    float twilight = 0.0f;              // warm horizon glow around sunrise / sunset 0..1
    // v9 M8: the day ambient from the analytic sky (math/SkyModel.h): order-2 SH of irradiance / pi from the Preetham
    // sky + the sunlit ground below the horizon (all zero at night, where the v5 hemisphere is used).
    Vec3 skySH[9];
};

struct LightRig {
    DirectionalLight moon;  // L0 key directional light: the sun by day, the moon by night
    PointLight beacon;
    SpotLight spot;
    Vec3 ambient{0.06f, 0.065f, 0.085f};
    Atmosphere atmo;
};

}  // namespace ts
