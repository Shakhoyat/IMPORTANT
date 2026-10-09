#include "math/ToneMap.h"

#include <algorithm>

namespace ts::tonemap {

namespace {

Vec3 mat3Mul(const float* m, const Vec3& v) {   // column-major 3x3 * v
    return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z, m[2] * v.x + m[5] * v.y + m[8] * v.z};
}

float encodeLog(float v) {
    const float l = std::log2(std::max(v, 1e-10f));
    return (clampf(l, kAgxMinEv, kAgxMaxEv) - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
}

}  // namespace

float agxDayWeight(float dayWeight) {
    const float t = clampf(dayWeight, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float dayContrast(float w) { return lerpf(kAcesDayContrast, kAgxDayContrast, clampf(w, 0.0f, 1.0f)); }
float daySaturation(float w) { return lerpf(kAcesDaySaturation, kAgxDaySaturation, clampf(w, 0.0f, 1.0f)); }

float agxSigmoid(float x) {
    float s = 0.0f;
    for (float c : kAgxPoly) s = s * x + c;   // Horner, x^6 .. x^0
    return s;
}

Vec3 agx(const Vec3& linear) {
    Vec3 v = mat3Mul(kAgxInset, {std::max(linear.x, 0.0f), std::max(linear.y, 0.0f), std::max(linear.z, 0.0f)});
    v = {agxSigmoid(encodeLog(v.x)), agxSigmoid(encodeLog(v.y)), agxSigmoid(encodeLog(v.z))};
    v = mat3Mul(kAgxOutset, v);
    auto eotf = [](float c) { return std::pow(clampf(c, 0.0f, 1.0f), 2.2f); };
    return {eotf(v.x), eotf(v.y), eotf(v.z)};
}

Vec3 acesFitted(const Vec3& linear) {
    static const float inM[9] = {0.59719f, 0.07600f, 0.02840f, 0.35458f, 0.90834f, 0.13383f, 0.04823f, 0.01566f, 0.83777f};
    static const float outM[9] = {1.60475f, -0.10208f, -0.00327f, -0.53108f, 1.10813f, -0.07276f, -0.07367f, -0.00605f, 1.07602f};
    const Vec3 v = mat3Mul(inM, linear);
    auto rrt = [](float x) {
        const float a = x * (x + 0.0245786f) - 0.000090537f;
        const float b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
        return a / b;
    };
    const Vec3 o = mat3Mul(outM, {rrt(v.x), rrt(v.y), rrt(v.z)});
    return {clampf(o.x, 0.0f, 1.0f), clampf(o.y, 0.0f, 1.0f), clampf(o.z, 0.0f, 1.0f)};
}

float linearToSrgb(float c) {
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Vec3 display(const Vec3& linear, Operator op) {
    const Vec3 d = op == Operator::AgX ? agx(linear * kAgxExposure) : acesFitted(linear * kAcesExposure);
    return {linearToSrgb(d.x), linearToSrgb(d.y), linearToSrgb(d.z)};
}

float hueDeg(const Vec3& c) {
    const float a = c.x - 0.5f * (c.y + c.z), b = 0.8660254f * (c.y - c.z);
    float h = degrees(std::atan2(b, a));
    return h < 0.0f ? h + 360.0f : h;
}

}  // namespace ts::tonemap
