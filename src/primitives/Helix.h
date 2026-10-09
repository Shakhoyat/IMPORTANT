#pragma once

#include "primitives/MeshData.h"

namespace ts {

// Helix coil about local Z inside the unit cube: a circular tube of radius r swept along
//   c(t) = (R cos(2 pi k t), R sin(2 pi k t), L (t - 0.5)),  t in [0, 1],  L = 1 - 2 r,  R + r <= 0.5
// with k turns. Frame along the curve: N = radial unit vector (cos, sin, 0) (perpendicular to the tangent T because
// c' has no radial part), B = T x N; the tube ring at angle psi is c(t) + r (cos(psi) N + sin(psi) B) with that
// direction as its normal. Rings at t_i = i / segments (i = 0..segments), 'sides' vertices each; both ends are closed
// by a flat disc (centre + its own ring, normal -T(0) / +T(1)).
// Vertices V = (segments + 1) sides + 2 (sides + 1), triangles T = 2 segments sides + 2 sides; closed surface.
MeshData createHelix(float R = 0.475f, float r = 0.025f, float turns = 5.0f, int segments = 120, int sides = 6);

}  // namespace ts
