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
    float depthRange = 0.0f;  // v9 M8: metres along the light per unit of stored depth (orthographic only)
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

// ---- v9 M8: soft shadows (PCSS, Fernando 2005) -----------------------------------------------------------------
// An area light of size w_light seen from a blocker at distance d_blocker and a receiver at d_receiver (both measured
// from the light) casts a penumbra of width (similar triangles)
//     w_penumbra = (d_receiver - d_blocker) * w_light / d_blocker,
// so the shadow is hard where the blocker touches the receiver and widens with the gap (contact hardening).
float pcssPenumbraWidth(float dReceiver, float dBlocker, float lightSize);
// The sun / moon are at infinity: w_light / d_blocker -> the angle the disc subtends, so with depths measured along the
// light direction (metres) w_penumbra = (depth_receiver - depth_blocker) * 2 tan(angularDiameter / 2).
float pcssPenumbraDirectional(float receiverDepth, float blockerDepth, float angularDiameterDeg);
// Radius (metres) around the receiver's shadow-map position that the blocker search covers: a blocker up to
// maxBlockerDistance metres above the receiver can still shade it (half its penumbra reaches this far).
float pcssSearchRadius(float angularDiameterDeg, float maxBlockerDistance);
// PCF kernel radius in texels for a penumbra width (the kernel radius is half the width), clamped.
float pcssKernelTexels(float penumbraWorld, float texelWorld, float minTexels, float maxTexels);
// Effective angular diameters of the key lights. The sun's disc is 0.53 deg; the bright circumsolar aureole
// (forward-scattered sunlight within a few degrees of the disc) widens the source that real daylight shadows show, so
// the shading uses 3x the disc. The moon's disc is the reference for the workbook only: the moonlit night keeps the
// v8 fixed-radius PCF (night parity), PCSS runs while the sun is the key light.
constexpr float kSunSourceDeg = 1.6f, kMoonSourceDeg = 0.53f;
constexpr float kPcssMaxBlockerDistance = 10.0f;   // metres: the blocker search covers blockers up to this far above

// Cascade blending: weight of the far (world) cascade at a view distance. 0 inside the near cascade, 1 at the split,
// a C1-continuous smoothstep over the last bandFraction of the near cascade, so the two filters cross-fade instead of
// switching at a visible seam.
float cascadeBlendWeight(float viewDistance, float split, float bandFraction);
constexpr float kCascadeBlendBand = 0.2f;

}  // namespace ts
