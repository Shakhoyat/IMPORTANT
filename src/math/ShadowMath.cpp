#include "math/ShadowMath.h"

#include <algorithm>

namespace ts {

namespace {

// Any vector not parallel to d, used as the "up" of a light's lookAt.
Vec3 upFor(const Vec3& d) { return std::fabs(d.y) > 0.95f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}; }

}  // namespace

Sphere frustumSliceSphere(const Mat4& invView, float fovY, float aspect, float zNear, float zFar) {
    // Corners of the slice in view space (camera looks down -Z), then their centroid and max distance.
    const float ty = std::tan(0.5f * fovY);
    const float depths[2] = {zNear, zFar};
    Vec3 corners[8];
    int k = 0;
    for (float z : depths) {
        const float hy = ty * z, hx = hy * aspect;
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sx = -1; sx <= 1; sx += 2) corners[k++] = transformPoint(invView, {sx * hx, sy * hy, -z});
    }
    Vec3 c{0, 0, 0};
    for (const Vec3& p : corners) c = c + p;
    c = c / 8.0f;
    float r = 0.0f;
    for (const Vec3& p : corners) r = std::max(r, length(p - c));
    return {c, r};
}

LightMatrix fitDirectionalShadow(const Vec3& lightDir, const Sphere& bounds, int resolution, float depthMargin) {
    const Vec3 d = normalize(lightDir);
    // One texel of padding: snapping moves the centre by up to one texel.
    const float r = bounds.radius * static_cast<float>(resolution) / static_cast<float>(resolution - 2);
    const float texel = 2.0f * r / static_cast<float>(resolution);

    // Snap the sphere centre to whole texels, measured in a light space anchored at the world origin.
    const Mat4 anchor = lookAt(Vec3{0, 0, 0} - d, {0, 0, 0}, upFor(d));
    Vec3 lc = transformPoint(anchor, bounds.centre);
    lc.x = std::floor(lc.x / texel) * texel;
    lc.y = std::floor(lc.y / texel) * texel;
    const Vec3 snapped = transformPoint(inverse(anchor), lc);

    LightMatrix m;
    const float back = r + depthMargin;  // casters up to depthMargin outside the sphere still cast shadows
    m.view = lookAt(snapped - d * back, snapped, upFor(d));
    m.proj = ortho(-r, r, -r, r, 0.0f, back + r);
    m.viewProj = m.proj * m.view;
    m.texelWorld = texel;
    return m;
}

LightMatrix spotShadowMatrix(const Vec3& position, const Vec3& direction, float outerConeDeg, float zNear, float zFar) {
    const Vec3 d = normalize(direction);
    LightMatrix m;
    m.view = lookAt(position, position + d, upFor(d));
    m.proj = perspective(radians(2.0f * outerConeDeg + 4.0f), 1.0f, zNear, zFar);
    m.viewProj = m.proj * m.view;
    return m;
}

}  // namespace ts
