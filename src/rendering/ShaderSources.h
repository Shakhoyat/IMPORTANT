// GLSL sources, kept in C++ raw strings so the program runs from any working directory.
#pragma once

#include <string>

namespace ts::glsl {

extern const char* kFlatVert;     // Flat: geometry shader lights each triangle once at its centroid
extern const char* kFlatGeom;
extern const char* kFlatFrag;
extern const char* kGouraudVert;  // Gouraud: lighting per vertex, colour interpolated
extern const char* kGouraudFrag;
extern const char* kPhongVert;    // Phong: normal interpolated, lighting per fragment
extern const char* kPhongFrag;
extern const char* kBeamVert;     // additive light cone
extern const char* kBeamFrag;
extern const char* kLineVert;     // wireframe / axes / planner overlay
extern const char* kLineFrag;
extern const char* kDepthVert;    // shadow maps: depth only
extern const char* kDepthFrag;
extern const char* kPrepassVert;  // depth + view-space normal (SSAO, volumetric fog)
extern const char* kPrepassFrag;

// ShadersPost.cpp
extern const char* kSkyVert;      // night sky (full-screen triangle at the far plane)
extern const char* kSkyFrag;      // + palette
extern const char* kFullscreenVert;
extern const char* kSsaoFrag;     // + palette
extern const char* kBlurFrag;
extern const char* kFogFrag;      // + palette
extern const char* kBloomDownFrag;
extern const char* kBloomUpFrag;
extern const char* kCompositeFrag;  // + palette
extern const char* kHudVert;      // 2D overlay (NDC), optional texture
extern const char* kHudFrag;

// Source assembly: palette only (sky, HUD); + lighting (vertex/geometry stages); + output (fragment stage).
std::string withPalette(const char* body);
std::string withLighting(const char* body);
std::string withLightingAndOutput(const char* body);

}  // namespace ts::glsl
