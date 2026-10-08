// Optics and Monte Carlo sampling used by the tracers (the GLSL versions in ShadersRT.cpp mirror these line by line):
// Snell refraction, exact dielectric Fresnel and Schlick's approximation, GGX microfacet distribution with Smith
// masking, cosine-weighted hemisphere and GGX half-vector importance sampling, uniform cone / sphere / disc sampling.
#pragma once

#include <cmath>

#include "raytrace/RtTypes.h"

namespace ts::rt {

// Mirror direction of incoming direction d (pointing towards the surface) about unit normal n.
inline Vec3 reflectDir(const Vec3& d, const Vec3& n) { return d - n * (2.0f * dot(d, n)); }

// Snell's law n1 sin(theta1) = n2 sin(theta2). d: unit incoming direction, n: unit normal facing against d,
// eta = n1 / n2. Returns false on total internal reflection.
inline bool refractDir(const Vec3& d, const Vec3& n, float eta, Vec3& out) {
    const float cosi = -dot(d, n);
    const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k < 0.0f) return false;
    out = normalize(d * eta + n * (eta * cosi - std::sqrt(k)));
    return true;
}

// Exact unpolarised Fresnel reflectance of a dielectric interface. cosi = cosine of the incidence angle (>= 0),
// eta = n1 / n2. 1 under total internal reflection.
inline float fresnelDielectric(float cosi, float eta) {
    const float sin2t = eta * eta * (1.0f - cosi * cosi);
    if (sin2t >= 1.0f) return 1.0f;
    const float cost = std::sqrt(1.0f - sin2t);
    const float rs = (eta * cosi - cost) / (eta * cosi + cost);   // s-polarised (n1 cos i - n2 cos t) / (...)
    const float rp = (cosi - eta * cost) / (cosi + eta * cost);   // p-polarised
    return 0.5f * (rs * rs + rp * rp);
}

inline float schlickWeight(float c) {
    const float m = clampf(1.0f - c, 0.0f, 1.0f);
    const float m2 = m * m;
    return m2 * m2 * m;
}
inline float fresnelSchlick(float cosTheta, float f0) { return f0 + (1.0f - f0) * schlickWeight(cosTheta); }
inline Vec3 fresnelSchlick(float cosTheta, const Vec3& f0) {
    const float w = schlickWeight(cosTheta);
    return f0 + (Vec3{1, 1, 1} - f0) * w;
}

// GGX / Trowbridge-Reitz normal distribution, alpha = roughness^2.
inline float ggxD(float nh, float alpha) {
    const float a2 = alpha * alpha;
    const float d = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}
// Smith G1 for GGX (exact form) and the separable masking-shadowing G = G1(v) G1(l).
inline float smithG1(float nx, float alpha) {
    const float a2 = alpha * alpha;
    return 2.0f * nx / (nx + std::sqrt(a2 + (1.0f - a2) * nx * nx));
}

// Orthonormal basis around unit n (Duff et al. 2017, branchless).
inline void basis(const Vec3& n, Vec3& t, Vec3& b) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    const float c = n.x * n.y * a;
    t = {1.0f + s * n.x * n.x * a, s * c, -s * n.x};
    b = {c, s + n.y * n.y * a, -n.y};
}
inline Vec3 toWorld(const Vec3& local, const Vec3& n) {
    Vec3 t, b;
    basis(n, t, b);
    return t * local.x + b * local.y + n * local.z;
}

// Cosine-weighted hemisphere around n: pdf = cos(theta) / pi.
inline Vec3 sampleCosine(const Vec3& n, float u1, float u2) {
    const float r = std::sqrt(u1), phi = 2.0f * PI * u2;
    return toWorld({r * std::cos(phi), r * std::sin(phi), std::sqrt(std::fmax(0.0f, 1.0f - u1))}, n);
}
// GGX half vector around n: pdf_h = D(h) (n.h).
inline Vec3 sampleGgxHalf(const Vec3& n, float alpha, float u1, float u2) {
    const float a2 = alpha * alpha;
    const float cos2 = (1.0f - u1) / (1.0f + (a2 - 1.0f) * u1);
    const float c = std::sqrt(cos2), s = std::sqrt(std::fmax(0.0f, 1.0f - cos2)), phi = 2.0f * PI * u2;
    return toWorld({s * std::cos(phi), s * std::sin(phi), c}, n);
}
// Uniform direction inside a cone of half-angle acos(cosMax) around axis: pdf = 1 / (2 pi (1 - cosMax)).
inline Vec3 sampleCone(const Vec3& axis, float cosMax, float u1, float u2) {
    const float c = 1.0f - u1 * (1.0f - cosMax), s = std::sqrt(std::fmax(0.0f, 1.0f - c * c)), phi = 2.0f * PI * u2;
    return toWorld({s * std::cos(phi), s * std::sin(phi), c}, axis);
}
inline Vec3 sampleSphere(float u1, float u2) {
    const float z = 1.0f - 2.0f * u1, r = std::sqrt(std::fmax(0.0f, 1.0f - z * z)), phi = 2.0f * PI * u2;
    return {r * std::cos(phi), r * std::sin(phi), z};
}
// Uniform point on the unit disc (concentric mapping not needed here).
inline void sampleDisc(float u1, float u2, float& x, float& y) {
    const float r = std::sqrt(u1), phi = 2.0f * PI * u2;
    x = r * std::cos(phi);
    y = r * std::sin(phi);
}

}  // namespace ts::rt
