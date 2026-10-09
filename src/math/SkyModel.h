// v9 M8: analytic daylight sky (Preetham, Shirley & Smits 1999, "A Practical Analytic Model for Daylight") and the
// ambient light it gives (order-2 spherical-harmonic irradiance, Ramamoorthi & Hanrahan 2001). Pure CPU math: the sky
// shader, the aerial perspective, the sky reflections and the ambient term evaluate the same model on the GPU with the
// parameters computed here, so the ambient light always matches the sky that is drawn.
//
// Perez luminance / chromaticity distribution over the sky (theta = zenith angle of the view direction, gamma = angle
// between the view direction and the sun):
//     F(theta, gamma) = (1 + A e^(B / cos theta)) (1 + C e^(D gamma) + E cos^2 gamma)
//     Y(theta, gamma) = Y_z F(theta, gamma) / F(0, theta_s)     (the same for the chromaticities x and y)
// A..E, the zenith luminance Y_z (kcd/m^2) and the zenith chromaticity follow from the turbidity T and theta_s.
#pragma once

#include "math/Math3D.h"

namespace ts {

struct SkyModel {
    Vec3 perez[5];        // A..E of the Perez function; x: luminance Y, y: chromaticity x, z: chromaticity y
    Vec3 zenith;          // (Y_z, x_z, y_z), each divided by its F(0, theta_s): radiance = zenith * F(theta, gamma)
    Vec3 toSun{0, 1, 0};  // unit vector to the sun (elevation clamped to kSkyMinSunElevDeg for the model)
    float turbidity = 3.0f;
    float scale = 1.0f;   // kcd/m^2 -> the renderer's linear HDR radiance units
};

constexpr float kSkyTurbidity = 2.5f;        // clear sky (2 = very clear, 3 = light haze, 6 = hazy)
constexpr float kSkyMinSunElevDeg = 0.5f;    // the Perez fit is valid for the sun above the horizon
// kcd/m^2 -> HDR radiance: calibrated so the noon ambient on an up-facing face (irradiance / pi of this sky) is about
// the v8 day ambient (luminance ~0.45), keeping the exposure the scene was tuned for (T383 checks the noon value).
constexpr float kSkyRadianceScale = 0.05f;
// Effective albedo of the ground below the horizon (grass / soil / gravel, partly self-shadowed): the ground radiance
// that lights down-facing surfaces is albedo x (sun + sky irradiance) / pi.
constexpr Vec3 kSkyGroundAlbedo{0.070f, 0.064f, 0.050f};

SkyModel preethamSky(const Vec3& toSun, float turbidity = kSkyTurbidity, float scale = kSkyRadianceScale);
// Linear sRGB radiance of the clear sky in direction dir (the sun disc itself is not included; dir.y is clamped to
// just above the horizon).
Vec3 skyRadiance(const SkyModel& m, const Vec3& dir);
float perezF(float A, float B, float C, float D, float E, float cosTheta, float gamma);
Vec3 xyYToLinearSrgb(float x, float y, float Y);

// Ambient (irradiance / pi) a face with normal n receives from the sky above the horizon and a uniform ground below,
// projected onto the 9 real spherical harmonics of order <= 2 (cosine lobe convolution: A0 = pi, A1 = 2 pi / 3,
// A2 = pi / 4). evalAmbient(sh, n) = sum c_i Y_i(n).
struct AmbientSH {
    Vec3 c[9];
};
float shBasis(int i, const Vec3& n);
AmbientSH ambientSH(const SkyModel& m, const Vec3& groundRadiance, int thetaSteps = 24, int phiSteps = 48);
Vec3 evalAmbient(const AmbientSH& sh, const Vec3& n);
// The same quantity by direct numerical integration (1 / pi) * integral L(w) max(n . w, 0) dw (tests).
Vec3 ambientBruteForce(const SkyModel& m, const Vec3& groundRadiance, const Vec3& n, int thetaSteps = 96, int phiSteps = 192);
// Mean sky radiance in a band just above the horizon (elevation 2..8 deg), averaged over the azimuth.
Vec3 horizonRadiance(const SkyModel& m);

}  // namespace ts
