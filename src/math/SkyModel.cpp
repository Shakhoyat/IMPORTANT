#include "math/SkyModel.h"

#include <algorithm>

namespace ts {

float perezF(float A, float B, float C, float D, float E, float cosTheta, float gamma) {
    const float cg = std::cos(gamma);
    return (1.0f + A * std::exp(B / std::max(cosTheta, 0.01f))) * (1.0f + C * std::exp(D * gamma) + E * cg * cg);
}

Vec3 xyYToLinearSrgb(float x, float y, float Y) {
    const float yy = std::max(y, 1e-4f);
    const float X = x / yy * Y, Z = (1.0f - x - y) / yy * Y;
    return {std::max(3.2406f * X - 1.5372f * Y - 0.4986f * Z, 0.0f), std::max(-0.9689f * X + 1.8758f * Y + 0.0415f * Z, 0.0f),
            std::max(0.0557f * X - 0.2040f * Y + 1.0570f * Z, 0.0f)};
}

SkyModel preethamSky(const Vec3& toSunIn, float T, float scale) {
    SkyModel m;
    m.turbidity = T;
    m.scale = scale;
    // Sun elevation clamped to the model's range (below the horizon the renderer fades the day sky out instead).
    Vec3 s = normalize(toSunIn);
    const float minY = std::sin(radians(kSkyMinSunElevDeg));
    if (s.y < minY) {
        const float h = std::sqrt(std::max(s.x * s.x + s.z * s.z, 1e-8f));
        const float c = std::sqrt(1.0f - minY * minY);
        s = {s.x / h * c, minY, s.z / h * c};
    }
    m.toSun = s;
    const float ts = std::acos(clampf(s.y, -1.0f, 1.0f));   // sun zenith angle
    // Perez coefficients (Preetham et al. 1999, appendix A.2): luminance Y and chromaticities x, y.
    m.perez[0] = {0.1787f * T - 1.4630f, -0.0193f * T - 0.2592f, -0.0167f * T - 0.2608f};
    m.perez[1] = {-0.3554f * T + 0.4275f, -0.0665f * T + 0.0008f, -0.0950f * T + 0.0092f};
    m.perez[2] = {-0.0227f * T + 5.3251f, -0.0004f * T + 0.2125f, -0.0079f * T + 0.2102f};
    m.perez[3] = {0.1206f * T - 2.5771f, -0.0641f * T - 0.8989f, -0.0441f * T - 1.6537f};
    m.perez[4] = {-0.0670f * T + 0.3703f, -0.0033f * T + 0.0452f, -0.0109f * T + 0.0529f};
    // Zenith luminance (kcd/m^2) and chromaticity (appendix A.2).
    const float chi = (4.0f / 9.0f - T / 120.0f) * (PI - 2.0f * ts);
    const float Yz = std::max((4.0453f * T - 4.9710f) * std::tan(chi) - 0.2155f * T + 2.4192f, 0.05f);
    const float t2 = ts * ts, t3 = t2 * ts, T2 = T * T;
    const float xz = T2 * (0.00166f * t3 - 0.00375f * t2 + 0.00209f * ts) + T * (-0.02903f * t3 + 0.06377f * t2 - 0.03202f * ts + 0.00394f) +
                     (0.11693f * t3 - 0.21196f * t2 + 0.06052f * ts + 0.25886f);
    const float yz = T2 * (0.00275f * t3 - 0.00610f * t2 + 0.00317f * ts) + T * (-0.04214f * t3 + 0.08970f * t2 - 0.04153f * ts + 0.00516f) +
                     (0.15346f * t3 - 0.26756f * t2 + 0.06670f * ts + 0.26688f);
    auto F0 = [&](int c) {
        auto get = [&](int k) { return c == 0 ? m.perez[k].x : (c == 1 ? m.perez[k].y : m.perez[k].z); };
        return perezF(get(0), get(1), get(2), get(3), get(4), 1.0f, ts);
    };
    m.zenith = {Yz / F0(0), xz / F0(1), yz / F0(2)};
    return m;
}

Vec3 skyRadiance(const SkyModel& m, const Vec3& dirIn) {
    Vec3 d = normalize(dirIn);
    const float cosT = std::max(d.y, 0.01f);
    const float gamma = std::acos(clampf(dot(d, m.toSun), -1.0f, 1.0f));
    const Vec3* p = m.perez;
    const float Y = m.zenith.x * perezF(p[0].x, p[1].x, p[2].x, p[3].x, p[4].x, cosT, gamma);
    const float x = m.zenith.y * perezF(p[0].y, p[1].y, p[2].y, p[3].y, p[4].y, cosT, gamma);
    const float y = m.zenith.z * perezF(p[0].z, p[1].z, p[2].z, p[3].z, p[4].z, cosT, gamma);
    return xyYToLinearSrgb(x, y, Y) * m.scale;
}

float shBasis(int i, const Vec3& n) {
    switch (i) {
        case 0: return 0.282095f;
        case 1: return 0.488603f * n.y;
        case 2: return 0.488603f * n.z;
        case 3: return 0.488603f * n.x;
        case 4: return 1.092548f * n.x * n.y;
        case 5: return 1.092548f * n.y * n.z;
        case 6: return 0.315392f * (3.0f * n.z * n.z - 1.0f);
        case 7: return 1.092548f * n.x * n.z;
        default: return 0.546274f * (n.x * n.x - n.y * n.y);
    }
}

namespace {

// Midpoint quadrature over the sphere; f(direction, solid angle).
template <class F>
void forSphere(int thetaSteps, int phiSteps, F f) {
    const float dT = PI / static_cast<float>(thetaSteps), dP = 2.0f * PI / static_cast<float>(phiSteps);
    for (int i = 0; i < thetaSteps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * dT, st = std::sin(t), ct = std::cos(t);
        for (int j = 0; j < phiSteps; ++j) {
            const float p = (static_cast<float>(j) + 0.5f) * dP;
            f(Vec3{st * std::cos(p), ct, st * std::sin(p)}, st * dT * dP);   // y up
        }
    }
}

}  // namespace

AmbientSH ambientSH(const SkyModel& m, const Vec3& ground, int thetaSteps, int phiSteps) {
    AmbientSH sh;
    forSphere(thetaSteps, phiSteps, [&](const Vec3& w, float dw) {
        const Vec3 L = w.y > 0.0f ? skyRadiance(m, w) : ground;
        for (int i = 0; i < 9; ++i) sh.c[i] = sh.c[i] + L * (shBasis(i, w) * dw);
    });
    // Cosine-lobe convolution (irradiance) and / pi (the shader's ambient term is irradiance / pi).
    const float band[9] = {PI, 2.0f * PI / 3.0f, 2.0f * PI / 3.0f, 2.0f * PI / 3.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f};
    for (int i = 0; i < 9; ++i) sh.c[i] = sh.c[i] * (band[i] / PI);
    return sh;
}

Vec3 evalAmbient(const AmbientSH& sh, const Vec3& n) {
    Vec3 e{0, 0, 0};
    for (int i = 0; i < 9; ++i) e = e + sh.c[i] * shBasis(i, n);
    return {std::max(e.x, 0.0f), std::max(e.y, 0.0f), std::max(e.z, 0.0f)};
}

Vec3 ambientBruteForce(const SkyModel& m, const Vec3& ground, const Vec3& nIn, int thetaSteps, int phiSteps) {
    const Vec3 n = normalize(nIn);
    Vec3 e{0, 0, 0};
    forSphere(thetaSteps, phiSteps, [&](const Vec3& w, float dw) {
        const float c = dot(n, w);
        if (c > 0.0f) e = e + (w.y > 0.0f ? skyRadiance(m, w) : ground) * (c * dw);
    });
    return e / PI;
}

Vec3 horizonRadiance(const SkyModel& m) {
    Vec3 sum{0, 0, 0};
    int n = 0;
    for (int k = 0; k < 4; ++k) {
        const float e = radians(2.0f + 2.0f * static_cast<float>(k));
        for (int j = 0; j < 32; ++j) {
            const float a = 2.0f * PI * (static_cast<float>(j) + 0.5f) / 32.0f;
            sum = sum + skyRadiance(m, {std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a)});
            ++n;
        }
    }
    return sum / static_cast<float>(n);
}

}  // namespace ts
