// Light-space matrices for shadow mapping (pure math, no OpenGL, so it is unit-tested).
//   Moon (directional): orthographic cascades fitted around bounding spheres, snapped to whole texels
//   so the shadow edges do not shimmer when the camera moves.
//   Searchlight (spot): perspective projection along the lamp axis.
#pragma once

#include "math/Math3D.h"

namespace ts {

struct LightMatrix {
    Mat4 view, proj, viewProj;
    float texelWorld = 0.0f;  // size of one shadow-map texel in metres (orthographic only)
};

struct Sphere {
    Vec3 centre;
    float radius = 0.0f;
};

// Bounding sphere of the camera frustum slice between view depths zNear and zFar.
// invView = camera-to-world matrix, fovY in radians.
Sphere frustumSliceSphere(const Mat4& invView, float fovY, float aspect, float zNear, float zFar);

// Orthographic light matrix that contains 'bounds'. lightDir = direction the light travels.
// The centre is snapped to the texel grid of a 'resolution' x 'resolution' map (stable shadows).
LightMatrix fitDirectionalShadow(const Vec3& lightDir, const Sphere& bounds, int resolution, float depthMargin);

// Perspective light matrix of a spot light: apex at 'position', looking along 'direction'.
LightMatrix spotShadowMatrix(const Vec3& position, const Vec3& direction, float outerConeDeg, float zNear, float zFar);

}  // namespace ts
