// v9 M8: display transforms of the visible view (pure math, CPU mirror of the composite shader, unit-tested).
//
// AgX (Troy Sobotka, 2022; the Blender 4 default; polynomial fit of the sigmoid by Benjamin Wrensch):
//   1. inset matrix: pulls every primary towards the achromatic axis ("rotate + inset" of the working gamut), so very
//      bright saturated light desaturates towards white instead of clipping one channel,
//   2. log2 encoding of each channel over [kAgxMinEv, kAgxMaxEv] stops around 0.18 (16.5 stops),
//   3. a sigmoid in that log space (the "base contrast" look: the plain curve, no CDL; 6th-order polynomial fit),
//   4. outset matrix (restores the saturation of the non-clipped range), 5. display EOTF 2.2 -> display-linear.
// ACES (Stephen Hill's fit of the RRT + ODT, sRGB) is kept as the option: its per-channel curve saturates each channel
// separately, so saturated highlights hit 1.0 in one channel (hue skews: orange -> yellow, blue -> purple).
#pragma once

#include "math/Math3D.h"

namespace ts::tonemap {

enum class Operator { AgX = 0, Aces = 1 };

// AgX working-space matrices (column-major, as in GLSL mat3(...)); outset = inverse of inset.
constexpr float kAgxInset[9] = {0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
                                0.0784335999999992f, 0.878468636469772f, 0.0784336f,
                                0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f};
constexpr float kAgxOutset[9] = {1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
                                 -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
                                 -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f};
constexpr float kAgxMinEv = -12.47393f, kAgxMaxEv = 4.026069f;   // log2 range of the encoding
// Sigmoid polynomial coefficients, highest power first: x^6 .. x^0.
constexpr float kAgxPoly[7] = {15.5f, -40.14f, 31.96f, -6.868f, 0.4298f, 0.1191f, -0.00232f};

// Pre-exposure of each operator (scene-linear multiplier before the curve). ACES keeps the v3-v8 value; AgX maps
// mid grey 0.18 to the same display value as that ACES setting (see T382).
constexpr float kAcesExposure = 1.6f, kAgxExposure = 1.0f;
// Day grade before the curve (composite): log-space contrast around mid grey and extra saturation. ACES keeps the v8
// values; AgX's base-contrast sigmoid is ~2/3 as steep as the ACES fit at mid grey and lifts the shadows, so its day
// grade carries more contrast (the curve itself stays the base-contrast AgX; the night grade is unchanged).
constexpr float kAcesDayContrast = 1.10f, kAcesDaySaturation = 0.10f;
constexpr float kAgxDayContrast = 1.35f, kAgxDaySaturation = 0.22f;

// Weight of AgX against the ACES curve for a daylight factor (Atmosphere::day): AgX by day, the v8 ACES curve at night.
// AgX's log-encoded toe lifts the moonlit range by ~1.3 stops (0.01 linear: 0.07 display vs 0.03), which the night
// grade was not authored for (night SSIM vs v8 0.88-0.90 with AgX, >= 0.997 with ACES); the two cross-fade through
// twilight with a smoothstep so the display transform never jumps.
float agxDayWeight(float dayWeight);
// Day grade contrast / saturation for an AgX weight (linear between the ACES and the AgX values).
float dayContrast(float agxWeight);
float daySaturation(float agxWeight);

float agxSigmoid(float x);                                  // x = log-encoded 0..1 -> display-encoded 0..1
Vec3 agx(const Vec3& linear);                               // scene-linear -> display-linear (0..1)
Vec3 acesFitted(const Vec3& linear);                        // scene-linear -> display-linear, clamped to 0..1
float linearToSrgb(float c);                                // sRGB OETF
// Full display path of one operator: pre-exposure, curve, sRGB encoding (0..1).
Vec3 display(const Vec3& linear, Operator op);
// Hue angle (degrees, 0..360) of an sRGB-encoded colour in the opponent plane (a = R - (G + B) / 2, b = (G - B) * sqrt(3) / 2).
float hueDeg(const Vec3& c);

}  // namespace ts::tonemap
